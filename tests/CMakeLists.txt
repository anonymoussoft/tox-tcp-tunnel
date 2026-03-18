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
add_executable(unit_tests
    unit/placeholder_test.cpp
    unit/test_expected.cpp
)
target_link_libraries(unit_tests PRIVATE
    toxtunnel_lib
    GTest::gtest_main
)
if(MSVC)
    target_compile_options(unit_tests PRIVATE /W4 /WX)
else()
    target_compile_options(unit_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
gtest_discover_tests(unit_tests)
