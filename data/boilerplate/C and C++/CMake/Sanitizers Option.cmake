---
description: An option to build with AddressSanitizer and UBSan, which stop at memory bugs and undefined behaviour
ask: Target = ${PROJECT_NAME}
order: 41
section: options
---
# cmake -B build -DENABLE_SANITIZERS=ON: a bad pointer, buffer overrun, leak or signed overflow stops
# the program with a report naming the line. Slower, so leave it off for release builds.
option(ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options({{Target}} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options({{Target}} PRIVATE -fsanitize=address,undefined)
endif()
