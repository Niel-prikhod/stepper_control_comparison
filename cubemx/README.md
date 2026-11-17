# CubeMX projects

One CubeMX project per board, kept under `cubemx/<BOARD>/`:

- `cubemx/G491RE/G491RE.ioc` -> `cubemx/G491RE/`
- add future boards as `cubemx/<BOARD>/`

CubeMX owns everything in these folders. Do not move the project, do not edit
generated files by hand, and do not use the `CMakeLists.txt`,
`CMakePresets.json`, or `cmake/` that CubeMX emits. This repo builds the same
sources through `src/platform/CMakeLists.txt` and the board files in
`cmake/boards/`.

CubeMX may generate either `Core/Inc` + `Core/Src` or `Inc` + `Src`; the
platform CMake detects both. If the board BSP is enabled (e.g. Nucleo LEDs,
button, VCP), set `BOARD_BSP_DEFINE` in `cmake/boards/<board>.cmake` to the
matching CubeMX symbol (`USE_NUCLEO_64` for Nucleo-64).

## Platform HAL expectations

`src/platform/stm32/` is the only code that consumes the generated project. It
expects the CubeMX output and BSP configuration to provide the following.

### Generated API and symbols

| Expectation | Used for |
| --- | --- |
| `main.h` on the include path | HAL/BSP headers and board pin labels |
| `SystemClock_Config()` | Clock tree setup |
| `MX_GPIO_Init()` | Motor and unused-pin GPIO setup |
| `MOTOR_A_GPIO_Port` / `MOTOR_A_Pin` | Motor coil A |
| `MOTOR_B_GPIO_Port` / `MOTOR_B_Pin` | Motor coil B |
| `MOTOR_C_GPIO_Port` / `MOTOR_C_Pin` | Motor coil C |
| `MOTOR_D_GPIO_Port` / `MOTOR_D_Pin` | Motor coil D |
| `LED2`, `BSP_LED_Init()`, `BSP_LED_On()`, `BSP_LED_Off()` | Status LED |
| `MX_DMA_Init()` | DMA1/DMAMUX clocks and DMA1_Channel1 IRQ |
| `MX_I2C1_Init()` | I2C1 handle and timing |
| `hi2c1` | I2C1 handle |
| `hdma_i2c1_rx` | I2C1 RX DMA handle |
| `I2C1_EV_IRQn`, `I2C1_ER_IRQn` | I2C event/error NVIC lines |
| `BSP_COM_Init()`, `BSP_COM_SelectLogPort()` | VCP LPUART1 setup |
| `LPUART1_IRQn` | VCP receive interrupt line |
| `MX_TIM1_Init()` | 1 MHz step timer setup |
| `htim1`, `TIM1_UP_TIM16_IRQn` | TIM1 handle and update interrupt |
| `HAL_RCC_GetHCLKFreq()` | DWT cycle-to-time conversion |
| `HAL_RCC_GetPCLK2Freq()` | TIM1 prescaler calculation |
| `CoreDebug`, `DWT` | Cycle counter timing |
| `COM1`, `COM_InitTypeDef`, `hcom_uart[COM1]` | BSP COM logging handle |
| `HAL_GetTick()` | I2C timeout and HAL delay |

`DMA1_Channel1_IRQHandler()` must remain in the generated interrupt file and
call `HAL_DMA_IRQHandler(&hdma_i2c1_rx)`.

### Peripheral configuration

- **I2C1**: enabled, 7-bit addressing, analog filter enabled, no clock-stretch
  disable. The current generated timing is `0x40B285C2` for the 170 MHz clock
  tree. Pins are `PA15` = SCL and `PB7` = SDA, alternate function 4,
  open-drain. External or encoder-board pull-ups are required.
- **I2C1 RX DMA**: `DMA1_Channel1`, `DMA_REQUEST_I2C1_RX`,
  peripheral-to-memory, byte/byte, memory increment, normal mode.
- **I2C1 interrupts**: the platform adapter enables `I2C1_EV_IRQn` and
  `I2C1_ER_IRQn` and defines `I2C1_EV_IRQHandler()` /
  `I2C1_ER_IRQHandler()`. Do not enable CubeMX generation of duplicate I2C
  handlers unless you remove the adapter versions.
- **I2C recovery**: after a timeout the adapter calls
  `HAL_I2C_DeInit(&hi2c1)` and then `MX_I2C1_Init()`. The generated init path
  must be safe to call again.
- **BSP COM**: `USE_BSP_COM_FEATURE=1`, `USE_COM_LOG=1`, `COM1` on LPUART1
  `PA2`/`PA3`, 115200 8N1. The platform adapter starts one-byte RX
  interrupts and owns `LPUART1_IRQHandler()`.
- **TIM1**: enabled as a 1 MHz up-counter with update interrupt, ARR up to
  65535. The platform adapter overwrites the prescaler from PCLK2 and disables
  ARR preload so step-rate changes take effect immediately.
- **DWT**: `CoreDebug->DEMCR` trace enable and `DWT->CYCCNT` are configured by
  the platform adapter. No CubeMX setting is required. Firmware faults if the
  cycle counter cannot be enabled.
- **Motor GPIO**: `PA0`, `PA1`, `PA4`, `PB0` as push-pull outputs.
- **Clock**: 170 MHz from HSI/PLL. If the clock tree changes, regenerate the
  I2C timing value.

### Interrupt ownership

The platform adapter owns these strong symbols:

- `I2C1_EV_IRQHandler()`
- `I2C1_ER_IRQHandler()`
- `HAL_I2C_MemRxCpltCallback()`
- `HAL_I2C_ErrorCallback()`
- `LPUART1_IRQHandler()`
- `HAL_UART_RxCpltCallback()`
- `HAL_UART_ErrorCallback()`
- `HAL_TIM_PeriodElapsedCallback()`

Do not define them in application code or another driver. If CubeMX regenerates
them into `stm32g4xx_it.c`, remove the duplicate definitions or move the
platform implementation into the generated file. Duplicate strong definitions
fail the link.

The adapter sets TIM1 update priority `0`, DMA1_Channel1 priority `0`, I2C1
event/error priority `1`, LPUART1 priority `2`, and assumes SysTick priority
`0`. The I2C timeout relies on a running 1 ms `HAL_GetTick()`.

`TIM1_UP_TIM16_IRQHandler()` must remain in the generated interrupt file and
call `HAL_TIM_IRQHandler(&htim1)`. The platform adapter supplies the
`HAL_TIM_PeriodElapsedCallback()` implementation.

### Initialization order

The adapter requires:

1. `HAL_Init()`
2. `SystemClock_Config()`
3. `MX_GPIO_Init()`
4. `BSP_LED_Init(LED2)`
5. `platform_dwt_init()`
6. `MX_DMA_Init()`
7. `MX_I2C1_Init()`
8. Enable `I2C1_EV_IRQn` and `I2C1_ER_IRQn`
9. `BSP_COM_Init(COM1, ...)` and `BSP_COM_SelectLogPort(COM1)`
10. Enable `LPUART1_IRQn` and arm `HAL_UART_Receive_IT()`
11. `MX_TIM1_Init()`, set the 1 MHz prescaler, disable ARR preload

`MX_DMA_Init()` must run before `MX_I2C1_Init()` because the I2C MSP init
configures the DMA handle.

### Board-porting expectations

The adapter selects the HAL path with `__has_include("main.h")`. That detects
HAL presence, not peripheral capability. A HAL-backed board without I2C1, DMA,
BSP COM, or the expected motor pin labels will compile the HAL path and fail at
compile or link time. Such a board needs one of:

- stubs for unsupported platform functions,
- compile-time capability macros, or
- a board-specific platform adapter selected by CMake.

USART1, the user button, and SWO are not required by the current platform
adapter and may remain generated but inactive. LPUART1, TIM1, and DWT are
required by the measurement application.

## Recommended CubeMX settings

- Toolchain: CMake
- `ProjectManager.NoMain=true` (app/main.c owns `main()`)
- Keep user code enabled
- Either:
  - "Copy only the necessary library files" -> HAL lands in
    `cubemx/<BOARD>/Drivers/` and the repo is self-contained, or
  - "Do not copy library files" -> HAL is read from the STM32Cube repository
    via `CUBE_FW_PATH` / `STM32CUBE_FW_G4` (see top-level README).

> [!WARNING]
> Never edit files under `cubemx/<BOARD>/` by hand! 
> Change the `.ioc` instead.
