cmake_minimum_required(VERSION 3.10)
project(uvim VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(UVIM_ENABLE_CLANGD_LSP "Build uvim with optional clangd LSP client (runtime --clangd)" OFF)
option(UVIM_EXPORT_COMPILE_COMMANDS "Export compile_commands.json in the build dir" ON)

if(UVIM_EXPORT_COMPILE_COMMANDS)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

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

# ---- clangd LSP (optional) ----
if(UVIM_ENABLE_CLANGD_LSP)
    target_compile_definitions(uvim PRIVATE UVIM_ENABLE_CLANGD_LSP=1)

    # Minimal dependency for JSON parsing
    include(FetchContent)
    FetchContent_Declare(
      nlohmann_json
      GIT_REPOSITORY https://github.com/nlohmann/json.git
      GIT_TAG v3.11.3
    )
    FetchContent_MakeAvailable(nlohmann_json)

    find_package(Threads REQUIRED)

    target_sources(uvim PRIVATE
        src/lsp_client.cpp
    )
    target_link_libraries(uvim PRIVATE nlohmann_json::nlohmann_json Threads::Threads)
endif()
