# QRecall AutoQEC ESP32 Demo --- Zero-to-Hero Setup, Build, Flash, and Run

This guide documents the end-to-end workflow used for the QRecall
AutoQEC demo on **Windows + WSL2 (Ubuntu 24.04)**, using two ESP32
boards, ESP-NOW, Arduino CLI, and a Python JSONL logger.

## 1. What this demo does

1.  The **TX ESP32** accepts a text message over USB serial.
2.  It repetition-encodes each message bit three times and injects
    software-generated bit flips.
3.  It broadcasts the packet using ESP-NOW.
4.  The **RX ESP32** receives the packet and prints a JSON record over
    USB serial. It does **not** decode or correct the message.
5.  A Python logger on the PC reads the receiver's serial output and
    appends JSON records to a `.jsonl` file.

A successful TX line such as `ESP-NOW send request accepted.` means the
ESP-NOW stack accepted the send request; it does not alone guarantee
delivery. A JSON record in the receiver log is evidence that a packet
reached the receiver/logger.

## 2. Project layout

Run commands from the project directory:

``` bash
cd ~/qrecall-automation/qrecall-automation
```

Expected files:

``` text
qrecall-automation/
├── qrecall.sh
├── qrecall.conf
├── TXNoiseDebug/
│   └── TXNoiseDebug.ino
├── RXReceiver/
│   └── RXReceiver.ino
├── pc_logger/
│   └── rx_logger.py
└── runs/
```

Check that the required files exist:

``` bash
ls -l TXNoiseDebug/TXNoiseDebug.ino RXReceiver/RXReceiver.ino pc_logger/rx_logger.py
```

If you downloaded `qrecall-automation_bundle.zip` to Windows Downloads
and your Windows user folder is `ab32a`, an example extraction workflow
is:

``` bash
mkdir -p ~/qrecall-automation
unzip /mnt/c/Users/ab32a/Downloads/qrecall-automation_bundle.zip -d ~/qrecall-automation
cd ~/qrecall-automation/qrecall-automation
```

The archive contains the automation script/config example/README; make
sure the three firmware/logger files are also copied into the expected
folders before building.

## 3. Install the Linux-side tools

In Ubuntu/WSL:

``` bash
sudo apt update
sudo apt install -y tmux python3 python3-serial unzip usbutils
```

Confirm tools:

``` bash
arduino-cli version
python3 --version
tmux -V
```

If `arduino-cli` is not installed, install Arduino CLI using the
official Arduino CLI installation instructions. The ESP32 Arduino
platform must also be installed in Arduino CLI's board manager. Confirm
the expected board FQBN is available:

``` bash
arduino-cli core list
arduino-cli board list
```

This project has been configured for:

``` text
esp32:esp32:esp32
```

If your installed board package uses a different board FQBN, update the
script/config accordingly rather than assuming the value is correct for
every ESP32 variant.

## 4. Connect both ESP32s to WSL2 over USB

### 4.1 Keep an Ubuntu/WSL terminal open

Open Ubuntu 24.04 and leave it open while attaching USB devices.

### 4.2 List USB devices in Windows PowerShell

Open **PowerShell as Administrator**:

``` powershell
usbipd list
```

Identify the two USB-to-UART adapters. They may appear as Silicon Labs
CP210x, CH340, or another USB serial adapter. Record each device's
actual **BUSID**.

### 4.3 Bind each device (Administrator PowerShell)

Replace the example IDs with the BUSIDs shown by `usbipd list`:

``` powershell
usbipd bind --busid 1-4
usbipd bind --busid 1-5
```

Do not copy `1-4` and `1-5` literally unless those are the actual IDs on
your machine.

### 4.4 Attach each device to WSL (normal PowerShell)

``` powershell
usbipd attach --wsl --busid 1-4
usbipd attach --wsl --busid 1-5
```

Again, use the actual BUSIDs. A USB device attached to WSL is not
simultaneously available to Windows applications.

### 4.5 Verify inside Ubuntu

``` bash
lsusb
ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
```

For this setup, both adapters appeared as:

``` text
/dev/ttyUSB0
/dev/ttyUSB1
```

The exact device names can change after disconnecting/reconnecting, so
verify them each session.

List ports detected by the QRecall script:

``` bash
./qrecall.sh ports
```

Example output from this setup:

``` text
Serial ports visible to WSL:
/dev/serial/by-id/usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_0001-if00-port0

Kernel serial devices:
/dev/ttyUSB0
/dev/ttyUSB1
```

A `/dev/serial/by-id` entry may exist for only one adapter, depending on
the adapter's USB serial information. The kernel devices are still
useful, but confirm which physical board maps to which port.

### 4.6 Identify TX and RX port mapping

To identify the physical board associated with each port:

1.  Disconnect one ESP32 USB cable.
2.  Run `./qrecall.sh ports` and note which device disappears.
3.  Reconnect it and wait for it to reappear.
4.  Repeat for the other board if needed.

The mapping used during the successful demo was:

-   TX: `/dev/ttyUSB0`
-   RX: `/dev/ttyUSB1`

Use this mapping only if it still matches the current session.

### 4.7 Fix serial permissions if necessary

Inspect group membership:

``` bash
groups
```

If `dialout` is missing, run:

``` bash
sudo usermod -aG dialout "$USER"
newgrp dialout
```

Check read/write access:

``` bash
test -r /dev/ttyUSB0 && test -w /dev/ttyUSB0 && echo "USB0 accessible"
test -r /dev/ttyUSB1 && test -w /dev/ttyUSB1 && echo "USB1 accessible"
```

If the group change does not take effect in the current shell, close and
reopen Ubuntu, then check again.

## 5. Configure QRecall ports

Open the configuration file:

``` bash
nano qrecall.conf
```

The configuration used in the working session contained:

``` bash
TX_PORT="/dev/ttyUSB0"
RX_PORT="/dev/ttyUSB1"
```

If your `qrecall.conf` has these keys, set them to the verified current
mapping. If the variable names differ in your version, preserve the
existing names and edit only the port values.

Save in nano with **Ctrl+O**, press **Enter**, then exit with
**Ctrl+X**.

Review the configuration:

``` bash
cat qrecall.conf
```

Verify the three source files again:

``` bash
ls -l TXNoiseDebug/TXNoiseDebug.ino RXReceiver/RXReceiver.ino pc_logger/rx_logger.py
```

## 6. Build/compile both firmware projects

From the project directory:

``` bash
cd ~/qrecall-automation/qrecall-automation
./qrecall.sh build
```

This is the recommended project-level build command. It should compile
the TX and RX sketches without flashing them.

If it fails, read the full compiler output and resolve that error before
deploying. Do not proceed to flashing unless the build succeeds.

### Optional: check Arduino CLI directly

Arduino CLI compile syntax follows this pattern:

``` bash
arduino-cli compile --fqbn esp32:esp32:esp32 TXNoiseDebug
arduino-cli compile --fqbn esp32:esp32:esp32 RXReceiver
```

Run these from the project directory. The sketch folder and `.ino`
filename should match (for example, `TXNoiseDebug/TXNoiseDebug.ino`).

## 7. Flash and deploy

Once the build succeeds, verify that both boards are still connected and
that `qrecall.conf` has the correct ports.

Run:

``` bash
./qrecall.sh deploy
```

The automation script is intended to compile/flash the receiver and
transmitter and then start the logging/monitoring session. Follow any
prompts printed by the script. Do not unplug the boards during flashing.

### Manual Arduino CLI commands (if needed)

The generic upload form is:

``` bash
arduino-cli upload --fqbn esp32:esp32:esp32 --port /dev/ttyUSB1 RXReceiver
arduino-cli upload --fqbn esp32:esp32:esp32 --port /dev/ttyUSB0 TXNoiseDebug
```

Use the verified ports. Some Arduino CLI versions/platforms may use `-p`
instead of `--port`; check `arduino-cli upload --help` if your installed
version rejects the option. The project's `./qrecall.sh deploy` command
is preferred because it centralizes the configured port mapping.

## 8. What the running session should look like

The QRecall session uses `tmux` to split the terminal:

-   **Receiver/logger pane:** runs `pc_logger/rx_logger.py` against the
    RX serial port and appends JSONL records.
-   **Transmitter pane:** runs Arduino CLI's serial monitor against the
    TX serial port, allowing you to type test messages.

Example TX monitor command used by the automation session:

``` bash
arduino-cli monitor --port /dev/ttyUSB0 --config baudrate=115200
```

The receiver logger was launched in the form:

``` bash
python3 pc_logger/rx_logger.py --port /dev/ttyUSB1 --baud 115200 --output runs/<RUN_ID>/received.jsonl
```

`<RUN_ID>` is a timestamped run directory created by the automation
script. Do not type the placeholder literally.

**Do not open a second serial monitor on `/dev/ttyUSB1` while the Python
logger is using it.** A serial port generally should be owned by only
one reader at a time.

## 9. Send a test message

In the **TX monitor pane**, type a short test message and press Enter:

``` text
QHACK 2026
```

The successful test showed output similar to:

``` text
----- TX DEBUG -----
Original: QHACK 2026
Sequence: 1
Encoded bits: 240
Flipped bits: 22
Configured noise: 10%
ESP-NOW send request accepted.
--------------------
```

Exact sequence and flip counts will vary between transmissions because
software noise is randomized.

For `QHACK 2026`, the text has 10 characters. With 8 bits per character
and three repetitions per bit:

``` text
10 characters × 8 bits/character × 3 = 240 encoded bits
```

A sample receiver JSON record looked like:

``` json
{"seq":1,"raw":"QHACK 2026","noise_percent":10,"flip_count":22,"encoded_bits_per_char":24,"encoded_bits_total":240,"observed_noise_percent":9.17,"noisy_encoded_hex":"...","sender_mac":"68:25:DD:31:F5:BC","receiver_count":1}
```

The `noisy_encoded_hex` field is abbreviated here for readability; the
actual log record contains the encoded data.

The example's observed noise is:

``` text
22 / 240 × 100 = 9.1666...% ≈ 9.17%
```

The TX's `ESP-NOW send request accepted.` line alone is not proof of
delivery. A corresponding RX JSON record is the stronger end-to-end
check.

## 10. Find and inspect the JSONL output

Open a **new WSL terminal** so you do not interfere with the active tmux
session.

``` bash
cd ~/qrecall-automation/qrecall-automation
ls -lt runs/
```

Find the newest `received.jsonl` file:

``` bash
LATEST=$(find runs -type f -name received.jsonl -printf '%T@ %p\n' |
  sort -nr | head -n 1 | cut -d' ' -f2-)

echo "Latest log: $LATEST"
```

Show the last few records:

``` bash
tail -n 10 "$LATEST"
```

Count records:

``` bash
wc -l "$LATEST"
```

Each valid JSON line is one JSONL record. The logger adds
`pc_received_at_utc` when supported by the current logger
implementation.

### Validate that every line is JSON

``` bash
python3 - "$LATEST" <<'PY'
import json
import sys

path = sys.argv[1]
valid = 0
invalid = 0

with open(path, encoding="utf-8") as f:
    for number, line in enumerate(f, 1):
        try:
            json.loads(line)
            valid += 1
        except json.JSONDecodeError as exc:
            invalid += 1
            print(f"Invalid JSON at line {number}: {exc}")

print(f"\nValid JSON records: {valid}")
print(f"Invalid lines: {invalid}")
PY
```

Expected: one or more valid records, and ideally zero invalid lines.

## 11. Repeat the test

In the TX monitor pane, send another message:

``` text
QEC TEST
```

Then inspect the newest JSONL file again:

``` bash
tail -n 5 "$LATEST"
```

Confirm that a new record appears and the sequence number advances. If
no record appears, investigate the serial logger, board/channel
configuration, or ESP-NOW receive path before changing firmware.

## 12. Useful commands reference

### Windows PowerShell

Run `usbipd list` in Administrator PowerShell:

``` powershell
usbipd list
```

Administrator PowerShell, replacing BUSIDs with actual IDs:

``` powershell
usbipd bind --busid <BUSID_TX>
usbipd bind --busid <BUSID_RX>
```

Normal PowerShell:

``` powershell
usbipd attach --wsl --busid <BUSID_TX>
usbipd attach --wsl --busid <BUSID_RX>
```

Optional detach from WSL (normal PowerShell; use the actual BUSID):

``` powershell
usbipd detach --busid <BUSID_TX>
usbipd detach --busid <BUSID_RX>
```

### WSL/Linux

``` bash
cd ~/qrecall-automation/qrecall-automation
lsusb
ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
./qrecall.sh ports
groups
cat qrecall.conf
./qrecall.sh build
./qrecall.sh deploy
arduino-cli monitor --port /dev/ttyUSB0 --config baudrate=115200
```

To stop a serial monitor, use `Ctrl+C`. This stops the monitor process;
it does not erase the firmware stored on the ESP32.

## 13. Troubleshooting

### No serial devices appear

1.  Run `usbipd list` in Windows PowerShell.
2.  Ensure both devices are bound/shared and attached to WSL.
3.  Run `lsusb` in WSL.
4.  Run `ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null`.
5.  Re-run `./qrecall.sh ports`.

Official WSL USB instructions:
https://learn.microsoft.com/en-us/windows/wsl/connect-usb

### `Permission denied` opening `/dev/ttyUSB0` or `/dev/ttyUSB1`

``` bash
groups
sudo usermod -aG dialout "$USER"
newgrp dialout
```

Then re-test device access.

### Build fails

``` bash
arduino-cli version
arduino-cli core list
arduino-cli board list
./qrecall.sh build
```

Resolve the compiler error before deployment. Confirm the ESP32 platform
and FQBN match your board.

### Logger is running but the JSONL file is empty

-   Confirm the logger uses the RX port, not the TX port.
-   Confirm the TX monitor uses the TX port.
-   Verify the two ESP32s use the same ESP-NOW channel and compatible
    packet definitions.
-   Send a short test message and inspect both panes.
-   Check the correct run directory and file using
    `find runs -name received.jsonl -type f`.

### The receiver pane shows unreadable diamond/replacement characters

The terminal may be receiving non-text bytes, serial output at an
unexpected baud rate, or startup/reset output that is not valid UTF-8.
The fact that a later JSON record is readable does not prove every
preceding byte was valid. Check that both firmware and the
logger/monitor use the intended `115200` baud rate, and validate the
saved JSONL lines using the Python check above before modifying
firmware.

### Need to identify which physical board is `/dev/ttyUSB0`

Disconnect one ESP32, run `./qrecall.sh ports`, and observe which device
disappears. Reconnect it and repeat for the other board. Port numbering
may change after reconnection.

## 14. Scope and limitations

-   The receiver logs the original text field supplied in the packet and
    the noisy encoded payload. It does not perform majority-vote
    decoding or error correction.
-   `noise_percent` is the configured flip probability;
    `observed_noise_percent` is computed from the actual flip count and
    encoded-bit total.
-   ESP-NOW send-request acceptance is not the same as a delivery
    acknowledgement.
-   The packet layout, ESP-NOW channel, and callback API must match
    between the two firmware sketches and the installed ESP32 Arduino
    core.

## 15. Official references

-   Microsoft Learn --- Connect USB devices to WSL 2:
    https://learn.microsoft.com/en-us/windows/wsl/connect-usb
-   Arduino CLI --- Getting started (compile and upload):
    https://docs.arduino.cc/arduino-cli/getting-started
-   Arduino CLI --- Monitor command:
    https://docs.arduino.cc/arduino-cli/commands-reference/arduino-cli_monitor
-   Arduino CLI --- Upload command:
    https://docs.arduino.cc/arduino-cli/commands-reference/arduino-cli_upload
