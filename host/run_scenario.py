#!/usr/bin/env python3
"""Run the fixed G491RE validation scenario and write result reports."""

import argparse
import csv
import datetime as dt
import json
import math
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOGS = ROOT / "logs"

MODE1_FREQS = (100, 333, 500, 777, 1000, 1234, 2000, 2345, 4000)
MODE1_TABLE_FREQS = (100, 500, 1000, 2000, 4000)
MODE2_FREQS = (500, 1000, 2000, 4000)
EXPECTED_BOARD = "g491re"
EXPECTED_CLOCK_HZ = 170000000
EXPECTED_PSC = 169
EXPECTED_RAW_SAMPLES = 100
EXPECTED_TRIALS = 100
MODE4_SIGMA_MIN_US = 1.0
MODE4_SIGMA_EXPECTED_US = 10.0
CONDITION_KEYS = {
    "load_pct",
    "busy_pct",
    "idle_pct",
    "peak_load_pct",
    "isr_count",
    "isr_cycles",
    "period_cycles",
    "idle_cycles",
    "busy_cycles",
    "total_cycles",
    "iterations",
    "main_loop_iteration_us",
    "main_loop_mean_us",
    "wfe_present",
    "wfe_scope",
    "peak_isr_cycles",
    "peak_isr_us",
}


class RunRecord:
    def __init__(self, label, command, canonical, ok, fields, conditions,
                 timing, raw, attempts):
        self.label = label
        self.command = command
        self.canonical = canonical
        self.ok = ok
        self.fields = fields
        self.conditions = conditions
        self.timing = timing
        self.raw = raw
        self.attempts = attempts


def log_msg(message):
    print(message, file=sys.stderr, flush=True)


def utc_now():
    return dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds").replace(
        "+00:00", "Z"
    )


def strip_timestamp(line):
    return re.sub(r"^\d{4}-\d{2}-\d{2}T\S+Z\s+", "", line)


def parse_log(path):
    fields = {}
    conditions = {}
    timing = []
    raw = []
    current_condition = None
    current_timing = None
    in_raw = False

    if path is None or not path.exists():
        return fields, conditions, timing, raw

    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = strip_timestamp(raw_line)

        if line.startswith("condition="):
            current_condition = line.split("=", 1)[1].strip()
            conditions.setdefault(current_condition, {})
            current_timing = None
            in_raw = False
            continue

        if line.startswith("timing_hz="):
            current_timing = {"timing_hz": line.split("=", 1)[1].strip()}
            timing.append(current_timing)
            current_condition = None
            in_raw = False
            continue

        if line.startswith("raw_count="):
            current_condition = None
            current_timing = None
            in_raw = True

        if line.startswith("END "):
            in_raw = False
            current_condition = None
            current_timing = None

        if in_raw and re.fullmatch(r"\d+", line):
            raw.append(int(line))
            continue

        for key, value in re.findall(r"([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)", line):
            if current_condition is not None and key in CONDITION_KEYS:
                conditions[current_condition][key] = value
            elif current_timing is not None and key.startswith("timing_"):
                current_timing[key] = value
            else:
                fields[key] = value

    return fields, conditions, timing, raw


def find_new_log(start_epoch):
    candidates = [
        path
        for path in LOGS.glob("log_mode*.txt")
        if path.stat().st_mtime >= start_epoch - 1.0
    ]
    if not candidates:
        return None
    return max(candidates, key=lambda path: path.stat().st_mtime_ns)


def run_measure(label, command, canonical, timeout, port, generated,
                descriptions, warnings):
    canonical = Path(canonical)
    canonical.parent.mkdir(parents=True, exist_ok=True)
    attempts = []

    for attempt in (1, 2):
        start_epoch = time.time()
        cmd = [
            sys.executable,
            "host/measure.py",
            "--port",
            port,
            "--baud",
            "115200",
            "--timeout",
            str(timeout),
            "--log-dir",
            str(LOGS),
            command,
        ]
        log_msg(f"[run] {label} attempt {attempt}/2: {command}")
        try:
            proc = subprocess.run(
                cmd,
                cwd=ROOT,
                text=True,
                capture_output=True,
                timeout=timeout + 30.0,
            )
            stdout = proc.stdout
            stderr = proc.stderr
            rc = proc.returncode
        except subprocess.TimeoutExpired as exc:
            stdout = exc.stdout if isinstance(exc.stdout, str) else ""
            stderr = exc.stderr if isinstance(exc.stderr, str) else ""
            rc = 124

        log_path = None
        for text in (stdout, stderr):
            match = re.search(r"log=(\S+)", text)
            if match:
                candidate = Path(match.group(1))
                if not candidate.is_absolute():
                    candidate = ROOT / candidate
                if candidate.exists():
                    log_path = candidate
                    break
        if log_path is None:
            log_path = find_new_log(start_epoch)

        if log_path is not None:
            generated.add(log_path)
            descriptions[log_path] = f"{label} attempt {attempt}: `{command}`"

        attempts.append(
            {
                "attempt": attempt,
                "rc": rc,
                "log": log_path,
                "stdout": stdout.strip(),
                "stderr": stderr.strip(),
            }
        )

        if rc == 0 and log_path is not None:
            break
        time.sleep(0.5)

    successful = next(
        (item for item in attempts if item["rc"] == 0 and item["log"] is not None),
        None,
    )
    source = successful or attempts[-1]
    if source.get("log") is not None and source["log"].exists():
        shutil.copyfile(source["log"], canonical)
        generated.add(canonical)
        descriptions[canonical] = f"canonical log for {label}: `{command}`"

    fields, conditions, timing, raw = parse_log(canonical)
    ok = successful is not None
    if not ok:
        warnings.append(f"RUN_FAILED_{label.upper()}")

    log_msg(f"[{'ok' if ok else 'fail'}] {label} -> {canonical.name}")
    return RunRecord(
        label, command, canonical, ok, fields, conditions, timing, raw, attempts
    )


def fget(record, key):
    if record is None:
        return None
    value = record.fields.get(key)
    if value is None:
        return None
    try:
        return float(value)
    except ValueError:
        return None


def cget(record, condition, key):
    if record is None or condition not in record.conditions:
        return None
    value = record.conditions[condition].get(key)
    if value is None:
        return None
    try:
        return float(value)
    except ValueError:
        return None


def cget_str(record, condition, key):
    if record is None or condition not in record.conditions:
        return None
    return record.conditions[condition].get(key)


def mean(values):
    return sum(values) / len(values) if values else None


def sigma(values):
    return statistics.stdev(values) if len(values) > 1 else (
        0.0 if values else None
    )


def fmt(value, decimals=3):
    if value is None:
        return "N/A"
    return f"{value:.{decimals}f}"


def fmt_mean_sigma(value_mean, value_sigma, decimals=3):
    if value_mean is None:
        return "N/A"
    if value_sigma is None:
        return fmt(value_mean, decimals)
    return f"{fmt(value_mean, decimals)} ± {fmt(value_sigma, decimals)}"


def csv_num(value, decimals=6):
    if value is None:
        return ""
    return f"{value:.{decimals}f}"


def expected_arr(hz):
    period_us = 1000000 // hz
    if period_us == 0:
        period_us = 1
    if period_us > 65535:
        period_us = 65535
    return period_us - 1


def expected_error_ppm(hz):
    arr = expected_arr(hz)
    actual_micro_hz = 1000000000000 // (arr + 1)
    requested_micro_hz = hz * 1000000
    return ((actual_micro_hz - requested_micro_hz) * 1000000) // requested_micro_hz


def validate_mode0(record):
    errors = []
    if not record.ok:
        return ["mode0 run failed"]

    if record.fields.get("board") != EXPECTED_BOARD:
        errors.append(f"board={record.fields.get('board')!r}")
    clock = fget(record, "clock_hz")
    if clock is None or int(clock) != EXPECTED_CLOCK_HZ:
        errors.append(f"clock_hz={record.fields.get('clock_hz')!r}")
    for key in ("dwt", "timer", "uart", "i2c"):
        if record.fields.get(key) != "ok":
            errors.append(f"{key}={record.fields.get(key)!r}")
    if record.fields.get("encoder_addr") != "0x36":
        errors.append(f"encoder_addr={record.fields.get('encoder_addr')!r}")
    if "encoder_read" in record.fields:
        errors.append("encoder_read=fail")
    if record.fields.get("encoder_status") is None:
        errors.append("encoder_status missing")

    if len(record.timing) != len(MODE1_FREQS):
        errors.append(f"timing groups={len(record.timing)} expected={len(MODE1_FREQS)}")
    else:
        for hz, group in zip(MODE1_FREQS, record.timing):
            try:
                group_hz = int(group.get("timing_hz", ""))
                psc = int(group.get("timing_psc", ""))
                arr = int(group.get("timing_arr", ""))
                period_us = int(group.get("timing_period_us", ""))
                actual_hz = float(group.get("timing_actual_hz", ""))
                error_ppm = int(group.get("timing_error_ppm", ""))
            except ValueError:
                errors.append(f"timing group for {hz} Hz is incomplete")
                continue
            if group_hz != hz:
                errors.append(f"timing order {group_hz} != {hz}")
            if psc != EXPECTED_PSC:
                errors.append(f"{hz} Hz psc={psc} expected={EXPECTED_PSC}")
            if arr != expected_arr(hz):
                errors.append(f"{hz} Hz arr={arr} expected={expected_arr(hz)}")
            if period_us != arr + 1:
                errors.append(f"{hz} Hz period_us={period_us} != arr+1")
            expected_actual = 1000000.0 / (arr + 1)
            if abs(actual_hz - expected_actual) > 0.001:
                errors.append(
                    f"{hz} Hz actual={actual_hz} expected={expected_actual:.3f}"
                )
            expected_ppm = expected_error_ppm(hz)
            if abs(error_ppm - expected_ppm) > 1:
                errors.append(
                    f"{hz} Hz error_ppm={error_ppm} expected={expected_ppm}"
                )

    return errors


def write_raw_csv(path, raw_values):
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["raw_dwt_cycles"])
        for value in raw_values:
            writer.writerow([value])


def write_mode4_raw_csv(path, raw_values, clock_hz):
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["trial", "latency_cycles", "latency_us"])
        for index, value in enumerate(raw_values):
            latency_us = value * 1e6 / clock_hz
            writer.writerow([index, value, f"{latency_us:.6f}"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--board", default=EXPECTED_BOARD)
    args = parser.parse_args()

    started = time.monotonic()
    LOGS.mkdir(parents=True, exist_ok=True)
    generated = set()
    descriptions = {}
    warnings = []
    records = []

    # Step 0: baseline diagnostics. A failure stops the scenario.
    mode0 = run_measure(
        "mode0_diag",
        "mode0",
        LOGS / "mode0_diag.log",
        10.0,
        args.port,
        generated,
        descriptions,
        warnings,
    )
    records.append(mode0)
    mode0_errors = validate_mode0(mode0)
    if mode0_errors:
        print("STOP: Step 0 failed: " + "; ".join(mode0_errors), file=sys.stderr)
        return 2

    board = mode0.fields.get("board", args.board)
    results_dir = ROOT / "results" / board
    results_dir.mkdir(parents=True, exist_ok=True)

    # Step 1: frequency accuracy, 9 frequencies x 5 runs.
    mode1 = {}
    for freq in MODE1_FREQS:
        recs = []
        for run in range(1, 6):
            record = run_measure(
                f"mode1_{freq}hz_run{run}",
                f"mode1 {freq} 2000",
                LOGS / f"mode1_{freq}hz_run{run}.log",
                30.0,
                args.port,
                generated,
                descriptions,
                warnings,
            )
            recs.append(record)
            records.append(record)
        ok = [
            record
            for record in recs
            if record.ok and fget(record, "rel_error_pct") is not None
        ]
        values = [fget(record, "rel_error_pct") for record in ok]
        mode1[freq] = {
            "n_ok": len(ok),
            "n_total": len(recs),
            "mean": mean(values),
            "sigma": sigma(values),
        }
        if len(ok) < len(recs):
            warnings.append(f"MODE1_{freq}HZ_INCOMPLETE")

    # Step 2: interval jitter, 4 frequencies x 3 runs.
    mode2 = {}
    for freq in MODE2_FREQS:
        recs = []
        for run in range(1, 4):
            record = run_measure(
                f"mode2_{freq}hz_run{run}",
                f"mode2 {freq} 10000",
                LOGS / f"mode2_{freq}hz_run{run}.log",
                90.0,
                args.port,
                generated,
                descriptions,
                warnings,
            )
            recs.append(record)
            records.append(record)
        ok = [
            record
            for record in recs
            if record.ok and fget(record, "sigma_ns") is not None
        ]
        sigma_us = [fget(record, "sigma_ns") / 1000.0 for record in ok]
        p2p_us = [
            fget(record, "p2p_ns") / 1000.0
            for record in ok
            if fget(record, "p2p_ns") is not None
        ]
        mode2[freq] = {
            "n_ok": len(ok),
            "n_total": len(recs),
            "sigma_mean_us": mean(sigma_us),
            "sigma_sigma_us": sigma(sigma_us),
            "p2p_mean_us": mean(p2p_us),
            "mean_interval_us": mean(
                [fget(record, "mean_interval_us") for record in ok]
            ),
        }
        if len(ok) < len(recs):
            warnings.append(f"MODE2_{freq}HZ_INCOMPLETE")

    # Step 3: raw DWT dump verification, 4 frequencies x 1 run.
    raw_checks = {}
    for freq in MODE2_FREQS:
        record = run_measure(
            f"mode2_{freq}hz_raw",
            f"mode2 {freq} {EXPECTED_RAW_SAMPLES} raw=1",
            LOGS / f"mode2_{freq}hz_raw.log",
            30.0,
            args.port,
            generated,
            descriptions,
            warnings,
        )
        records.append(record)
        raw_csv = LOGS / f"mode2_{freq}hz_raw.csv"
        fw_sigma_ns = fget(record, "sigma_ns")
        clock_hz = fget(record, "clock_hz") or float(EXPECTED_CLOCK_HZ)
        offline_sigma_ns = None
        relative_error = None
        if len(record.raw) >= 2:
            offline_sigma_ns = (
                statistics.stdev(record.raw) * 1e9 / clock_hz
            )
            if fw_sigma_ns is not None:
                if offline_sigma_ns > 0:
                    relative_error = abs(fw_sigma_ns - offline_sigma_ns) / offline_sigma_ns
                else:
                    relative_error = 0.0 if fw_sigma_ns == 0 else math.inf
            write_raw_csv(raw_csv, record.raw)
            generated.add(raw_csv)
            descriptions[raw_csv] = (
                f"raw DWT cycles for mode2 {freq} Hz raw verification"
            )
            if record.fields.get("raw_count") is not None:
                if int(record.fields["raw_count"]) != len(record.raw):
                    warnings.append(f"RAW_COUNT_MISMATCH_{freq}HZ")
        else:
            warnings.append(f"RAW_DUMP_MISSING_{freq}HZ")

        if relative_error is not None and relative_error > 0.05:
            warnings.append(f"SIGMA_MISMATCH_{freq}HZ")

        raw_checks[freq] = {
            "n": len(record.raw),
            "fw_sigma_ns": fw_sigma_ns,
            "offline_sigma_ns": offline_sigma_ns,
            "relative_error": relative_error,
            "ok": record.ok,
        }

    # Step 4: timer-free idle and constant-load conditions.
    mode3_idle = run_measure(
        "mode3_idle",
        "mode3 0 1000",
        LOGS / "mode3_idle.log",
        30.0,
        args.port,
        generated,
        descriptions,
        warnings,
    )
    records.append(mode3_idle)
    mode3_constant = run_measure(
        "mode3_constant",
        "mode3 1000 1000",
        LOGS / "mode3_constant.log",
        30.0,
        args.port,
        generated,
        descriptions,
        warnings,
    )
    records.append(mode3_constant)

    idle_load = cget(mode3_idle, "idle", "busy_pct")
    if idle_load is None:
        idle_load = cget(mode3_idle, "idle", "load_pct")
    idle_pct = cget(mode3_idle, "idle", "idle_pct")
    idle_peak_load = cget(mode3_idle, "idle", "peak_load_pct")
    idle_main_loop_us = fget(mode3_idle, "main_loop_iteration_us")
    if idle_main_loop_us is None:
        idle_main_loop_us = fget(mode3_idle, "main_loop_mean_us")
    idle_wfe = cget_str(mode3_idle, "idle", "wfe_present")
    const_load = cget(mode3_constant, "constant", "load_pct")
    const_peak_load = cget(mode3_constant, "constant", "peak_load_pct")
    const_peak_us = cget(mode3_constant, "constant", "peak_isr_us")
    const_peak_cycles = cget(mode3_constant, "constant", "peak_isr_cycles")
    worst_isr_us = max(
        [value for value in (cget(mode3_idle, "idle", "peak_isr_us"), const_peak_us)
         if value is not None],
        default=None,
    )
    worst_isr_cycles = max(
        [value for value in (cget(mode3_idle, "idle", "peak_isr_cycles"),
                             const_peak_cycles)
         if value is not None],
        default=None,
    )
    if not mode3_idle.ok or idle_load is None:
        warnings.append("MODE3_IDLE_FAILED")
    if not mode3_constant.ok or const_load is None:
        warnings.append("MODE3_CONSTANT_FAILED")
    if idle_wfe != "yes":
        warnings.append("MODE3_WFE_MISSING")
    if idle_load is not None:
        if idle_load > 50.0:
            warnings.append("MODE3_IDLE_BUSY_HIGH")
        elif idle_load > 5.0:
            warnings.append("MODE3_IDLE_BUSY_ABOVE_TARGET")

    # Step 5: UART-trigger-to-first-step latency.
    mode4 = run_measure(
        "mode4_run1",
        "mode4 100",
        LOGS / "mode4_run1.log",
        60.0,
        args.port,
        generated,
        descriptions,
        warnings,
    )
    records.append(mode4)
    latency_mean = fget(mode4, "mean_us") if mode4.ok else None
    latency_sigma = fget(mode4, "sigma_us") if mode4.ok else None
    latency_min = fget(mode4, "min_us") if mode4.ok else None
    latency_max = fget(mode4, "max_us") if mode4.ok else None
    latency_p99 = fget(mode4, "p99_us") if mode4.ok else None
    latency_ref = mode4.fields.get("latency_ref") if mode4.ok else None
    if latency_p99 is None:
        warnings.append("P99_MISSING")
    if latency_ref != "uart_first_byte_to_first_step_gpio":
        warnings.append("MODE4_LATENCY_REF_UNEXPECTED")

    mode4_raw_csv = LOGS / "mode4_raw.csv"
    if mode4.ok and mode4.raw:
        clock_hz = fget(mode4, "clock_hz") or float(EXPECTED_CLOCK_HZ)
        write_mode4_raw_csv(mode4_raw_csv, mode4.raw, clock_hz)
        generated.add(mode4_raw_csv)
        descriptions[mode4_raw_csv] = (
            "raw Mode 4 UART-first-byte to first-step GPIO latency samples"
        )
    else:
        warnings.append("MODE4_RAW_MISSING")

    if mode4.ok:
        raw_count = mode4.fields.get("raw_count")
        if raw_count is None or int(raw_count) != len(mode4.raw):
            warnings.append("MODE4_RAW_COUNT_MISMATCH")
        if len(mode4.raw) != EXPECTED_TRIALS:
            warnings.append("MODE4_RAW_COUNT_UNEXPECTED")
        num_trials = fget(mode4, "num_trials")
        if num_trials is None or int(num_trials) != EXPECTED_TRIALS:
            warnings.append("MODE4_TRIALS_UNEXPECTED")

    if latency_sigma is not None:
        if latency_sigma < MODE4_SIGMA_MIN_US:
            warnings.append("MODE4_SIGMA_LOW")
            print(
                "STOP: Mode 4 sigma < 1 us; raw samples written to "
                f"{mode4_raw_csv}",
                file=sys.stderr,
            )
        elif latency_sigma < MODE4_SIGMA_EXPECTED_US:
            warnings.append("MODE4_SIGMA_BELOW_EXPECTED")

    if any(not record.ok for record in records):
        warnings.append("RUN_FAILURES_PRESENT")

    warnings = list(dict.fromkeys(warnings))
    warnings_text = ", ".join(warnings) if warnings else "None"

    # Table aggregation.
    freq_cells = {}
    for freq in MODE1_TABLE_FREQS:
        item = mode1[freq]
        if item["mean"] is None:
            freq_cells[freq] = (
                "N/A",
                f"no successful runs ({item['n_ok']}/{item['n_total']})",
            )
        else:
            note = f"mean ± σ over {item['n_ok']}/{item['n_total']} runs"
            if item["n_ok"] < item["n_total"]:
                note += f"; MODE1_{freq}HZ_INCOMPLETE"
            freq_cells[freq] = (
                fmt_mean_sigma(item["mean"], item["sigma"]),
                note,
            )

    jitter_cells = {}
    for freq in MODE2_FREQS:
        item = mode2[freq]
        if item["sigma_mean_us"] is None:
            jitter_cells[freq] = (
                "N/A",
                f"no successful runs ({item['n_ok']}/{item['n_total']})",
            )
        else:
            note = (
                f"mean of {item['n_ok']}/{item['n_total']} run σ; "
                f"run-to-run σ={fmt(item['sigma_sigma_us'])}"
            )
            if item["p2p_mean_us"] is not None:
                note += f"; mean p2p={fmt(item['p2p_mean_us'])} µs"
            if item["n_ok"] < item["n_total"]:
                note += f"; MODE2_{freq}HZ_INCOMPLETE"
            jitter_cells[freq] = (fmt(item["sigma_mean_us"]), note)

    cpu_idle_cell = fmt(idle_load)
    cpu_idle_note = (
        "timer-free foreground load; busy time outside WFE; interrupt time "
        "during WFE remains in idle_cycles"
    )
    idle_condition = mode3_idle.conditions.get("idle", {})
    if "busy_cycles" in idle_condition and "idle_cycles" in idle_condition:
        cpu_idle_note += (
            f"; busy_cycles={idle_condition['busy_cycles']}"
            f"; idle_cycles={idle_condition['idle_cycles']}"
            f"; total_cycles={idle_condition.get('total_cycles', 'missing')}"
        )
    if idle_pct is not None:
        cpu_idle_note += f"; idle_pct={fmt(idle_pct)}%"
    if idle_main_loop_us is not None:
        cpu_idle_note += (
            f"; main_loop_iteration_us={fmt(idle_main_loop_us)}"
        )
    if idle_wfe is not None:
        cpu_idle_note += f"; wfe_present={idle_wfe}"
    if idle_peak_load is not None:
        cpu_idle_note += f"; peak_load={fmt(idle_peak_load)}%"
    if not mode3_idle.ok or idle_load is None:
        cpu_idle_note += "; MODE3_IDLE_FAILED"

    cpu_const_cell = fmt(const_load)
    cpu_const_note = "from `mode3 1000 1000` constant condition"
    if const_peak_load is not None:
        cpu_const_note += f"; peak_load={fmt(const_peak_load)}%"
    if const_peak_cycles is not None:
        cpu_const_note += f"; worst_isr_cycles={fmt(const_peak_cycles, 0)}"
    if not mode3_constant.ok or const_load is None:
        cpu_const_note += "; MODE3_CONSTANT_FAILED"

    worst_isr_cell = fmt(worst_isr_us)
    worst_isr_note = (
        f"max peak_isr_us across idle/constant conditions; "
        f"worst cycles={fmt(worst_isr_cycles, 0)}"
    )

    latency_max_p99_cell = f"{fmt(latency_max)} / {fmt(latency_p99)}"
    latency_ref_note = latency_ref or "missing"
    latency_max_p99_note = (
        f"from `mode4 100`; latency_ref={latency_ref_note}"
    )
    if latency_min is not None:
        latency_max_p99_note += f"; min={fmt(latency_min)} µs"
    if latency_p99 is None:
        latency_max_p99_note += "; P99_MISSING"

    clock_mhz = fget(mode0, "clock_hz") / 1e6 if fget(mode0, "clock_hz") else None
    table = f"""| Metric | Value | Notes |
| :--- | :--- | :--- |
| **Board** | {board} | |
| **System clock (MHz)** | {fmt(clock_mhz, 1)} | from Mode 0 |
| **Freq. error @ 100 Hz (%)** | {freq_cells[100][0]} | {freq_cells[100][1]} |
| **Freq. error @ 500 Hz (%)** | {freq_cells[500][0]} | {freq_cells[500][1]} |
| **Freq. error @ 1000 Hz (%)** | {freq_cells[1000][0]} | {freq_cells[1000][1]} |
| **Freq. error @ 2000 Hz (%)** | {freq_cells[2000][0]} | {freq_cells[2000][1]} |
| **Freq. error @ 4000 Hz (%)** | {freq_cells[4000][0]} | {freq_cells[4000][1]} |
| **Jitter σ @ 500 Hz (µs)** | {jitter_cells[500][0]} | {jitter_cells[500][1]} |
| **Jitter σ @ 1000 Hz (µs)** | {jitter_cells[1000][0]} | {jitter_cells[1000][1]} |
| **Jitter σ @ 2000 Hz (µs)** | {jitter_cells[2000][0]} | {jitter_cells[2000][1]} |
| **Jitter σ @ 4000 Hz (µs)** | {jitter_cells[4000][0]} | {jitter_cells[4000][1]} |
| **CPU busy load, idle condition (%)** | {cpu_idle_cell} | {cpu_idle_note} |
| **CPU idle, idle condition (%)** | {fmt(idle_pct)} | from `mode3 0 1000`; expected >95% |
| **CPU load, constant 1 kHz (%)** | {cpu_const_cell} | {cpu_const_note} |
| **Worst ISR duration (µs)** | {worst_isr_cell} | {worst_isr_note} |
| **UART trigger-to-step mean (µs)** | {fmt(latency_mean)} | from `mode4 100`; latency_ref={latency_ref or "missing"} |
| **UART trigger-to-step σ (µs)** | {fmt(latency_sigma)} | from `mode4 100`; expected ≈ period / √12 (28.9 µs at 100 µs) |
| **UART trigger-to-step max / p99 (µs)** | {latency_max_p99_cell} | {latency_max_p99_note} |
| **Warnings** | {warnings_text} | see summary.csv notes |
"""

    table_path = results_dir / "table.md"
    table_path.write_text(table, encoding="utf-8")

    csv_rows = []

    def add(metric, value_mean, value_sigma, unit, notes):
        csv_rows.append(
            {
                "board": board,
                "metric": metric,
                "value_mean": value_mean,
                "value_sigma": value_sigma,
                "unit": unit,
                "notes": notes,
            }
        )

    add(
        "System clock (MHz)",
        csv_num(clock_mhz, 3) if clock_mhz is not None else "N/A",
        "",
        "MHz",
        "from Mode 0",
    )
    for freq in MODE1_FREQS:
        item = mode1[freq]
        if item["mean"] is None:
            add(
                f"Freq. error @ {freq} Hz (%)",
                "N/A",
                "",
                "%",
                f"no successful runs ({item['n_ok']}/{item['n_total']})",
            )
        else:
            note = (
                f"mean and sample sigma over {item['n_ok']}/{item['n_total']} runs"
            )
            if item["n_ok"] < item["n_total"]:
                note += f"; MODE1_{freq}HZ_INCOMPLETE"
            add(
                f"Freq. error @ {freq} Hz (%)",
                csv_num(item["mean"]),
                csv_num(item["sigma"]),
                "%",
                note,
            )

    for freq in MODE2_FREQS:
        item = mode2[freq]
        if item["sigma_mean_us"] is None:
            add(
                f"Jitter σ @ {freq} Hz (µs)",
                "N/A",
                "",
                "us",
                f"no successful runs ({item['n_ok']}/{item['n_total']})",
            )
        else:
            note = (
                f"mean of per-run sigma over {item['n_ok']}/{item['n_total']} runs; "
                f"sigma is run-to-run; mean p2p={fmt(item['p2p_mean_us'])} us"
            )
            if item["n_ok"] < item["n_total"]:
                note += f"; MODE2_{freq}HZ_INCOMPLETE"
            add(
                f"Jitter σ @ {freq} Hz (µs)",
                csv_num(item["sigma_mean_us"]),
                csv_num(item["sigma_sigma_us"]),
                "us",
                note,
            )

    add(
        "CPU busy load, idle condition (%)",
        csv_num(idle_load),
        "",
        "%",
        cpu_idle_note,
    )
    add(
        "CPU idle, idle condition (%)",
        csv_num(idle_pct),
        "",
        "%",
        "from mode3 0 1000; expected >95%",
    )
    add(
        "CPU load, constant 1 kHz (%)",
        csv_num(const_load),
        "",
        "%",
        cpu_const_note,
    )
    add(
        "Worst ISR duration (µs)",
        csv_num(worst_isr_us),
        "",
        "us",
        worst_isr_note,
    )
    add(
        "UART trigger-to-step mean (µs)",
        csv_num(latency_mean),
        "",
        "us",
        f"from mode4 100; latency_ref={latency_ref or 'missing'}",
    )
    add(
        "UART trigger-to-step σ (µs)",
        csv_num(latency_sigma),
        "",
        "us",
        "from mode4 100; expected ≈ period / sqrt(12) (28.9 us at 100 us)",
    )
    add(
        "UART trigger-to-step max (µs)",
        csv_num(latency_max),
        "",
        "us",
        f"from mode4 100; latency_ref={latency_ref or 'missing'}",
    )
    add(
        "UART trigger-to-step p99 (µs)",
        csv_num(latency_p99),
        "",
        "us",
        (
            f"from mode4 100; latency_ref={latency_ref or 'missing'}"
            if latency_p99 is not None
            else "P99_MISSING"
        ),
    )
    add("Warnings", warnings_text, "", "", warnings_text)

    csv_path = results_dir / "summary.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=["board", "metric", "value_mean", "value_sigma", "unit", "notes"],
        )
        writer.writeheader()
        writer.writerows(csv_rows)

    # Raw index: only files generated by this scenario run.
    raw_lines = [
        "# Raw Data Index",
        "",
        f"Board: `{board}`  ",
        f"Firmware: `{mode0.fields.get('firmware', 'unknown')}`  ",
        f"Mode 0 baseline: `{(LOGS / 'mode0_diag.log').relative_to(ROOT)}`",
        "",
        "Canonical logs are copies of the first successful attempt, or the final failed attempt when no retry succeeded. Timestamped originals are preserved beside them.",
        "",
        "| Log file | Description |",
        "| :--- | :--- |",
    ]
    for path in sorted(generated, key=lambda item: str(item.relative_to(ROOT))):
        if not path.exists():
            continue
        rel = path.relative_to(ROOT)
        desc = descriptions.get(path, "unclassified log file")
        raw_lines.append(f"| `{rel}` | {desc} |")
    raw_lines.append("")
    raw_path = results_dir / "raw_index.md"
    raw_path.write_text("\n".join(raw_lines), encoding="utf-8")

    run_data = []
    for record in records:
        run_data.append(
            {
                "label": record.label,
                "command": record.command,
                "canonical_log": str(record.canonical.relative_to(ROOT)),
                "ok": record.ok,
                "fields": record.fields,
                "conditions": record.conditions,
                "timing": record.timing,
                "raw_count": len(record.raw),
                "attempts": [
                    {
                        "attempt": item["attempt"],
                        "rc": item["rc"],
                        "log": str(item["log"].relative_to(ROOT))
                        if item["log"] is not None
                        else None,
                        "stdout": item["stdout"],
                        "stderr": item["stderr"],
                    }
                    for item in record.attempts
                ],
            }
        )
    (results_dir / "run_data.json").write_text(
        __import__("json").dumps(run_data, indent=2) + "\n", encoding="utf-8"
    )

    print(table)
    print()
    print(f"CSV: {csv_path}")
    print(f"Raw index: {raw_path}")
    for warning in warnings:
        print(f"[WARN] {warning}")
    elapsed = time.monotonic() - started
    print(f"Total wall-clock time: {int(elapsed // 60)}m {elapsed % 60:.1f}s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
