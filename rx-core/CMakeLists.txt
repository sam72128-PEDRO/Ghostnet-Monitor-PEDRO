cmake_minimum_required(VERSION 3.20)
project(ghostnet_rx_core VERSION 0.1.0 LANGUAGES C CXX)

# First-party GhostNet Monitor receive core: RTL-SDR source, DSP pipeline,
# receive-only JS8 engine wrapper, frame unpacking and message assembly.
# Builds on host (Linux/macOS) for tests and the ghostnet-rx CLI, and is
# pulled into the Android app via phase0-poc/app/CMakeLists.txt.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

get_filename_component(GHOSTNET_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(GHOSTNET_THIRD_PARTY "${GHOSTNET_ROOT}/third_party")

option(GHOSTNET_WITH_RTLSDR "Build the RTL-SDR USB source (libusb + librtlsdr)" ON)

include(CTest)

# --- dependencies -----------------------------------------------------------

if(NOT TARGET ghostnet_rf_frontend)
  set(_saved_testing ${BUILD_TESTING})
  set(BUILD_TESTING OFF)
  add_subdirectory("${GHOSTNET_ROOT}/rf-frontend" "${CMAKE_CURRENT_BINARY_DIR}/rf-frontend" EXCLUDE_FROM_ALL)
  set(BUILD_TESTING ${_saved_testing})
endif()

if(NOT TARGET js8core)
  set(EIGEN3_INCLUDE_DIR "${GHOSTNET_THIRD_PARTY}" CACHE PATH "Vendored Eigen" FORCE)
  set(_saved_testing ${BUILD_TESTING})
  set(BUILD_TESTING OFF)
  add_subdirectory("${GHOSTNET_THIRD_PARTY}/js8core" "${CMAKE_CURRENT_BINARY_DIR}/js8core" EXCLUDE_FROM_ALL)
  set(BUILD_TESTING ${_saved_testing})
endif()

if(GHOSTNET_WITH_RTLSDR)
  include(cmake/libusb.cmake)
  include(cmake/rtlsdr.cmake)
endif()

# --- library ----------------------------------------------------------------

add_library(ghostnet_rx_core STATIC
  src/js8_frames.cpp
  src/message_assembler.cpp
  src/receiver.cpp
  src/file_iq_source.cpp
  src/test_signal.cpp
  src/spectrum.cpp
  src/rtty.cpp
)
add_library(ghostnet::rx_core ALIAS ghostnet_rx_core)
target_include_directories(ghostnet_rx_core PUBLIC include)
target_link_libraries(ghostnet_rx_core PUBLIC ghostnet::rf_frontend js8core)
if(GHOSTNET_WITH_RTLSDR)
  target_sources(ghostnet_rx_core PRIVATE src/rtlsdr_source.cpp)
  target_link_libraries(ghostnet_rx_core PUBLIC ghostnet_rtlsdr)
endif()

# Signal generators for tests and host tools only; never linked into the app.
add_library(ghostnet_rx_testsupport STATIC src/rtty_test_signal.cpp)
target_link_libraries(ghostnet_rx_testsupport PUBLIC ghostnet::rx_core)

# --- desktop tool -----------------------------------------------------------

if(GHOSTNET_WITH_RTLSDR AND NOT ANDROID)
  add_executable(ghostnet-rx tools/ghostnet_rx.cpp)
  target_link_libraries(ghostnet-rx PRIVATE ghostnet::rx_core)
endif()
if(NOT ANDROID)
  add_executable(make-test-capture tools/make_test_capture.cpp)
  target_link_libraries(make-test-capture PRIVATE ghostnet::rx_core)
endif()

# --- tests ------------------------------------------------------------------

if(BUILD_TESTING)
  add_executable(ghostnet-rx-assembler-test tools/assembler_test.cpp)
  target_link_libraries(ghostnet-rx-assembler-test PRIVATE ghostnet::rx_core)
  add_test(NAME ghostnet-rx-assembler COMMAND ghostnet-rx-assembler-test)

  add_executable(ghostnet-rx-rtty-test tools/rtty_test.cpp)
  target_link_libraries(ghostnet-rx-rtty-test PRIVATE ghostnet_rx_testsupport)
  add_test(NAME ghostnet-rx-rtty COMMAND ghostnet-rx-rtty-test)

  add_executable(ghostnet-rx-end-to-end-test tools/end_to_end_test.cpp)
  target_link_libraries(ghostnet-rx-end-to-end-test PRIVATE ghostnet_rx_testsupport)
  add_test(NAME ghostnet-rx-end-to-end COMMAND ghostnet-rx-end-to-end-test)
  set_tests_properties(ghostnet-rx-end-to-end PROPERTIES TIMEOUT 300)
endif()
