# stepper_control

Multi-board STM32 stepper motor control. CubeMX-generated HAL per board, a
portable motor driver, and a HAL-free platform abstraction.

## Layout

```
inc/
  drivers/motor/   public driver API
  platform/        HAL-free interfaces
  app/             app-internal API
src/
  drivers/motor/   driver impl + private headers
  platform/stm32/  HAL adapter (only place including CubeMX headers)
  platform/shim/   startup/system stub for boards without CubeMX output
  app/             main.c + control logic
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

## Prerequisites

- `arm-none-eabi-gcc`, `cmake >= 3.22`, `ninja`
- STM32Cube firmware package for the board family

## HAL dependency

HAL/CMSIS live in the STM32Cube repository, not in this repo. 
Point CMake at it:

```sh
export STM32CUBE_FW_G4=$HOME/STM32Cube/Repository/STM32Cube_FW_G4_V1.6.3
```

or pass `-DCUBE_FW_PATH=/path/to/STM32Cube_FW_G4_x.y.z`.

## Build

```sh
cmake --preset g491re
cmake --build build/g491re
```

Artifacts: `build/g491re/src/app/firmware.{elf,hex,bin}`.

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
