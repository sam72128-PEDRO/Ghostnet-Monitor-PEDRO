cmake_minimum_required(VERSION 3.22)
project(ghostnet_monitor LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The DSP and decoder must keep up with 240 kS/s in real time even in debug
# APKs; an -O0 build is several times slower. Optimise in every variant.
add_compile_options($<$<CONFIG:Debug>:-O2>)

get_filename_component(GHOSTNET_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(BUILD_TESTING OFF CACHE BOOL "No host tests in the Android build" FORCE)
add_subdirectory("${GHOSTNET_ROOT}/rx-core" "${CMAKE_CURRENT_BINARY_DIR}/rx-core")

add_library(ghostnet_native SHARED src/main/cpp/ghostnet_jni.cpp)
target_link_libraries(ghostnet_native PRIVATE ghostnet::rx_core android log)
