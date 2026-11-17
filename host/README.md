# Measurement Logger

Install the serial dependency:

```sh
python3 -m pip install -r host/requirements.txt
```

Run a command and write a timestamped log:

```sh
python3 host/measure.py /dev/ttyACM0 "mode1 1000 1000"
```

If the command is omitted, the script prompts for it:

```sh
python3 host/measure.py /dev/ttyACM0 --baud 115200
```

Each received line is written with an ISO-8601 UTC prefix. Logs are named
`log_mode<N>_<UTC>.txt` in the current directory unless `--log-dir` is used.
The script exits non-zero for a timeout, a firmware error, or a malformed
`BEGIN`/`END` response. Use `--timeout` to change the default 60 second limit
and `--verbose` to echo received lines to stdout.
