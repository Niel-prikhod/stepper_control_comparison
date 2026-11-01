# Board: Nucleo-F401RE (STM32F401RET6, Cortex-M4F)
# Included by top-level CMakeLists.txt AFTER project() via TARGET_BOARD dispatch,
# but MCU_FLAGS must be applied via add_compile_options/add_link_options here.
set(BOARD_NAME "nucleo-f401re")
set(BOARD_MCU "STM32F401RET6")
set(BOARD_HAL_DEFINE "STM32F401xE")
set(BOARD_FAMILY "F4")
set(BOARD_CUBE_DIR "${CMAKE_SOURCE_DIR}/cubemx/nucleo-f401re")

set(MCU_CPU "cortex-m4")
set(MCU_FPU "fpv4-sp-d16")
set(MCU_FLOAT_ABI "hard")

set(MCU_FLAGS
  -mcpu=${MCU_CPU}
  -mthumb
  -mfpu=${MCU_FPU}
  -mfloat-abi=${MCU_FLOAT_ABI}
)
add_compile_options(${MCU_FLAGS})
add_link_options(${MCU_FLAGS} -mthumb)

# CubeMX will generate STM32F401RETX_FLASH.ld inside CUBE dir.
# Until then use the skeleton linker script.
if(EXISTS "${BOARD_CUBE_DIR}/STM32F401RETX_FLASH.ld")
  set(BOARD_LINKER_SCRIPT "${BOARD_CUBE_DIR}/STM32F401RETX_FLASH.ld")
else()
  set(BOARD_LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/ld/cortex-m4_flash.ld")
endif()
add_link_options(-T${BOARD_LINKER_SCRIPT})

add_compile_definitions(
  ${BOARD_HAL_DEFINE}
  USE_HAL_DRIVER
  STM32F4
)
