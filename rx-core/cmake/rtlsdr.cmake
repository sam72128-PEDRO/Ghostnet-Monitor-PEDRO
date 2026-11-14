# Builds vendored librtlsdr (rtl-sdr-blog fork, GPL-2.0) as a static library.
# Only the library is built — none of the rtl_* command-line tools, and in
# particular nothing that transmits (RTL-SDR hardware is receive-only).

set(RTLSDR_ROOT "${GHOSTNET_THIRD_PARTY}/rtl-sdr-blog")

add_library(ghostnet_rtlsdr STATIC
  "${RTLSDR_ROOT}/src/librtlsdr.c"
  "${RTLSDR_ROOT}/src/tuner_e4k.c"
  "${RTLSDR_ROOT}/src/tuner_fc0012.c"
  "${RTLSDR_ROOT}/src/tuner_fc0013.c"
  "${RTLSDR_ROOT}/src/tuner_fc2580.c"
  "${RTLSDR_ROOT}/src/tuner_r82xx.c"
)
target_include_directories(ghostnet_rtlsdr PUBLIC "${RTLSDR_ROOT}/include")
target_compile_definitions(ghostnet_rtlsdr PUBLIC rtlsdr_STATIC)
target_compile_options(ghostnet_rtlsdr PRIVATE -w)
# Linux hosts: detach the DVB-T kernel driver automatically if it grabbed
# the dongle. Not applicable on Android (no kernel driver claims it there).
if(UNIX AND NOT APPLE AND NOT ANDROID)
  target_compile_definitions(ghostnet_rtlsdr PRIVATE DETACH_KERNEL_DRIVER)
endif()
target_link_libraries(ghostnet_rtlsdr PUBLIC ghostnet_libusb)
