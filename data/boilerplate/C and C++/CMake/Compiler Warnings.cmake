---
description: Turns on the useful warnings for a target, with an option to make them errors
ask: Target = ${PROJECT_NAME}
order: 40
section: options
---
# cmake -B build -DWARNINGS_AS_ERRORS=ON makes any warning fail the build (handy in CI).
option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
if(MSVC)
    target_compile_options({{Target}} PRIVATE /W4 /permissive-)
    if(WARNINGS_AS_ERRORS)
        target_compile_options({{Target}} PRIVATE /WX)
    endif()
else()
    target_compile_options({{Target}} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    if(WARNINGS_AS_ERRORS)
        target_compile_options({{Target}} PRIVATE -Werror)
    endif()
endif()
