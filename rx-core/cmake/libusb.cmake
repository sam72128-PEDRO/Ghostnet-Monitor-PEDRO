# Builds vendored libusb (LGPL-2.1) as a static library without autotools.
#
# Backends: Linux usbfs (host Linux and Android) and Darwin (macOS host, for
# running ghostnet-rx on a Mac). On Android the app never enumerates devices
# itself — it wraps the file descriptor Android's UsbManager hands out (see
# rtlsdr_open_sys_dev in rtl-sdr-blog, patched locally).

set(LIBUSB_ROOT "${GHOSTNET_THIRD_PARTY}/libusb")
set(LIBUSB_GEN "${CMAKE_CURRENT_BINARY_DIR}/libusb-gen")

file(WRITE "${LIBUSB_GEN}/version_describe.h"
  "#ifndef LIBUSB_DESCRIBE\n#define LIBUSB_DESCRIBE \"v1.0.30 (vendored by GhostNet Monitor)\"\n#endif\n")

set(_libusb_common_config [=[
#define DEFAULT_VISIBILITY __attribute__ ((visibility ("default")))
#define ENABLE_LOGGING 1
#define PLATFORM_POSIX 1
#define PRINTF_FORMAT(a, b) __attribute__ ((__format__ (__printf__, a, b)))
#define HAVE_CLOCK_GETTIME 1
#define HAVE_NFDS_T 1
#define HAVE_SYS_TIME_H 1
]=])

if(ANDROID)
  file(READ "${LIBUSB_ROOT}/android/config.h" _libusb_config)
elseif(APPLE)
  set(_libusb_config "${_libusb_common_config}#define HAVE_PTHREAD_THREADID_NP 1\n")
elseif(UNIX)
  set(_libusb_config "${_libusb_common_config}#define _GNU_SOURCE 1
#define HAVE_ASM_TYPES_H 1
#define HAVE_PIPE2 1
#define HAVE_EVENTFD 1
#define HAVE_TIMERFD 1
#define HAVE_PTHREAD_CONDATTR_SETCLOCK 1
")
else()
  message(FATAL_ERROR "libusb.cmake: unsupported platform (Linux, Android or macOS only)")
endif()
file(WRITE "${LIBUSB_GEN}/config.h" "${_libusb_config}")

set(_libusb_sources
  "${LIBUSB_ROOT}/libusb/core.c"
  "${LIBUSB_ROOT}/libusb/descriptor.c"
  "${LIBUSB_ROOT}/libusb/hotplug.c"
  "${LIBUSB_ROOT}/libusb/io.c"
  "${LIBUSB_ROOT}/libusb/sync.c"
  "${LIBUSB_ROOT}/libusb/strerror.c"
  "${LIBUSB_ROOT}/libusb/os/events_posix.c"
  "${LIBUSB_ROOT}/libusb/os/threads_posix.c"
)
if(APPLE)
  list(APPEND _libusb_sources "${LIBUSB_ROOT}/libusb/os/darwin_usb.c")
else()
  list(APPEND _libusb_sources
    "${LIBUSB_ROOT}/libusb/os/linux_usbfs.c"
    "${LIBUSB_ROOT}/libusb/os/linux_netlink.c")
endif()

add_library(ghostnet_libusb STATIC ${_libusb_sources})
target_include_directories(ghostnet_libusb
  PRIVATE "${LIBUSB_GEN}" "${LIBUSB_ROOT}/libusb" "${LIBUSB_ROOT}/libusb/os"
  PUBLIC "${LIBUSB_ROOT}/libusb")
target_compile_options(ghostnet_libusb PRIVATE -fvisibility=hidden -w)
find_package(Threads REQUIRED)
target_link_libraries(ghostnet_libusb PUBLIC Threads::Threads)
if(ANDROID)
  target_link_libraries(ghostnet_libusb PUBLIC log)
elseif(APPLE)
  target_link_libraries(ghostnet_libusb PUBLIC objc "-framework IOKit" "-framework CoreFoundation" "-framework Security")
endif()
