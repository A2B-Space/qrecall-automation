#!/usr/bin/env python3

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

import serial


def main():
    parser = argparse.ArgumentParser(
        description="Save QRecall ESP32 receiver JSON records."
    )
    parser.add_argument(
        "--port",
        default="/dev/ttyUSB0",
        help="RX ESP32 serial port (default: /dev/ttyUSB0)",
    )
    parser.add_argument(
        "--baud",
        type=int,
        default=115200,
        help="Serial baud rate (default: 115200)",
    )
    parser.add_argument(
        "--output",
        default="qrecall_received.jsonl",
        help="Output JSONL file",
    )

    args = parser.parse_args()
    output_path = Path(args.output).expanduser()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    print(f"RX serial port : {args.port}")
    print(f"Baud rate      : {args.baud}")
    print(f"Output file    : {output_path.resolve()}")
    print("Waiting for JSON records. Press Ctrl+C to stop.")

    saved_count = 0

    try:
        with serial.Serial(
            args.port,
            args.baud,
            timeout=1,
        ) as ser, output_path.open(
            "a",
            encoding="utf-8",
        ) as output_file:

            # Opening a serial port can reset some ESP32 boards.
            # Ignore startup messages until valid JSON arrives.
            while True:
                raw_line = ser.readline()

                if not raw_line:
                    continue

                line = raw_line.decode(
                    "utf-8",
                    errors="replace",
                ).strip()

                if not line:
                    continue

                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    print(f"[SERIAL] {line}")
                    continue

                if not isinstance(record, dict):
                    print("[SKIP] JSON record is not an object.")
                    continue

                # Add PC-side reception time without replacing
                # the receiver's sequence or other fields.
                record["pc_received_at_utc"] = (
                    datetime.now(timezone.utc).isoformat()
                )

                output_file.write(
                    json.dumps(record, ensure_ascii=False) + "\n"
                )
                output_file.flush()

                saved_count += 1

                print(
                    f"[SAVED #{saved_count}] "
                    f"seq={record.get('seq')} "
                    f"raw={record.get('raw')!r} "
                    f"flips={record.get('flip_count')}"
                )

    except KeyboardInterrupt:
        print(f"\nStopped. Records saved this session: {saved_count}")

    except serial.SerialException as exc:
        print(f"\nSerial error: {exc}", file=sys.stderr)
        print(
            "Check the port, permissions, USB attachment, and "
            "whether another program is using the serial port.",
            file=sys.stderr,
        )
        sys.exit(1)


if __name__ == "__main__":
    main()
