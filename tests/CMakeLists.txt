# Tests
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# Unit tests
add_executable(toxtunnel_tests
    unit/placeholder_test.cpp
)
target_link_libraries(toxtunnel_tests PRIVATE
    toxtunnel_lib
    GTest::gtest_main
)
gtest_discover_tests(toxtunnel_tests)
