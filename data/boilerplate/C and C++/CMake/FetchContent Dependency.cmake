---
description: Downloads a library when CMake first runs (fmt here) and links the program to it
ask: Target = ${PROJECT_NAME}
order: 30
section: dependencies
---
# Fetched into the build folder the first time CMake configures; nothing needs installing. Goes after
# add_executable(). Pin a tag (not a branch) so every build gets the same code.
include(FetchContent)
FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 12.1.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(fmt)
target_link_libraries({{Target}} PRIVATE fmt::fmt)
