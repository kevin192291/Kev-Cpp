---
description: Links an installed library found through its CMake package (Threads, for std::thread)
ask: Target = ${PROJECT_NAME}
order: 31
section: dependencies
---
# REQUIRED stops with a clear message when the package isn't installed. Each package documents its
# targets: ZLIB::ZLIB, CURL::libcurl, OpenGL::GL, Boost::filesystem...
find_package(Threads REQUIRED)
target_link_libraries({{Target}} PRIVATE Threads::Threads)
