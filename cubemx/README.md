# CubeMX projects

Each hardware board has a CubeMX project under `cubemx/<BOARD>/`. Edit its
`.ioc` file and regenerate; do not hand-edit generated files. The repository
build uses its own CMake files (`CMakePresets.json` and `cmake/boards/`), not
the CMake files emitted by CubeMX. `ProjectManager.NoMain=true` is required:
`src/app/main.c` owns `main()`.

The platform adapter expects the generated `main.h`, clock and peripheral
initializers, and the named motor pins `MOTOR_A` through `MOTOR_D`. Preserve
those labels when assigning pins. Peripheral setup and interrupt ownership
must match the board implementation in `src/platform/stm32/`; do not add
duplicate strong IRQ or HAL callback definitions during regeneration.

## Current board wiring

| Board | Motor labels | Encoder I2C1 | ST-Link VCP |
| --- | --- | --- | --- |
| G491RE | PA0, PA1, PA4, PB0 | PA15 SCL, PB7 SDA | LPUART1 via Nucleo BSP |
| F446RE | PA0, PA1, PA4, PB0 | PB8 SCL, PB9 SDA | USART2 on PA2/PA3 |

Both use TIM1 as the step timer and the DWT cycle counter for timing. The
platform adapter configures the step timer for a 1 MHz counter and derives its
prescaler from the board clock. I2C timing, UART setup, LED handling, and
interrupt wiring differ by MCU/board; the G491RE `.ioc` settings are not a
drop-in template for F446RE.

## Adding a board

1. Create and configure the CubeMX project with the required clocks, GPIO,
   TIM1, I2C, and VCP UART resources. Use the existing motor labels.
2. Add a CMake board file and preset, including the correct STM32Cube family,
   device defines, startup/linker configuration, and compile definitions.
3. Implement or adapt the STM32 platform functions for that board's UART,
   I2C, LED, timer, and interrupt conventions. The current adapter has
   explicit G491RE/F446RE differences; a new HAL project does not become
   supported merely because `main.h` exists.
4. Build, flash, wait for `READY`, and validate Mode 0 and the hardware tests.
   A shim build checks portable-code compilation only.

Choose either CubeMX's option to copy the required HAL files into the project,
or use the installed STM32Cube package via `CUBE_FW_PATH` (see the root
README). Keep generated sources synchronized with the `.ioc` and do not
commit unrelated CubeMX-generated CMake files.
