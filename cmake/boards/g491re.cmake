set(BOARD_NAME "g491re")
set(BOARD_MCU "STM32G491RETx")
set(BOARD_HAL_DEFINE "STM32G491xx")
set(BOARD_FAMILY "G4")
set(BOARD_CUBE_DIR "${CMAKE_SOURCE_DIR}/cubemx/G491RE")

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

if(EXISTS "${BOARD_CUBE_DIR}/STM32G491XX_FLASH.ld")
  set(BOARD_LINKER_SCRIPT "${BOARD_CUBE_DIR}/STM32G491XX_FLASH.ld")
else()
  set(BOARD_LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/ld/cortex-m4_flash.ld")
endif()
add_link_options(-T${BOARD_LINKER_SCRIPT})

set(CUBE_FW_PATH "" CACHE PATH "STM32Cube ${BOARD_FAMILY} firmware repository")
if(NOT CUBE_FW_PATH AND DEFINED ENV{STM32CUBE_FW_G4})
  set(CUBE_FW_PATH "$ENV{STM32CUBE_FW_G4}" CACHE PATH "STM32Cube ${BOARD_FAMILY} firmware repository" FORCE)
endif()
if(NOT EXISTS "${BOARD_CUBE_DIR}/Drivers" AND NOT CUBE_FW_PATH)
  message(FATAL_ERROR
    "HAL sources not found for ${BOARD_NAME}. Set -DCUBE_FW_PATH=<path to STM32Cube_FW_G4_x.y.z> "
    "or export STM32CUBE_FW_G4=<path>.")
endif()

add_compile_definitions(
  ${BOARD_HAL_DEFINE}
  USE_HAL_DRIVER
  STM32G4
)
