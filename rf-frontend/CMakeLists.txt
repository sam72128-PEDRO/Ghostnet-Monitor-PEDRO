cmake_minimum_required(VERSION 3.20)
project(ghostnet_rf_frontend VERSION 0.1.0 LANGUAGES CXX)

# First-party GhostNet Monitor code (not vendored third-party). DSP only:
# turns a real-valued sample stream (as produced by RTL2832U Q-branch
# direct sampling) into demodulated SSB audio at a fixed output rate. No
# USB, no RTL2832U register programming, no Android dependency — see
# PHASE1_PLAN.md work stream 3 for how this is meant to be wired into the
# actual capture path, and for what is still unverified against real
# hardware.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

add_library(ghostnet_rf_frontend STATIC
  src/fir.cpp
  src/nco.cpp
  src/ssb_demodulator.cpp
)
add_library(ghostnet::rf_frontend ALIAS ghostnet_rf_frontend)

target_include_directories(ghostnet_rf_frontend PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

if(BUILD_TESTING)
  add_executable(ghostnet-rf-image-rejection-test tools/image_rejection_test.cpp)
  target_link_libraries(ghostnet-rf-image-rejection-test PRIVATE ghostnet::rf_frontend)
  add_test(NAME ghostnet-rf-image-rejection COMMAND ghostnet-rf-image-rejection-test)

  add_executable(ghostnet-rf-passband-test tools/passband_test.cpp)
  target_link_libraries(ghostnet-rf-passband-test PRIVATE ghostnet::rf_frontend)
  add_test(NAME ghostnet-rf-passband COMMAND ghostnet-rf-passband-test)

  add_executable(ghostnet-rf-iq-path-test tools/iq_path_test.cpp)
  target_link_libraries(ghostnet-rf-iq-path-test PRIVATE ghostnet::rf_frontend)
  add_test(NAME ghostnet-rf-iq-path COMMAND ghostnet-rf-iq-path-test)
endif()
