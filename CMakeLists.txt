cmake_minimum_required(VERSION 3.16)

project(waveforge VERSION 1.2.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

file(GLOB WAVEFORGE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

add_library(waveforge STATIC ${WAVEFORGE_SOURCES})
target_include_directories(waveforge PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(waveforge PUBLIC m)

add_executable(wg "${CMAKE_CURRENT_SOURCE_DIR}/apps/wg_main.c")
target_link_libraries(wg PRIVATE waveforge m)

add_executable(selftest "${CMAKE_CURRENT_SOURCE_DIR}/tests/wg_selftest.c")
target_link_libraries(selftest PRIVATE waveforge m)

enable_testing()
add_test(NAME selftest COMMAND selftest)
