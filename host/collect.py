#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = [
#   "pyserial>=3.5",
# ]
# ///

import argparse
import datetime
import json
import re
import subprocess
import sys
from pathlib import Path


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat(
        timespec="milliseconds"
    ).replace("+00:00", "Z")


def utc_stamp():
    now = datetime.datetime.now(datetime.timezone.utc)
    return now.strftime("%Y-%m-%dT%H-%M-%S") + f"-{now.microsecond // 1000:03d}Z"


def safe_name(value):
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "_", value).strip("._-")
    return cleaned or "board"


def parse_modes(value):
    modes = []
    for part in value.split(","):
        part = part.strip()
        if not part:
            continue
        try:
            mode = int(part)
        except ValueError as exc:
            raise argparse.ArgumentTypeError(f"invalid mode: {part}") from exc
        if mode < 0 or mode > 5:
            raise argparse.ArgumentTypeError(f"mode out of range: {mode}")
        if mode not in modes:
            modes.append(mode)
    if not modes:
        raise argparse.ArgumentTypeError("at least one mode is required")
    return modes


def build_commands(args):
    commands = {
        0: "mode0",
        1: f"mode1 {args.mode1_hz} {args.mode1_duration_ms}",
        2: f"mode2 {args.mode2_hz} {args.mode2_samples}",
        3: f"mode3 {args.mode3_hz} {args.mode3_samples}",
        4: f"mode4 {args.mode4_trials}",
        5: f"mode5 {args.mode5_start_hz} {args.mode5_step_hz} {args.mode5_max_hz}",
    }
    return [(mode, commands[mode]) for mode in args.modes]


def latest_log(run_dir, mode):
    logs = sorted(
        run_dir.glob(f"log_mode{mode}_*.txt"),
        key=lambda path: path.stat().st_mtime_ns,
    )
    return logs[-1] if logs else None


def main():
    parser = argparse.ArgumentParser(
        description="Collect mode0-mode5 data from one board into a timestamped run directory."
    )
    parser.add_argument("--board", required=True, help="board label, for example g491re")
    parser.add_argument("--port", default="/dev/ttyACM0",
                        help="serial device, for example /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200,
                        help="serial baud rate")
    parser.add_argument("--timeout", type=float, default=60.0,
                        help="per-command timeout in seconds")
    parser.add_argument("--out-dir", default="data",
                        help="root directory for collected runs")
    parser.add_argument("--modes", type=parse_modes, default=parse_modes("0,1,2,3,4,5"),
                        help="comma-separated modes to run, default 0,1,2,3,4,5")
    parser.add_argument("--mode1-hz", type=int, default=1000)
    parser.add_argument("--mode1-duration-ms", type=int, default=1000)
    parser.add_argument("--mode2-hz", type=int, default=1000)
    parser.add_argument("--mode2-samples", type=int, default=10000)
    parser.add_argument("--mode3-hz", type=int, default=1000)
    parser.add_argument("--mode3-samples", type=int, default=1000)
    parser.add_argument("--mode4-trials", type=int, default=100)
    parser.add_argument("--mode5-start-hz", type=int, default=100)
    parser.add_argument("--mode5-step-hz", type=int, default=200)
    parser.add_argument("--mode5-max-hz", type=int, default=5000)
    parser.add_argument("--keep-going", action="store_true",
                        help="continue after a failed mode")
    parser.add_argument("--verbose", action="store_true",
                        help="pass through every received firmware line")
    parser.add_argument("--dry-run", action="store_true",
                        help="print commands without opening the serial port")
    args = parser.parse_args()

    measure_script = Path(__file__).resolve().with_name("measure.py")
    if not measure_script.exists():
        print(f"error=missing_measure script={measure_script}", file=sys.stderr)
        return 2

    commands = build_commands(args)
    run_dir = Path(args.out_dir).expanduser() / safe_name(args.board) / utc_stamp()

    if args.dry_run:
        print(f"board={args.board} port={args.port} run_dir={run_dir}")
        for mode, command in commands:
            print(f"mode{mode}: {command}")
        return 0

    try:
        run_dir.mkdir(parents=True, exist_ok=False)
    except OSError as exc:
        print(f"error=create_run_dir detail={exc}", file=sys.stderr)
        return 2

    started = utc_now()
    records = []
    ok = True

    for mode, command in commands:
        cmd = [
            sys.executable,
            str(measure_script),
            "--port", args.port,
            "--baud", str(args.baud),
            "--timeout", str(args.timeout),
            "--log-dir", str(run_dir),
        ]
        if args.verbose:
            cmd.append("--verbose")
        cmd.append(command)

        print(f"[mode{mode}] {command}")
        try:
            proc = subprocess.run(cmd, text=True, capture_output=True)
        except OSError as exc:
            print(f"error=run_measure mode={mode} detail={exc}", file=sys.stderr)
            records.append({
                "mode": mode,
                "command": command,
                "returncode": None,
                "log": None,
                "stdout": "",
                "stderr": str(exc),
            })
            ok = False
            if not args.keep_going:
                break
            continue

        stdout = proc.stdout.strip()
        stderr = proc.stderr.strip()
        if stdout:
            print(stdout)
        if stderr:
            print(stderr, file=sys.stderr)

        log_path = latest_log(run_dir, mode)
        records.append({
            "mode": mode,
            "command": command,
            "returncode": proc.returncode,
            "log": log_path.name if log_path else None,
            "stdout": stdout,
            "stderr": stderr,
        })

        if proc.returncode != 0:
            ok = False
            print(f"error=mode_failed mode={mode} rc={proc.returncode}", file=sys.stderr)
            if not args.keep_going:
                break

    manifest = {
        "board": args.board,
        "port": args.port,
        "baud": args.baud,
        "started_utc": started,
        "finished_utc": utc_now(),
        "ok": ok,
        "commands": records,
    }
    manifest_path = run_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    print(f"ok={str(ok).lower()} run_dir={run_dir} manifest={manifest_path}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
