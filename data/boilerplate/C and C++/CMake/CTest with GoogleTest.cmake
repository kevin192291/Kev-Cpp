---
description: Downloads GoogleTest and registers every TEST() with CTest, so ctest runs and reports each one
ask: TestTarget = unit_tests
order: 50
section: testing
---
# Build, then run the tests: ctest --test-dir build --output-on-failure
# Code under test is easiest to share as a library linked to both the program and the tests.
enable_testing()

include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
    GIT_SHALLOW TRUE
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE) # keep it out of `cmake --install`
FetchContent_MakeAvailable(googletest)

add_executable({{TestTarget}}
    tests/example_test.cpp
)
target_link_libraries({{TestTarget}} PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests({{TestTarget}})
