# stepper_control

STM32 firmware and host tools for evaluating stepper control and encoder
measurements on supported boards. Firmware is split into an application,
drivers, platform interfaces, and STM32/CubeMX adapters. The current hardware
examples are NUCLEO-G491RE and NUCLEO-F446RE.

## Start here

Install CMake, Ninja, `arm-none-eabi-gcc`, and the matching STM32Cube package.
The Makefile finds CubeG4 for `g491re` and CubeF4 for `f446re` under
`~/STM32Cube/Repository/`; set `CUBE_FW_PATH` if it is elsewhere.

```sh
make                         # build the default g491re target
make BOARD=f446re            # build the F446RE target
make BOARD=f446re flash      # build and flash, if a supported flash tool is installed
```

The F401RE and G431KB presets currently provide shim builds, not working
hardware ports. A successful shim build is not hardware validation.

## Documentation by task

- **Add hardware or understand the layers:** [Architecture and hardware
  porting](docs/architecture.md)
- **Run or interpret measurements:** [Testing modes](docs/testing-modes.md)
- **Use the host logger and collectors:** [Host tools](host/README.md)
- **Understand generated CubeMX files:** [CubeMX integration](cubemx/README.md)

## Current measurement application

After startup self-tests pass, firmware prints `READY` and accepts `mode0`
through `mode5` over the ST-Link VCP at 115200 baud. Mode 0 checks board and
peripheral setup; Modes 1–4 measure step timing, jitter, CPU load, and
UART-trigger-to-step latency; Mode 5 compares commanded motion with encoder
motion. See the mode guide for requirements and limitations.

The default test hardware is a 28BYJ-48 through a ULN2003A and an
AS5600-compatible encoder. Pin assignments, encoder assumptions, and the
current limitations when changing motor or encoder types are documented in
[Architecture and hardware porting](docs/architecture.md).
