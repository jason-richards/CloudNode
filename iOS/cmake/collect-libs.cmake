# Copies the static libraries and C headers the iOS Swift package links against.
# Inputs: RTC_BUILD_DIR, MBEDTLS_PREFIX, RTC_SOURCE_DIR, LIB_DIR, HEADER_DIR
file(REMOVE_RECURSE "${LIB_DIR}")
file(MAKE_DIRECTORY "${LIB_DIR}" "${HEADER_DIR}")

file(GLOB_RECURSE rtc_libs "${RTC_BUILD_DIR}/*.a")
file(GLOB mbed_libs "${MBEDTLS_PREFIX}/lib/libmbed*.a")
file(COPY ${rtc_libs} ${mbed_libs} DESTINATION "${LIB_DIR}")
file(COPY "${RTC_SOURCE_DIR}/include/rtc/rtc.h" "${RTC_SOURCE_DIR}/include/rtc/version.h"
     DESTINATION "${HEADER_DIR}")
