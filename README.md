# stepper_control

Multi-board STM32 stepper motor control with AS5600-compatible I2C encoder
feedback and VCP UART logging. CubeMX-generated HAL per board, portable motor
and encoder drivers, and a HAL-free platform abstraction.

## Layout

```
inc/
  drivers/motor/   public motor driver API
  drivers/encoder/ public encoder driver API
  platform/        HAL-free interfaces (stepper, i2c, log, dwt, timer, uart)
  app/             app-internal API
src/
  drivers/motor/   motor driver impl + private headers
  drivers/encoder/ AS5600-compatible encoder impl
  platform/stm32/  HAL adapters (only place including CubeMX headers)
  platform/shim/   startup/system stub for boards without CubeMX output
  app/             main.c + command parser + measurements
host/              PC serial logger
cubemx/<BOARD>/    CubeMX-owned output (never hand-edit)
cmake/
  toolchain/       generic arm-none-eabi toolchain
  boards/          per-board MCU flags, defines, linker, cube dir
  utils/
  ld/              fallback linker script
```

Dependency direction (single `inc` root; layering is a convention, private
headers stay out of it):

```
app -> drivers -> platform_if
app -> platform_stm32 -> cube_hal -> cubemx/<BOARD> + HAL
```

## Documentation

Long-form notes covering the build, CubeMX/HAL, platform layer, board details,
the 28BYJ-48/ULN2003A wiring, flashing/debugging, and extension steps are in
`notes/README.md`.

Mode-by-mode commands, parameters, outputs, and interpretation are documented
in [`docs/testing-modes.md`](docs/testing-modes.md). Public functions are
documented with Doxygen comments in their header declarations under `inc/`;
private helpers and interrupt callbacks are documented in their source files.

## Prerequisites

- `arm-none-eabi-gcc`, `cmake >= 3.22`, `ninja`
- STM32Cube firmware package for the board family
- `st-flash` (or `openocd`) for flashing

## HAL dependency

HAL/CMSIS live in the STM32Cube repository, not in this repo.
Point CMake at it:

```sh
export STM32CUBE_FW_G4=$HOME/STM32Cube/Repository/STM32Cube_FW_G4_V1.6.3
```

or pass `-DCUBE_FW_PATH=/path/to/STM32Cube_FW_G4_x.y.z`.

## Build and flash

```sh
make            # configure + build (BOARD=g491re by default)
make flash      # build, then flash with st-flash (falls back to openocd)
make reset      # reset the board
make erase      # mass erase
make size       # show firmware size
```

Useful overrides: `make BOARD=nucleo-f401re`, `make FLASH_TOOL=openocd`,
`make CUBE_FW_PATH=/path/to/STM32Cube_FW_G4_x.y.z`. `CUBE_FW_PATH` is
auto-detected from `~/STM32Cube/Repository/STM32Cube_FW_G4_*` when possible.

Equivalent CMake commands:

```sh
cmake --preset g491re
cmake --build build/g491re
```

Artifacts: `build/g491re/src/app/firmware.{elf,hex,bin}`.

## Measurement application

The default firmware expects an AS5600-compatible encoder on I2C1
(`PA15` SCL, `PB7` SDA, address `0x36`) and accepts ASCII commands on the
ST-Link VCP (LPUART1 `PA2`/`PA3`, 115200 8N1).

On power-up the firmware self-tests DWT, TIM1, UART, and the encoder. It then
emits `READY` and waits for commands. Supported commands:

```text
mode0
mode1 <target_hz> <duration_ms>
mode2 <target_hz> <num_samples>
mode3 <target_hz> <num_samples>
mode4 <num_trials>
mode5 <start_hz> <step_hz> <max_hz>
```

Responses use `BEGIN <mode>`, key=value result lines, and `END <mode> OK|ERR`.
Measurements use DWT cycle timing. Modes 1 and 2 step the motor at the commanded
rate; mode 3 compares idle, constant, and ramped load; mode 4 measures
command-to-first-step latency. Mode 5 compares AS5600 angle change with the
commanded step count.

`AS5600L` uses address `0x40`; change `ENCODER_I2C_ADDRESS` or call
`encoder_set_address()`.

## Host scripts

The host scripts declare their `pyserial` dependency inline; `uv` creates an
isolated environment automatically.

```sh
uv run host/measure.py --port /dev/ttyACM0 "mode1 1000 1000"
```

`measure.py` writes each received line with an ISO-8601 UTC prefix to
`log_mode<N>_<UTC>.txt` and exits non-zero on timeout, firmware error, or a
malformed `BEGIN`/`END` response.

To collect mode0 through mode5 for one board in a single run:

```sh
uv run host/collect.py --board g491re --port /dev/ttyACM0
```

This writes logs and `manifest.json` under `data/<board>/<UTC>/`. See
`host/README.md` for parameter overrides.

Other boards: `cmake --preset nucleo-f401re`, `nucleo-g431kb` (shim build until
CubeMX output exists in `cubemx/<board>/`).

## compile_commands.json

`enable_compile_commands()` in `cmake/utils/compile_commands.cmake` is called
from the top-level `CMakeLists.txt`. It creates a symlink
`compile_commands.json` in the repo root pointing at the configured preset's
database. Existing symlinks are relinked; an existing regular file is left
untouched; failures only warn. The symlink is gitignored. With several presets
it points at the last configured one, so re-run `cmake --preset <board>` before
starting clangd.

## CubeMX workflow

1. Edit `cubemx/<board>/<board>.ioc` in CubeMX.
2. Keep `ProjectManager.NoMain=true`: CubeMX generates `SystemClock_Config()`
   and `Error_Handler()` but no `main()`; `src/app/main.c` owns the entry point.
3. Generate code in place under `cubemx/<board>/`. Do not move it.
4. Do not use the `CMakeLists.txt` / `CMakePresets.json` CubeMX emits; this
   project builds the same sources through `src/platform/CMakeLists.txt`.

## Adding a board

1. Generate CubeMX output into `cubemx/<board>/`.
2. Copy `cmake/boards/g491re.cmake` to `cmake/boards/<board>.cmake` and adjust
   MCU, family, HAL define, linker script, `CUBE_FW_PATH` env name.
3. Add `<board>` to `SUPPORTED_BOARDS` in `CMakeLists.txt`.
4. Add a preset in `CMakePresets.json`.
5. Map pins in `src/platform/stm32/stm32_platform_stepper.c`.

## Future extraction

Reusable code is namespaced and self-contained under `inc/` + `src/`. If a
second consumer appears, `git subtree split -P src/drivers -P inc/drivers`
(or moving a component to its own repo) requires no source changes thanks to
namespaced includes.
