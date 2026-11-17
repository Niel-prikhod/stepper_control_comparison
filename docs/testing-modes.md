# Testing Modes

The firmware exposes six measurement modes over the ST-Link VCP (LPUART1,
115200 8N1). Commands are ASCII lines terminated by `\n`; `\r` is ignored.
Every started measurement is bracketed by a matching `BEGIN`/`END` pair so a
host can detect failed or timed-out runs.

## Before running

1. Connect the board, motor driver, and AS5600-compatible encoder.
2. Reset the board. It must print `READY` before accepting measurements.
3. Run `mode0` to confirm the board identity, clock, and self-tests.
4. Keep the board still during encoder-based tests and do not send another
   command while a measurement is active. A command sent while busy receives
   `ERR BUSY`.

On startup the firmware self-tests DWT, TIM1, UART, and the encoder. If any
self-test fails it prints `ERR SELFTEST ...` instead of `READY`; measurements
are then rejected.

## Protocol

A successful command produces:

```text
BEGIN <mode>
<key>=<value>
...
END <mode> OK
```

A failed command produces `ERR <reason>` and, for a started measurement,
`END <mode> ERR`. Common errors:

| Error | Meaning |
| --- | --- |
| `ERR PARAM` | Missing, extra, non-numeric, or out-of-range parameter |
| `ERR LIMIT` | Parameter is valid but exceeds a firmware limit |
| `ERR BUSY` | A measurement is already running |
| `ERR UNKNOWN_CMD` | Command name is not recognized |
| `ERR SELFTEST ...` | Startup self-test failed; board is not ready |

## Limits and defaults

| Mode | Command | Defaults | Valid range |
| --- | --- | --- | --- |
| 0 | `mode0` | none | no parameters |
| 1 | `mode1 <target_hz> <duration_ms>` | `1000 1000` | `20..5000` Hz, `1..20000` ms |
| 2 | `mode2 <target_hz> <samples> [raw=1]` | `1000 10000` | `20..5000` Hz, `2..100000` samples; `raw=1` requires `2..100` samples |
| 3 | `mode3 <target_hz> <samples>` | `1000 1000` | `0..5000` Hz, `1..10000` samples; `target_hz=0` means `samples` is duration in ms (`1..20000`) |
| 4 | `mode4 <trials>` | `100` | `1..100` trials |
| 5 | `mode5 <start_hz> <step_hz> <max_hz>` | `100 200 5000` | start `20..max`, max `<=5000`, step `>0`, at most 100 points |

## Mode 0: board and peripheral diagnostics

```sh
uv run host/measure.py --port /dev/ttyACM0 mode0
```

Why: records which board and firmware produced a data set and proves that the
clock, timer, UART, I2C bus, and encoder are usable before longer tests.

Output fields include `board`, `mcu`, `firmware`, `clock_hz`, `dwt`, `timer`,
`uart`, `uart_baud`, `uart_loopback`, `uart_rx_errors`, `i2c`,
`encoder_addr`, `encoder_counts_per_rev`, `steps_per_rev`, and, when the
encoder responds, `encoder_status` and `encoder_raw`; otherwise it reports
`encoder_read=fail`.

Mode 0 also reports the timer configuration for `100`, `333`, `500`, `777`,
`1000`, `1234`, `2000`, `2345`, and `4000` Hz. Each group contains
`timing_hz`, `timing_psc`, `timing_arr`, `timing_period_us`,
`timing_actual_hz`, and `timing_error_ppm`. This exposes the integer
auto-reload quantization that a frequency-only check can hide.

`uart_loopback=software` means the UART transmit path and receiver setup passed
without a physical TX/RX echo; `verified` requires the loopback test line to be
received. Mode 0 still returns `END 0 OK` when only the optional encoder status
read fails, so inspect `encoder_read=fail` in the log.

## Mode 1: step frequency accuracy

```sh
uv run host/measure.py --port /dev/ttyACM0 mode1 1000 1000
```

Why: verifies that the commanded step rate matches the actual TIM1 step rate.
This is the primary check for timer prescaler, auto-reload, and clock-tree
mistakes.

The firmware steps continuously for the requested duration, timestamps the
first and last step with DWT, and computes:

```text
measured_hz = (step_count - 1) * clock_hz / (last_timestamp - first_timestamp)
```

Output fields: `target_hz`, `measured_hz`, `abs_error_hz`, `rel_error_pct`,
`step_count`, `duration_ms`, and `clock_hz`. `duration_ms` is the measured
interval between the first and last step, not the requested duration. A small
negative or positive error is normal; a large error usually indicates a timer
clock or prescaler problem.

## Mode 2: step interval jitter

```sh
uv run host/measure.py --port /dev/ttyACM0 mode2 1000 10000
```

Why: measures consistency of the step interval rather than only the average
rate. It exposes missed timer interrupts, long ISR latency, and unstable
scheduling.

The firmware collects `samples` timer intervals after the first step and
computes mean, sample standard deviation, minimum, maximum, and peak-to-peak
jitter. The raw DWT cycle deltas are kept for the variance calculation, so
`sigma_ns` is not truncated by microsecond conversion. `measured_hz` is derived
from the mean interval:

```text
measured_hz = 1 / mean_interval_us
jitter_pct  = (max_interval_us - min_interval_us) / mean_interval_us * 100
```

Output fields: `target_hz`, `measured_hz`, `samples`, `mean_ns`, `sigma_ns`,
`min_ns`, `max_ns`, `p2p_ns`, `mean_interval_us`, `stddev_us`,
`min_interval_us`, `max_interval_us`, `p2p_jitter_us`, `jitter_pct`, and
`clock_hz`.

With `raw=1`, the firmware also prints `raw_count` followed by that many raw
DWT cycle deltas, one integer per line, before `END`. The raw dump is limited
to `APP_MAX_RAW_SAMPLES` (100) values so it fits in the static buffer. Use it
to recompute the sample standard deviation offline and compare it with
`sigma_ns`.

## Mode 3: timer ISR load

```sh
uv run host/measure.py --port /dev/ttyACM0 mode3 1000 1000
```

Why: quantifies how much CPU time the step timer interrupt consumes. It is
useful before adding work to the ISR or increasing the step rate.

When `target_hz` is non-zero, the firmware runs the same number of timer
interrupts in three conditions:

| Condition | Behavior |
| --- | --- |
| `idle` | Timer ISR runs, but the motor is not stepped |
| `constant` | Motor steps every interrupt at `target_hz` |
| `ramp` | Motor steps and frequency ramps from `target_hz / 10` to `target_hz` over the first half, then back down |

For each condition it reports `condition`, `load_pct`, `peak_load_pct`,
`isr_count`, `isr_cycles`, `period_cycles`, `peak_isr_cycles`, and
`peak_isr_us`.

With `target_hz=0`, no step timer interrupt runs. The firmware services the
main loop for `samples` milliseconds and reports a timer-free `condition=idle`
with `load_pct`, `peak_load_pct`, `idle_cycles`, `busy_cycles`,
`total_cycles`, `iterations`, and `main_loop_mean_us`. The load is computed
from the measured main-loop service time:

```text
load_pct = 100 * (1 - idle_cycles / total_cycles)
```

For non-zero `target_hz`, `load_pct` is the sum of measured ISR execution
cycles divided by the sum of measured timer periods:

```text
load_pct = sum(isr_cycles) / sum(period_cycles) * 100
```

In that mode, the `idle` condition still includes the timer ISR, so it measures
interrupt overhead without motor GPIO writes. The ramp condition changes the
auto-reload value from inside the ISR; compare it with `constant` to see the
cost of changing the timer period.

## Mode 4: command-to-first-step latency

```sh
uv run host/measure.py --port /dev/ttyACM0 mode4 100
```

Why: estimates response time from command processing to the first motor step.
It is useful for evaluating the control-loop and UART handling overhead.

Each trial starts a 100 Hz timer and records the time from the start of command
processing to the first timer interrupt. Trials are separated by 50 ms so the
next command cannot overlap the previous step. All latency samples are kept in
a static buffer, so the firmware can report `p99_us` in addition to mean,
standard deviation, minimum, and maximum.
Output fields: `num_trials`, `baud`, `cmd_len`, `latency_ref=cmd_process`,
`uart_rx_to_first_step_us`, `mean_us`, `sigma_us`, `stddev_us`, `min_us`,
`max_us`, `p99_us`, and `clock_hz`.

`mean_us` and the min/max values use the command-processing reference.
`uart_rx_to_first_step_us` is measured only for the first trial and uses the
UART line timestamp; it is not included in the statistics. This distinction is
important when comparing logs: `latency_ref=cmd_process` is the current
firmware behavior, not a full per-trial UART-to-step measurement.

## Mode 5: maximum step rate with encoder feedback

```sh
uv run host/measure.py --port /dev/ttyACM0 mode5 100 200 5000
```

Why: finds the highest commanded step rate that still produces the expected
mechanical rotation. It detects missed steps caused by torque loss, resonance,
driver limits, or supply sag.

The firmware sweeps `start_hz` to `max_hz` in `step_hz` increments. At each
frequency it:

1. Reads the AS5600 raw angle.
2. Commands 200 steps with a 50-step ramp from `start_hz` to the test
   frequency.
3. Waits 5 ms for the rotor to settle.
4. Reads the raw angle again and computes the circular delta.
5. Converts the encoder delta to motor steps using
   `steps = (abs(delta) + 1) / 2`, because the 28BYJ-48 has 2048 steps/rev and
   the encoder has 4096 counts/rev.
6. Marks the point as a loss when `abs(measured_steps - 200) > 2`.

Per-point output:

```text
point=<n> freq_hz=<hz> expected_steps=200 measured_steps=<n> raw_before=<n> raw_after=<n> loss=<0|1>
```

Summary output: `points`, `max_stable_hz`, `first_loss_hz`, and
`failing_points`. `max_stable_hz` is the highest individual point that stayed
within tolerance; it is not necessarily the highest contiguous stable range.
Inspect `first_loss_hz` and `failing_points` together, especially when the
measured steps are non-monotonic due to inertia or backlash.

## Full validation scenario

`host/run_scenario.py` runs the fixed G491RE validation scenario and writes
`results/<board>/table.md`, `summary.csv`, `raw_index.md`, and
`run_data.json`:

```sh
python3 host/run_scenario.py --port /dev/ttyACM0
```

The scenario runs `mode0` first and stops immediately if the board identity,
clock, DWT, timer, UART, I2C, encoder, or timer PSC/ARR diagnostics fail. It
then runs nine Mode 1 frequencies (5 runs each), four Mode 2 frequencies
(3 runs each), four raw Mode 2 dumps, the timer-free and constant Mode 3
conditions, and `mode4 100`. Each command is retried once; a run that fails
twice is marked incomplete and recorded as a warning. The raw index contains
only files generated by that scenario run.

## Collecting all modes for one board

```sh
uv run host/collect.py --board g491re --port /dev/ttyACM0
```

This runs mode0 through mode5 with the defaults above and writes one log per
mode plus `manifest.json` under `data/<board>/<UTC>/`. Use:

```sh
uv run host/collect.py --board g491re --port /dev/ttyACM0 \
  --mode1-hz 500 --mode1-duration-ms 2000 \
  --mode2-samples 20000 \
  --mode5-start-hz 100 --mode5-step-hz 100 --mode5-max-hz 3000
```

Other useful options are `--modes 0,1,3`, `--keep-going`, `--out-dir`,
`--timeout`, `--verbose`, and `--dry-run`. Reset the board before a run so it
is idle at `READY`.

## Recommended test sequence

1. `mode0` on every board and keep the log with the board label.
2. `mode1` at a low, medium, and high rate (for example 100, 1000, 5000 Hz).
3. `mode2` at the same rates with enough samples to expose jitter.
4. `mode3` at the intended operating rate.
5. `mode4` with at least 100 trials.
6. `mode5` across the intended operating range; repeat if the result is
   non-monotonic.

Use the same parameters for every board so results are comparable. Record the
board label, firmware version, encoder address, supply voltage, motor driver,
and test fixture in the run notes.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| No `READY` | Reset the board, verify the VCP port and 115200 baud, inspect `ERR SELFTEST` |
| `ERR BUSY` | Wait for the current measurement to finish or reset the board |
| `error=timer` | Timer self-test or TIM1 initialization failed |
| `error=timeout` | Motor/encoder did not make progress; check wiring and encoder address |
| `error=i2c phase=before/after` | AS5600 read failed; check SDA/SCL, pull-ups, address, and power |
| `encoder_read=fail` in mode0 | Encoder status read failed, but the board can still run other modes |
| Mode 5 all points fail | Verify encoder direction/coupling, steps per revolution, and motor current |
| Mode 5 results jump around | Increase settle time, reduce acceleration, or inspect mechanical backlash |
