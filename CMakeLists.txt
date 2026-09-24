cmake_minimum_required(VERSION 3.20)
project(CloudNode LANGUAGES C CXX)

find_path(B64_INCLUDE_DIR b64/cencode.h)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)


find_library(B64_LIBRARY NAMES b64)
find_library(ZMQ_LIBRARY NAMES zmq)

include(FetchContent)

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)

if(NOT ZLIB_FOUND)
    message(STATUS "System ZLIB not found, fetching via FetchContent...")
    include(FetchContent)
    FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG        v1.3.1
    )
    FetchContent_MakeAvailable(zlib)
    
    # Create the standard imported target alias if ZLIB built from source
    if(TARGET zlibstatic AND NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    endif()
endif()


FetchContent_Declare(
    datachannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG v0.24.5
    GIT_SHALLOW FALSE
    GIT_PROGRESS on
)
FetchContent_MakeAvailable(datachannel)

FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG v11.4.6
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(ixwebsocket)

# ------------------------------------------------------------------------------
# 1. Fetch uSockets (C backend)
# ------------------------------------------------------------------------------
FetchContent_Declare(
    uSockets
    GIT_REPOSITORY https://github.com/uNetworking/uSockets.git
    GIT_TAG        v0.8.8
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES ""
)
FetchContent_MakeAvailable(uSockets)

# Gather all uSockets C source files
file(GLOB_RECURSE USOCKETS_SOURCES 
    "${usockets_SOURCE_DIR}/src/*.c"
    "${usockets_SOURCE_DIR}/src/*.cpp"
)

add_library(uSockets STATIC ${USOCKETS_SOURCES})

# Tell uSockets/uWebSockets to compile with OpenSSL & ZLIB enabled
target_compile_definitions(uSockets PUBLIC
    LIBUS_USE_OPENSSL
    LIBUS_USE_ZLIB
)

# PUBLIC ensures that anything linking against uSockets also gets this include directory
target_include_directories(uSockets PUBLIC 
    "${usockets_SOURCE_DIR}/src"
)

target_link_libraries(uSockets PUBLIC
    Threads::Threads
    ZLIB::ZLIB
    OpenSSL::SSL
    OpenSSL::Crypto
)


# ------------------------------------------------------------------------------
# 2. Fetch uWebSockets (Header-Only C++ Layer)
# ------------------------------------------------------------------------------
FetchContent_Declare(
    uWebSockets
    GIT_REPOSITORY https://github.com/uNetworking/uWebSockets.git
    GIT_TAG        v20.70.0
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES ""
)
FetchContent_MakeAvailable(uWebSockets)

add_library(uWebSockets INTERFACE)

# Add both the uWebSockets src directory AND uSockets src directory
target_include_directories(uWebSockets INTERFACE 
    "${uwebsockets_SOURCE_DIR}"
    "${uwebsockets_SOURCE_DIR}/src"
    "${usockets_SOURCE_DIR}/src"
)

target_link_libraries(uWebSockets INTERFACE uSockets)


# Specify C++ standard version (adjust as needed)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)


# Build subdirectories in correct order:
include_directories(include)           # Optionally include header directories here or use target_include_directories

# Find and include cppzmq
# If you have the headers in a specific folder (e.g., include/zmq), change the path below
# If headers are standard (e.g. /usr/include), this might find them automatically
if(NOT TARGET cppzmq)
    find_path(CPPZMQ_INCLUDE_DIRS "zmq.hpp")
    if(CPPZMQ_INCLUDE_DIRS)
        include_directories(${CPPZMQ_INCLUDE_DIRS})
    else()
        message(FATAL_ERROR "zmq.hpp not found. Please install cppzmq or update CMakeLists.txt include path.")
    endif()
endif()

find_path(RAPIDJSON_INCLUDE_DIR
    NAMES rapidjson/document.h
    REQUIRED
)

add_library(RapidJSON INTERFACE)

file(GLOB SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_executable(CloudNode ${SOURCES})
target_link_libraries(CloudNode PRIVATE
    datachannel
    zmq 
    RapidJSON
    ${B64_LIBRARY}
    uWebSockets
    ZLIB::ZLIB 
    OpenSSL::SSL 
    OpenSSL::Crypto
)

if(TARGET ixwebsocket)
    target_link_libraries(CloudNode PRIVATE ixwebsocket)
elseif(TARGET ixwebsocket::ixwebsocket)
    target_link_libraries(CloudNode PRIVATE ixwebsocket::ixwebsocket)
else()
    message(FATAL_ERROR "IXWebSocket target was not created by FetchContent.")
endif()
target_include_directories(CloudNode PRIVATE ${B64_INCLUDE_DIR} ${RAPIDJSON_INCLUDE_DIR})

# Testing setup
#enable_testing()
#add_subdirectory(tests)
