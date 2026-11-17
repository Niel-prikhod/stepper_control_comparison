add_library(platform_if INTERFACE)
target_include_directories(platform_if INTERFACE ${PROJECT_SOURCE_DIR}/inc)

add_library(platform_stm32 STATIC
  stm32/stm32_platform_stepper.c
  stm32/stm32_platform_dwt.c
  stm32/stm32_platform_i2c.c
  stm32/stm32_platform_log.c
  stm32/stm32_platform_timer.c
  stm32/stm32_platform_uart.c
)
target_link_libraries(platform_stm32 PUBLIC platform_if)

if(EXISTS "${BOARD_CUBE_DIR}/Core/Src")
  set(CUBE_SRC_DIR "${BOARD_CUBE_DIR}/Core/Src")
  set(CUBE_INC_DIR "${BOARD_CUBE_DIR}/Core/Inc")
elseif(EXISTS "${BOARD_CUBE_DIR}/Src")
  set(CUBE_SRC_DIR "${BOARD_CUBE_DIR}/Src")
  set(CUBE_INC_DIR "${BOARD_CUBE_DIR}/Inc")
else()
  set(CUBE_SRC_DIR "")
  set(CUBE_INC_DIR "")
endif()

if(CUBE_SRC_DIR)
  if(EXISTS "${CUBE_SRC_DIR}/main.c")
    file(READ "${CUBE_SRC_DIR}/main.c" _cube_main_src)
    if(_cube_main_src MATCHES "int[ \t\r\n]+main[ \t\r\n]*\\(")
      message(FATAL_ERROR
        "CubeMX main.c in ${BOARD_CUBE_DIR} defines main(). Set ProjectManager.NoMain=true "
        "so src/app/main.c owns the entry point.")
    endif()
  endif()

  file(GLOB CUBE_CORE_SRCS "${CUBE_SRC_DIR}/*.c")
  file(GLOB CUBE_STARTUP_SRCS "${BOARD_CUBE_DIR}/startup_*.s")

  if(EXISTS "${BOARD_CUBE_DIR}/Drivers")
    set(HAL_ROOT "${BOARD_CUBE_DIR}")
  else()
    if(NOT CUBE_FW_PATH)
      message(FATAL_ERROR
        "CUBE_FW_PATH is not set. Export STM32CUBE_FW_G4 or pass -DCUBE_FW_PATH=<repo>.")
    endif()
    set(HAL_ROOT "${CUBE_FW_PATH}")
  endif()

  string(TOLOWER "${BOARD_FAMILY}" _family_lower)
  set(_hal_prefix "stm32${_family_lower}xx_hal_")
  set(_hal_conf "${CUBE_INC_DIR}/stm32${_family_lower}xx_hal_conf.h")

  if(NOT EXISTS "${_hal_conf}")
    message(FATAL_ERROR "Missing HAL config header: ${_hal_conf}")
  endif()
  file(READ "${_hal_conf}" _hal_conf_src)

  file(GLOB _hal_all "${HAL_ROOT}/Drivers/STM32${BOARD_FAMILY}xx_HAL_Driver/Src/${_hal_prefix}*.c")
  set(CUBE_HAL_SRCS)
  set(_hal_base "${HAL_ROOT}/Drivers/STM32${BOARD_FAMILY}xx_HAL_Driver/Src/stm32${_family_lower}xx_hal.c")
  if(EXISTS "${_hal_base}")
    list(APPEND CUBE_HAL_SRCS "${_hal_base}")
  endif()
  foreach(_src ${_hal_all})
    get_filename_component(_name "${_src}" NAME_WE)
    if(_name MATCHES "_template$")
      continue()
    endif()
    string(REGEX REPLACE "^${_hal_prefix}" "" _mod "${_name}")
    string(REGEX REPLACE "_(ex|ramfunc)$" "" _mod "${_mod}")
    string(TOUPPER "${_mod}" _MOD)
    if(_hal_conf_src MATCHES "HAL_${_MOD}_MODULE_ENABLED")
      list(APPEND CUBE_HAL_SRCS "${_src}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES CUBE_HAL_SRCS)

  add_library(cube_hal STATIC ${CUBE_HAL_SRCS})
  target_include_directories(cube_hal PUBLIC
    "${CUBE_INC_DIR}"
    "${HAL_ROOT}/Drivers/STM32${BOARD_FAMILY}xx_HAL_Driver/Inc"
    "${HAL_ROOT}/Drivers/STM32${BOARD_FAMILY}xx_HAL_Driver/Inc/Legacy"
    "${HAL_ROOT}/Drivers/CMSIS/Device/ST/STM32${BOARD_FAMILY}xx/Include"
    "${HAL_ROOT}/Drivers/CMSIS/Include"
  )

  set(_bsp_dir "${HAL_ROOT}/Drivers/BSP/STM32${BOARD_FAMILY}xx_Nucleo")
  if(EXISTS "${CUBE_INC_DIR}/stm32${_family_lower}xx_nucleo_conf.h" AND EXISTS "${_bsp_dir}")
    target_include_directories(cube_hal PUBLIC "${_bsp_dir}")
    set(_bsp_src "${_bsp_dir}/stm32${_family_lower}xx_nucleo.c")
    if(EXISTS "${_bsp_src}")
      target_sources(cube_hal PRIVATE "${_bsp_src}")
    endif()
    if(BOARD_BSP_DEFINE)
      target_compile_definitions(cube_hal PUBLIC ${BOARD_BSP_DEFINE})
    endif()
  endif()

  target_compile_definitions(cube_hal PUBLIC USE_HAL_DRIVER ${BOARD_HAL_DEFINE})
  target_link_libraries(platform_stm32 PUBLIC cube_hal)

  set(PLATFORM_CORE_SRCS ${CUBE_CORE_SRCS} ${CUBE_STARTUP_SRCS} PARENT_SCOPE)
else()
  target_sources(platform_stm32 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/shim/startup_cortex_m4.s
    ${CMAKE_CURRENT_SOURCE_DIR}/shim/system_stub.c
  )
endif()

add_library(platform::platform ALIAS platform_stm32)
