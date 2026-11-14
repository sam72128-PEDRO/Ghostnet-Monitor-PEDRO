cmake_minimum_required(VERSION 3.22)
project(ghostnet_phase0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(EIGEN3_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party" CACHE PATH "Vendored Eigen")

set(BUILD_TESTING OFF CACHE BOOL "Disable host-only js8core tests in the Android harness")
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/js8core ${CMAKE_CURRENT_BINARY_DIR}/js8core EXCLUDE_FROM_ALL)

add_library(ghostnet_phase0 SHARED src/main/native/phase0_jni.cpp)
target_include_directories(ghostnet_phase0 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/js8core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/js8core
)
target_link_libraries(ghostnet_phase0 PRIVATE js8core log)
