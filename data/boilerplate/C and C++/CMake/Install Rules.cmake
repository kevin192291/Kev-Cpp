---
description: Install rules, so cmake --install build --prefix ~/.local puts the program in ~/.local/bin
ask: Target = ${PROJECT_NAME}
order: 61
section: install
---
# GNUInstallDirs gives the usual folder names (bin, lib or lib64, share...) for this system.
include(GNUInstallDirs)
install(TARGETS {{Target}}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
