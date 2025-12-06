cmake_minimum_required(VERSION 3.10)
project(uvim VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(uvim
    src/main.cpp
    src/editor.cpp
    src/terminal.cpp
)

target_include_directories(uvim PRIVATE src)
