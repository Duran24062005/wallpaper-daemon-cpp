cmake_minimum_required(VERSION 3.16)

project(wallpaper-daemon-cpp
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(wallpaper-daemon
    src/main.cpp
    src/config.cpp
)