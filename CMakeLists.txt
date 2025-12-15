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

# Release optimizations (compiler-only)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(uvim PRIVATE
        -O3
        -DNDEBUG
        -ffunction-sections
        -fdata-sections
    )
endif()

# Enable LTO / IPO safely on macOS
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()
