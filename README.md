# QRecall AutoQEC — ESP32 Repetition-Code Noise Demonstrator

A two-ESP32 demonstration for transmitting repetition-encoded text with software-injected bit flips over ESP-NOW, receiving the noisy encoded payload, and logging receiver output as JSON Lines (JSONL).

> **Scope:** The transmitter performs 3× repetition encoding and software bit-flip injection. The receiver receives and reports the packet; it does **not** decode or perform error correction. The `raw` message field is transmitter-supplied metadata, not a message reconstructed by the receiver.

## Architecture

```text
PC / WSL2
  | USB serial (text input)
  v
TX ESP32
  | 3× repetition encoding
  | software bit-flip injection
  | ESP-NOW broadcast
  v
RX ESP32
  | JSON over USB serial (no decoding/correction)
  v
Python logger
  | append valid JSON objects, one per line
  v
runs/<session>/received.jsonl
```

## Repository layout

```text
qrecall-automation/
├── .gitignore
├── README.md
├── qrecall.sh
├── qrecall.conf.example
├── TXNoiseDebug/TXNoiseDebug.ino
├── RXReceiver/RXReceiver.ino
├── pc_logger/rx_logger.py
└── docs/INSTRUCTIONS.md
```

## Requirements

- Windows with WSL 2 and Ubuntu (the demonstrated workflow used Ubuntu 24.04)
- Two ESP32 development boards
- `usbipd-win` for USB passthrough into WSL 2
- Arduino CLI with the ESP32 Arduino platform installed
- Python 3 and `pyserial`
- `tmux`, `usbutils`, and standard Linux utilities

The automation script defaults to the Arduino FQBN `esp32:esp32:esp32`. Confirm this FQBN matches the exact ESP32 board variant being used.

## 1. Install Linux-side dependencies

In Ubuntu/WSL:

```bash
sudo apt update
sudo apt install -y tmux python3 python3-serial unzip usbutils
```

Confirm the required tools are present:

```bash
arduino-cli version
arduino-cli core list
arduino-cli board list
python3 --version
tmux -V
```

Install Arduino CLI and the ESP32 board platform if they are not already installed.

## 2. Attach both ESP32 boards to WSL 2

Open **PowerShell as Administrator** and list connected USB devices:

```powershell
usbipd list
```

Identify the two USB-to-UART adapters and their actual BUSIDs. Bind each device if it is not already shared:

```powershell
usbipd bind --busid <TX_BUSID>
usbipd bind --busid <RX_BUSID>
```

Then use **normal PowerShell** to attach both to WSL:

```powershell
usbipd attach --wsl --busid <TX_BUSID>
usbipd attach --wsl --busid <RX_BUSID>
```

Replace the placeholders with the BUSIDs shown on your computer.

Verify from Ubuntu:

```bash
lsusb
ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
./qrecall.sh ports
```

To identify which physical ESP32 corresponds to a port, disconnect one board, run `./qrecall.sh ports`, and note which device disappears. Repeat for the other board. Device numbering may change after reconnecting; use `/dev/serial/by-id/` paths where available.

If you get a serial permission error, check group membership and add your user to `dialout` if needed:

```bash
groups
sudo usermod -aG dialout "$USER"
newgrp dialout
```

If the new group is not reflected in the current shell, open a new Ubuntu terminal. Verify read/write access using the actual device paths, for example:

```bash
test -r /dev/ttyUSB0 && test -w /dev/ttyUSB0 && echo "USB0 accessible"
test -r /dev/ttyUSB1 && test -w /dev/ttyUSB1 && echo "USB1 accessible"
```

## 3. Configure the TX and RX serial ports

Copy the example configuration and edit it:

```bash
cp qrecall.conf.example qrecall.conf
nano qrecall.conf
```

Set the verified paths. Example only:

```bash
TX_PORT="/dev/ttyUSB0"
RX_PORT="/dev/ttyUSB1"
```

Prefer stable `/dev/serial/by-id/...` paths if the adapters expose unique IDs. **Do not commit `qrecall.conf`**: it is machine-specific and ignored by Git. Commit only `qrecall.conf.example`.

## 4. Compile both ESP32 sketches

From the repository root:

```bash
./qrecall.sh build
```

This compiles TX and RX without flashing. Fix compiler errors before continuing. To override the default board FQBN:

```bash
FQBN="esp32:esp32:esp32" ./qrecall.sh build
```

## 5. Flash and deploy

After a successful build and after checking the port mapping:

```bash
./qrecall.sh deploy
```

The automation script compiles both sketches, flashes the RX first and then the TX, creates a timestamped folder under `runs/`, and launches a tmux session containing the RX JSON logger and TX serial monitor.

The Python logger owns the RX serial port during the session. Do not open a second serial monitor on that same RX port.

## 6. Send a test message

In the TX serial-monitor pane, enter a test message such as:

```text
QHACK 2026
```

For ten ASCII characters, with 8 bits per character and three repetitions per bit:

```text
10 characters × 8 bits/character × 3 = 240 encoded bits
```

The TX debug output reports the sequence number, encoded-bit count, flip count, configured noise, and ESP-NOW send-request status.

`ESP-NOW send request accepted` means the local ESP-NOW stack accepted the request; it is not by itself a delivery acknowledgement. A corresponding JSON record from the RX logger is evidence that the packet reached the receiver and was logged.

## 7. Inspect the JSONL output

Open a second WSL terminal without stopping the running logger:

```bash
LATEST=$(find runs -type f -name received.jsonl -printf '%T@ %p\n' | sort -nr | head -n 1 | cut -d' ' -f2-)
echo "Latest log: $LATEST"
wc -l "$LATEST"
tail -n 10 "$LATEST"
```

Validate that each non-empty line is valid JSON:

```bash
python3 - "$LATEST" <<'PY'
import json
import sys

valid = invalid = 0
with open(sys.argv[1], encoding="utf-8") as stream:
    for line_number, line in enumerate(stream, 1):
        if not line.strip():
            continue
        try:
            json.loads(line)
            valid += 1
        except json.JSONDecodeError as exc:
            invalid += 1
            print(f"Invalid JSON at line {line_number}: {exc}")

print(f"Valid JSON records: {valid}")
print(f"Invalid lines: {invalid}")
PY
```

## Automation commands

Run these from the repository root:

| Command | Purpose |
|---|---|
| `./qrecall.sh ports` | List serial devices visible to WSL |
| `./qrecall.sh build` | Compile TX and RX sketches |
| `./qrecall.sh flash` | Compile and flash both ESP32 boards |
| `./qrecall.sh deploy` | Compile, flash, and launch logger/monitor session |
| `./qrecall.sh monitor` | Launch logger/monitor session without flashing |
| `./qrecall.sh stop` | Stop the QRecall tmux session |

To open the TX monitor directly:

```bash
arduino-cli monitor --port /dev/ttyUSB0 --config baudrate=115200
```

Use the currently verified TX port. `Ctrl+C` exits the host-side monitor; it does not erase the firmware stored on the ESP32.

## Troubleshooting

### No serial ports appear in WSL

- Run `usbipd list` in Windows PowerShell.
- Confirm that both USB adapters are attached to WSL.
- Run `lsusb` and `ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null` in Ubuntu.
- Run `./qrecall.sh ports` again.

See [Microsoft's WSL USB documentation](https://learn.microsoft.com/en-us/windows/wsl/connect-usb).

### Permission denied on a serial port

Ensure your user belongs to `dialout`, then open a new Ubuntu shell if the group change has not taken effect.

### Build fails

Check the Arduino CLI version, installed ESP32 core, and selected FQBN. Resolve the first compiler error before flashing.

### Logger runs but no JSON is saved

Check that the logger is using the RX port and the monitor is using the TX port. Verify both firmware sketches agree on the ESP-NOW channel and packet structure, and confirm the RX firmware has been flashed.

### Unreadable serial characters

Confirm firmware, logger, and monitor use the intended baud rate (default `115200`). Startup output may appear before JSON. The logger prints non-JSON serial lines for diagnostics but persists only valid JSON objects.

## Detailed setup guide

See [`docs/INSTRUCTIONS.md`](docs/INSTRUCTIONS.md) for the full step-by-step WSL2, USB passthrough, build, flash, run, and troubleshooting instructions.

## References

- [Microsoft Learn: Connect USB devices to WSL](https://learn.microsoft.com/en-us/windows/wsl/connect-usb)
- [Arduino CLI documentation](https://docs.arduino.cc/arduino-cli/)
- [Arduino CLI monitor command](https://docs.arduino.cc/arduino-cli/commands-reference/arduino-cli_monitor)
- [Arduino CLI upload command](https://docs.arduino.cc/arduino-cli/commands-reference/arduino-cli_upload)
