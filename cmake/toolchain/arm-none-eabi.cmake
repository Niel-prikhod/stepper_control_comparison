# Single generic toolchain for all STM32 boards.
# Board-specific -mcpu/-mfpu/-mfloat-abi and linker script come from
# cmake/boards/<board>.cmake, NOT from here.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TOOLCHAIN_PREFIX arm-none-eabi-)

find_program(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc REQUIRED)
find_program(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++ REQUIRED)
find_program(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc REQUIRED)
find_program(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}objcopy REQUIRED)
find_program(CMAKE_OBJDUMP ${TOOLCHAIN_PREFIX}objdump REQUIRED)
find_program(CMAKE_SIZE ${TOOLCHAIN_PREFIX}size REQUIRED)
find_program(CMAKE_DEBUGGER ${TOOLCHAIN_PREFIX}gdb-multiarch)
# stlink / openocd / pyocd are handled outside the toolchain

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_EXECUTABLE_SUFFIX_ASM ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX ".elf")

# Language standards, no extensions that break HAL builds
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Common warnings + section pruning for all boards
add_compile_options(
  -ffunction-sections
  -fdata-sections
  -fno-common
  -Wall
  -Wextra
  -Wundef
)
add_link_options(
  --specs=nano.specs
  -Wl,--gc-sections
  -Wl,--print-memory-usage
)
