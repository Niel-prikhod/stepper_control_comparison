add_executable(firmware main.c control.c measure.c status.c)
if(PLATFORM_CORE_SRCS)
  target_sources(firmware PRIVATE ${PLATFORM_CORE_SRCS})
endif()
target_include_directories(firmware PRIVATE
  ${PROJECT_SOURCE_DIR}/inc
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(firmware PRIVATE
  APP_BOARD="${BOARD_NAME}"
  APP_MCU="${BOARD_MCU}"
)
target_link_libraries(firmware PRIVATE motor encoder platform::platform)
set_target_properties(firmware PROPERTIES LINK_DEPENDS "${BOARD_LINKER_SCRIPT}")
target_link_options(firmware PRIVATE -Wl,-Map=${CMAKE_BINARY_DIR}/firmware.map)

add_custom_command(TARGET firmware POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:firmware> $<TARGET_FILE_DIR:firmware>/firmware.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:firmware> $<TARGET_FILE_DIR:firmware>/firmware.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:firmware>
  COMMENT "Generating HEX/BIN for ${TARGET_BOARD}"
)
