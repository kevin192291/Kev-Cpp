---
description: A static library with its own include folder, linked into the program
ask: Library = core
ask: Target = ${PROJECT_NAME}
order: 20
section: targets
---
# Headers in include/ are visible to the library and to everything linked to it (PUBLIC).
add_library({{Library}} STATIC
    src/{{Library}}.cpp
)
target_include_directories({{Library}} PUBLIC include)
target_compile_features({{Library}} PUBLIC cxx_std_20)

target_link_libraries({{Target}} PRIVATE {{Library}})
