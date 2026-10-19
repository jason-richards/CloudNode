include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(CloudNodeTests
    CloudNodeTests.cpp
)
target_link_libraries(CloudNodeTests PRIVATE
    GTest::gtest_main
    CloudNodeCore
    Threads::Threads
)

gtest_discover_tests(CloudNodeTests)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(
    NAME CloudNodeThreeProcessFileTransfer
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/three_process_file_transfer.py
        $<TARGET_FILE:CloudNode>
)
set_tests_properties(CloudNodeThreeProcessFileTransfer PROPERTIES TIMEOUT 90)