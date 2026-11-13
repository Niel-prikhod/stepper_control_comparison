add_library(motor STATIC stepper.c)
target_include_directories(motor
  PUBLIC ${PROJECT_SOURCE_DIR}/inc
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(motor PUBLIC platform_if)
