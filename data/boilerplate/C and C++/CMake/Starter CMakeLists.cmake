---
label: Starter CMakeLists.txt
description: A C++20 project with one program built from src/main.cpp
file: CMakeLists.txt
whole-file: yes
ask: ProjectName = MyApp
order: 10
section: starters
---
cmake_minimum_required(VERSION 3.20)
project({{ProjectName}} VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# compile_commands.json gives clangd (IntelliSense) the exact compiler flags.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# A Debug build unless asked otherwise: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

add_executable(${PROJECT_NAME}
    src/main.cpp
)
