cmake_minimum_required(VERSION 3.22)

set(SUPPORTED_BOARDS g491re f446re nucleo-f401re nucleo-g431kb)
if(NOT DEFINED TARGET_BOARD)
  message(FATAL_ERROR "TARGET_BOARD not set. Use a preset or -DTARGET_BOARD=<board>. Supported: ${SUPPORTED_BOARDS}")
endif()
if(NOT TARGET_BOARD IN_LIST SUPPORTED_BOARDS)
  message(FATAL_ERROR "Unknown TARGET_BOARD='${TARGET_BOARD}'. Supported: ${SUPPORTED_BOARDS}")
endif()

project(stepper_control C ASM)

include(${CMAKE_SOURCE_DIR}/cmake/utils/compile_commands.cmake)
enable_compile_commands()

include(${CMAKE_SOURCE_DIR}/cmake/boards/${TARGET_BOARD}.cmake)

add_subdirectory(src/platform)
add_subdirectory(src/drivers/motor)
add_subdirectory(src/drivers/encoder)
add_subdirectory(src/app)
