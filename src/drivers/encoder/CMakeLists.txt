add_library(encoder STATIC encoder.c)
target_include_directories(encoder PUBLIC ${PROJECT_SOURCE_DIR}/inc)
target_link_libraries(encoder PUBLIC platform_if)
