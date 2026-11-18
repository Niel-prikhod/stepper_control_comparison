# Host tools

The scripts use `pyserial` declared in their inline `uv` metadata.

## Run one command

```sh
uv run host/measure.py --port /dev/ttyACM0 "mode1 1000 1000"
```

`measure.py` saves every received line to a timestamped log. Use
`--log-dir <dir>` to choose the output directory, `--timeout <seconds>` to
change its timeout, and `--verbose` to print the firmware response. Mode 4
must use this logger: it sends the per-trial `t` triggers after `BEGIN 4`.

## Fixed comparison scenario

```sh
uv run host/run_scenario.py --port /dev/ttyACM0
```

The script identifies the board using Mode 0 and currently validates the
G491RE and F446RE profiles. It runs repeated Mode 1 and Mode 2 tests, raw Mode
2 checks, timer-free and constant Mode 3 checks, and 100-trial Mode 4. It does
**not** run Mode 5. Results and generated logs go to `results/<board>/` and
`logs/`. An optional `--board` argument asserts the expected board identity.

## Collect all modes

```sh
uv run host/collect.py --board f446re --port /dev/ttyACM0
```

This runs Modes 0–5 once with defaults and saves timestamped logs plus a
`manifest.json` under `data/<board>/<UTC>/`. It is a collection run, not the
repeated comparison scenario. Select modes with `--modes 0,1,3`; use
`--keep-going` to continue collecting after a mode fails. `--dry-run` prints
the commands without opening the serial port.

For either suite, connect the board, driver/motor, and encoder; reset the
board; wait for `READY`; then run one collection at a time. Use identical
parameters and record hardware/supply details when comparing runs. See
[`docs/testing-modes.md`](../docs/testing-modes.md) for mode semantics,
parameters, and interpretation.
