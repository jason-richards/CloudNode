# iOS app: cross-builds libdatachannel for iOS, then builds the Swift package with xtool.
#
#   cmake -B build -DBUILD_IOS_APP=ON
#   cmake --build build --target ios_app   # libraries + xtool dev build
#   cmake --build build --target ios_run   # build, install and launch on a device
include(ExternalProject)

set(IOS_PLATFORM "iphoneos" CACHE STRING "iphoneos or iphonesimulator")
set_property(CACHE IOS_PLATFORM PROPERTY STRINGS iphoneos iphonesimulator)
set(IOS_MIN_VERSION "17.0" CACHE STRING "Minimum iOS version")
set(IOS_SDK_ROOT "$ENV{HOME}/.swiftpm/swift-sdks/darwin.artifactbundle/Developer/Platforms"
    CACHE PATH "Platforms directory of the xtool Darwin SDK")

file(GLOB _swift_hints /opt/swift-*/usr/bin)
find_program(IOS_SWIFT_EXECUTABLE swift HINTS ${_swift_hints} REQUIRED)
find_program(IOS_XTOOL_EXECUTABLE xtool REQUIRED)
get_filename_component(IOS_SWIFT_BIN "${IOS_SWIFT_EXECUTABLE}" DIRECTORY)

if(IOS_PLATFORM STREQUAL "iphoneos")
    set(IOS_SYSROOT "${IOS_SDK_ROOT}/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk")
    set(IOS_TARGET "arm64-apple-ios${IOS_MIN_VERSION}")
else()
    set(IOS_SYSROOT "${IOS_SDK_ROOT}/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk")
    set(IOS_TARGET "arm64-apple-ios${IOS_MIN_VERSION}-simulator")
endif()
if(NOT EXISTS "${IOS_SYSROOT}")
    message(FATAL_ERROR "iOS SDK not found at ${IOS_SYSROOT}; run 'xtool setup' or set IOS_SDK_ROOT.")
endif()

set(IOS_TOOLCHAIN "${CMAKE_CURRENT_BINARY_DIR}/ios-toolchain.cmake")
configure_file(cmake/ios-toolchain.cmake.in "${IOS_TOOLCHAIN}" @ONLY)

# Linked by the generated Package.swift.
set(IOS_DEPS_DIR "${CMAKE_CURRENT_BINARY_DIR}/deps/${IOS_PLATFORM}")
set(IOS_VENDOR_LIB_DIR "${IOS_DEPS_DIR}/lib")
set(IOS_HEADER_DIR "${IOS_DEPS_DIR}/include")

set(IOS_MBEDTLS_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/mbedtls-install")

ExternalProject_Add(ios_mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG v3.6.7
    GIT_SHALLOW ON
    GIT_SUBMODULES_RECURSE ON
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}/mbedtls"
    # libdatachannel needs DTLS-SRTP symbols, which the default config leaves out.
    PATCH_COMMAND python3 scripts/config.py set MBEDTLS_SSL_DTLS_SRTP
    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${IOS_TOOLCHAIN}
        -DCMAKE_BUILD_TYPE=Release
        -DENABLE_PROGRAMS=OFF
        -DENABLE_TESTING=OFF
        -DUSE_SHARED_MBEDTLS_LIBRARY=OFF
        -DUSE_STATIC_MBEDTLS_LIBRARY=ON
        -DCMAKE_INSTALL_PREFIX=${IOS_MBEDTLS_PREFIX}
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --parallel
    INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target install
    BUILD_BYPRODUCTS "${IOS_MBEDTLS_PREFIX}/lib/libmbedtls.a"
)

# Reuses the libdatachannel checkout the macOS/Linux build already fetched.
ExternalProject_Add(ios_libdatachannel
    SOURCE_DIR "${datachannel_SOURCE_DIR}"
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}/libdatachannel"
    DEPENDS ios_mbedtls
    DOWNLOAD_COMMAND ""
    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${IOS_TOOLCHAIN}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_PREFIX_PATH=${IOS_MBEDTLS_PREFIX}
        -DBUILD_SHARED_LIBS=OFF
        -DUSE_MBEDTLS=ON
        -DNO_MEDIA=ON
        -DNO_WEBSOCKET=ON
        -DNO_EXAMPLES=ON
        -DNO_TESTS=ON
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --parallel
    INSTALL_COMMAND ${CMAKE_COMMAND}
        -DRTC_BUILD_DIR=<BINARY_DIR>
        -DMBEDTLS_PREFIX=${IOS_MBEDTLS_PREFIX}
        -DRTC_SOURCE_DIR=${datachannel_SOURCE_DIR}
        -DLIB_DIR=${IOS_VENDOR_LIB_DIR}
        -DHEADER_DIR=${IOS_HEADER_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/collect-libs.cmake
    BUILD_BYPRODUCTS "${IOS_VENDOR_LIB_DIR}/libdatachannel.a"
)

add_custom_target(ios_libs DEPENDS ios_libdatachannel)

# xtool needs Package.swift at the package root. It is generated in the build tree
# and shares the sources, so nothing xtool or SwiftPM produce lands in the source tree.
set(IOS_PACKAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/package")
configure_file(Package.swift.in "${IOS_PACKAGE_DIR}/Package.swift" @ONLY)
configure_file(xtool.yml "${IOS_PACKAGE_DIR}/xtool.yml" COPYONLY)
configure_file(Info.plist "${IOS_PACKAGE_DIR}/Info.plist" COPYONLY)
if(NOT EXISTS "${IOS_PACKAGE_DIR}/Sources")
    file(CREATE_LINK "${CMAKE_CURRENT_SOURCE_DIR}/Sources" "${IOS_PACKAGE_DIR}/Sources" SYMBOLIC)
endif()

set(_xtool_env ${CMAKE_COMMAND} -E env "PATH=${IOS_SWIFT_BIN}:$ENV{PATH}")

add_custom_target(ios_app
    COMMAND ${_xtool_env} ${IOS_XTOOL_EXECUTABLE} dev build
    WORKING_DIRECTORY "${IOS_PACKAGE_DIR}"
    COMMENT "Building the iOS app with xtool"
    USES_TERMINAL
    VERBATIM
)
add_dependencies(ios_app ios_libs)

add_custom_target(ios_run
    COMMAND ${_xtool_env} ${IOS_XTOOL_EXECUTABLE} dev
    WORKING_DIRECTORY "${IOS_PACKAGE_DIR}"
    COMMENT "Installing and launching the iOS app with xtool"
    USES_TERMINAL
    VERBATIM
)
add_dependencies(ios_run ios_libs)
