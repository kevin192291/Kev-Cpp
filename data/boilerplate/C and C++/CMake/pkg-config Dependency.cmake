---
description: Links libraries found with pkg-config (gtkmm-4.0 here) through one imported target
ask: Modules = gtkmm-4.0
ask: Target = ${PROJECT_NAME}
order: 32
section: dependencies
---
# IMPORTED_TARGET puts the modules' include paths, flags and libraries in PkgConfig::DEPS. List
# several modules separated by spaces; `pkg-config --list-all` shows what is installed.
find_package(PkgConfig REQUIRED)
pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET {{Modules}})
target_link_libraries({{Target}} PRIVATE PkgConfig::DEPS)
