# Board: Nucleo-G431KB (STM32G431KBT6, Cortex-M4F)
set(BOARD_NAME "nucleo-g431kb")
set(BOARD_MCU "STM32G431KBT6")
set(BOARD_HAL_DEFINE "STM32G431xx")
set(BOARD_FAMILY "G4")
set(BOARD_CUBE_DIR "${CMAKE_SOURCE_DIR}/cubemx/nucleo-g431kb")

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

if(EXISTS "${BOARD_CUBE_DIR}/STM32G431KBTX_FLASH.ld")
  set(BOARD_LINKER_SCRIPT "${BOARD_CUBE_DIR}/STM32G431KBTX_FLASH.ld")
else()
  set(BOARD_LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/ld/cortex-m4_flash.ld")
endif()
add_link_options(-T${BOARD_LINKER_SCRIPT})

add_compile_definitions(
  ${BOARD_HAL_DEFINE}
  USE_HAL_DRIVER
  STM32G4
)
