# Architecture and hardware porting

This guide describes what is portable today, where board configuration lives,
and what must change when the hardware differs. It is not a claim that every
peripheral or motor is currently configurable.

## Layers

```text
src/app       measurement modes and command protocol
src/drivers   stepper sequencing and AS5600-compatible encoder logic
inc/platform  HAL-free interfaces consumed by app and drivers
src/platform  STM32 HAL/CubeMX adapters; shim implementation for build checks
cubemx/<board> generated pin, clock, and peripheral setup
cmake/boards  MCU flags, linker script, family and board compile definitions
host          serial command, collection, and report tools
```

Application and driver code should depend on public headers under `inc/`, not
on HAL or CubeMX headers. The STM32 adapter translates those interfaces to
generated GPIO, I2C, timer, UART, and clock APIs.

## What is configurable today

| Change | Main place to configure | Important constraint |
| --- | --- | --- |
| Motor coil pins | CubeMX `.ioc` labels `MOTOR_A` … `MOTOR_D`, then regenerate | The current API is four coil outputs, not step/direction. |
| Board/MCU | `cmake/boards/<board>.cmake`, `CMakePresets.json`, top-level board list, CubeMX project | Current adapter has board-specific code for UART, LED, and I2C interrupt ownership. |
| Encoder I2C address | `ENCODER_I2C_ADDRESS` in `inc/drivers/encoder/encoder.h` | The driver still assumes the AS5600 register map and status bits. |
| Motor sequence | `src/drivers/motor/stepper.c` | Current sequence is fixed to four coils; steps/revolution used by tests is separately defined in `inc/app/measure.h`. |
| Test parameters | Host command-line options or firmware mode arguments | Keep parameters and hardware conditions in the collected run metadata. |

For a board with the existing peripheral contract, a port typically needs a
CubeMX project, a board CMake file and preset, family-specific Cube package
selection, and generated labels/peripheral setup. Then build and verify Mode 0
before collecting measurements. Do not edit generated files under
`cubemx/<board>/` by hand; change the `.ioc` and regenerate.

Board examples currently differ:

| Board | Motor outputs | Encoder I2C1 | ST-Link VCP |
| --- | --- | --- | --- |
| G491RE | PA0, PA1, PA4, PB0 | PA15 SCL, PB7 SDA | LPUART1 via Nucleo BSP |
| F446RE | PA0, PA1, PA4, PB0 | PB8 SCL, PB9 SDA | USART2 on PA2/PA3 |

Both use the same named motor GPIO labels, but UART/LED and I2C interrupt
integration are not yet expressed as declarative board settings. See
`cmake/boards/` and `src/platform/stm32/` for the current implementations.

## Platform interface and current boundaries

The central interfaces are in `inc/platform/`: motor/LED/delay, timer, UART,
logging, DWT/CPU, and I2C register reads. They hide MCU-specific names from
portable code, but they do not make every behavior replaceable by changing a
few variables:

- The motor interface is a four-bit, four-coil API. A step/dir driver or a
  different motor topology needs a different driver/platform contract.
- The encoder driver and Mode 5 are AS5600-specific. Mode 5 assumes 4096
  encoder counts and 2048 motor steps per revolution; a different sensor,
  gear ratio, or motor needs driver/application changes and revalidation.
- The STM32 implementation assumes particular peripherals and interrupt
  callbacks. F446RE UART and I2C handler differences currently use
  `PLATFORM_BOARD_F446RE` branches in shared adapter source files.
- The shim only checks that portable code can compile/link. It does not
  validate a board port or peripheral behavior.

Treat these as known architecture gaps, not supported configuration options.
In particular, changing an address or pin label alone does not make a new
encoder or motor type supported.

## Developer verification

```sh
make BOARD=g491re
make BOARD=f446re
```

Builds verify compilation only. On hardware, reset the board, wait for
`READY`, run `mode0`, and inspect its identity, clock, peripheral status, and
timer PSC/ARR diagnostics before longer tests. Hardware test setup and result
interpretation are in [Testing modes](testing-modes.md).

## Documentation status

The local `notes/` directory contains long-form historical notes but is ignored
by Git and is not part of the tracked documentation. Some of it describes an
older demo application or only G491RE. The tracked guides linked from the root
README are the user-facing source of instructions; stale notes should not be
used as current porting requirements.
