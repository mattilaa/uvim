cmake_minimum_required(VERSION 3.10)
project(uvim VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build uvim with optional clangd LSP client (runtime-enabled with --clangd)
option(UVIM_ENABLE_CLANGD_LSP "Build uvim with clangd LSP support" OFF)
# Export compile_commands.json into the build dir
option(UVIM_EXPORT_COMPILE_COMMANDS "Export compile_commands.json" ON)

if(UVIM_EXPORT_COMPILE_COMMANDS)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

set(UVIM_SOURCES
    src/main.cpp
    src/terminal.cpp
)

if(UVIM_ENABLE_CLANGD_LSP)
    list(APPEND UVIM_SOURCES
        src/editor_lsp_query.cpp
        src/lsp_client_query.cpp
    )
else()
    list(APPEND UVIM_SOURCES
        src/editor.cpp
    )
endif()

add_executable(uvim ${UVIM_SOURCES})
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

# ---- clangd LSP deps (optional) ----
if(UVIM_ENABLE_CLANGD_LSP)
    target_compile_definitions(uvim PRIVATE UVIM_ENABLE_CLANGD_LSP=1)

    include(FetchContent)
    FetchContent_Declare(
      nlohmann_json
      GIT_REPOSITORY https://github.com/nlohmann/json.git
      GIT_TAG v3.11.3
    )
    FetchContent_MakeAvailable(nlohmann_json)

    find_package(Threads REQUIRED)
    target_link_libraries(uvim PRIVATE nlohmann_json::nlohmann_json Threads::Threads)
endif()
