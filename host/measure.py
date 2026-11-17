#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = [
#   "pyserial>=3.5",
# ]
# ///

import argparse
import datetime
import re
import sys
import time
from pathlib import Path

import serial


def utc_prefix():
    now = datetime.datetime.now(datetime.timezone.utc)
    return now.isoformat(timespec="milliseconds").replace("+00:00", "Z")


def utc_filename_stamp():
    now = datetime.datetime.now(datetime.timezone.utc)
    return now.strftime("%Y-%m-%dT%H-%M-%S") + f"-{now.microsecond // 1000:03d}Z"


def command_mode(command):
    parts = command.strip().split()
    if not parts:
        return "X"
    match = re.fullmatch(r"mode([0-5])", parts[0], re.IGNORECASE)
    return match.group(1) if match else "X"


def parse_end(line):
    parts = line.split()
    if len(parts) < 3 or parts[0] != "END":
        return None
    status = parts[2]
    if status.startswith("status="):
        status = status.split("=", 1)[1]
    return parts[1], status


def main():
    parser = argparse.ArgumentParser(
        description="Send a measurement command and log the firmware response."
    )
    parser.add_argument("--port", default="/dev/ttyACM0",
                        help="serial device, for example /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200,
                        help="serial baud rate")
    parser.add_argument("--timeout", type=float, default=60.0,
                        help="command timeout in seconds")
    parser.add_argument("--log-dir", default=".",
                        help="directory for timestamped logs")
    parser.add_argument("--verbose", action="store_true",
                        help="print received lines")
    parser.add_argument("command", nargs="?",
                        help="command line, for example 'mode1 1000 1000'")
    args = parser.parse_args()

    command = args.command
    if command is None:
        command = input("command> ")
    command = command.strip()
    if not command:
        print("error=empty_command", file=sys.stderr)
        return 2

    expected_mode = command_mode(command)
    log_dir = Path(args.log_dir)
    log_dir.mkdir(parents=True, exist_ok=True)
    log_path = log_dir / f"log_mode{expected_mode}_{utc_filename_stamp()}.txt"

    try:
        port = serial.Serial(
            args.port,
            baudrate=args.baud,
            timeout=0.2,
            write_timeout=2.0,
        )
    except serial.SerialException as exc:
        print(f"error=serial_open detail={exc}", file=sys.stderr)
        return 2

    begin_seen = False
    begin_mode = None
    end_seen = False
    end_mode = None
    end_status = None
    firmware_error = None
    line_count = 0
    deadline = time.monotonic() + args.timeout

    try:
        with port, log_path.open("w", encoding="utf-8") as log_file:
            port.reset_input_buffer()
            port.write((command + "\n").encode("ascii"))
            port.flush()

            while True:
                if time.monotonic() > deadline:
                    firmware_error = firmware_error or "timeout"
                    break

                raw = port.readline()
                if not raw:
                    continue

                line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                if not line:
                    continue

                line_count += 1
                log_file.write(f"{utc_prefix()} {line}\n")
                log_file.flush()
                if args.verbose:
                    print(line)

                if line.startswith("BEGIN"):
                    parts = line.split()
                    if len(parts) >= 2:
                        begin_seen = True
                        begin_mode = parts[1]
                        if expected_mode != "X" and begin_mode != expected_mode:
                            firmware_error = "mode_mismatch"
                            break

                if line.startswith("ERR"):
                    firmware_error = line
                    if not begin_seen:
                        break

                end = parse_end(line)
                if end is not None:
                    end_seen = True
                    end_mode, end_status = end
                    break
    except serial.SerialException as exc:
        firmware_error = f"serial_error:{exc}"

    if begin_seen and end_seen and begin_mode != end_mode:
        firmware_error = firmware_error or "mode_mismatch"
    if end_seen and end_status != "OK":
        firmware_error = firmware_error or f"status={end_status}"
    if not begin_seen:
        firmware_error = firmware_error or "missing_begin"
    if not end_seen:
        firmware_error = firmware_error or "missing_end"

    if firmware_error is not None:
        print(f"error={firmware_error} log={log_path}")
        return 1

    print(f"mode={end_mode} status={end_status} lines={
          line_count} log={log_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
