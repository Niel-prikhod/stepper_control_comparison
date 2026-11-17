set(BOARD_NAME "f446re")
set(BOARD_MCU "STM32F446RETx")
set(BOARD_HAL_DEFINE "STM32F446xx")
set(BOARD_FAMILY "F4")
set(BOARD_CUBE_DIR "${CMAKE_SOURCE_DIR}/cubemx/F446RE")
set(BOARD_BSP_DEFINE "USE_NUCLEO_64")

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
add_link_options(${MCU_FLAGS})

if(EXISTS "${BOARD_CUBE_DIR}/STM32F446XX_FLASH.ld")
  set(BOARD_LINKER_SCRIPT "${BOARD_CUBE_DIR}/STM32F446XX_FLASH.ld")
else()
  set(BOARD_LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/ld/cortex-m4_flash.ld")
endif()
add_link_options(-T${BOARD_LINKER_SCRIPT})

set(CUBE_FW_PATH "" CACHE PATH "STM32Cube ${BOARD_FAMILY} firmware repository")
if(NOT CUBE_FW_PATH AND DEFINED ENV{STM32CUBE_FW_F4})
  set(CUBE_FW_PATH "$ENV{STM32CUBE_FW_F4}" CACHE PATH "STM32Cube ${BOARD_FAMILY} firmware repository" FORCE)
endif()
if(NOT EXISTS "${BOARD_CUBE_DIR}/Drivers" AND NOT CUBE_FW_PATH)
  message(FATAL_ERROR
    "HAL sources not found for ${BOARD_NAME}. Set -DCUBE_FW_PATH=<path to STM32Cube_FW_F4_x.y.z> "
    "or export STM32CUBE_FW_F4=<path>.")
endif()

add_compile_definitions(
  ${BOARD_HAL_DEFINE}
  USE_HAL_DRIVER
  STM32F4
)
