---
description: A whole CMakeLists.txt that downloads and builds SFML 3 with the game, for machines without SFML
file: CMakeLists.txt
whole-file: yes
ask: ProjectName = MyGame
order: 11
section: starters
---
# Builds SFML 3 from source along with the game. On Debian/Ubuntu it needs these packages first:
#   sudo apt install libxrandr-dev libxcursor-dev libxi-dev libudev-dev libfreetype-dev
#                    libflac-dev libvorbis-dev libgl1-mesa-dev libegl1-mesa-dev
cmake_minimum_required(VERSION 3.28)
project({{ProjectName}} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
FetchContent_Declare(SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG 3.0.2
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL # only the modules the game links to get built
    SYSTEM # no warnings from SFML's headers
)
FetchContent_MakeAvailable(SFML)

add_executable(${PROJECT_NAME}
    src/main.cpp
)
# Also available: SFML::Audio, SFML::Network (Graphics brings Window and System with it).
target_link_libraries(${PROJECT_NAME} PRIVATE SFML::Graphics)
