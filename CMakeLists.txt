cmake_minimum_required(VERSION 3.10)
project(uvim VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build options
option(UVIM_ENABLE_CLANGD_LSP "Build uvim with clangd LSP support" OFF)
option(UVIM_EXPORT_COMPILE_COMMANDS "Export compile_commands.json" ON)
option(UVIM_DEBUG_LOGGING "Enable debug logging to /tmp/uvim_debug.txt" OFF)

if(UVIM_EXPORT_COMPILE_COMMANDS)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# All sources - refactored component classes always compiled
set(UVIM_SOURCES
    src/main.cpp
    src/terminal.cpp

    # Core editor
    src/editor.cpp

    # Component classes
    src/buffer_manager.cpp
    src/cursor_movement.cpp
    src/text_operations.cpp
    src/undo_manager.cpp
    src/search_engine.cpp
    src/file_io.cpp
    src/visual_mode.cpp
    src/code_formatter.cpp
    src/operator_pending.cpp

    # Rendering
    src/renderer.cpp
    src/syntax_highlighter.cpp

    # LSP integration (compiles but may be disabled at runtime)
    src/lsp_integration.cpp
)

# LSP client only needed when LSP is enabled
if(UVIM_ENABLE_CLANGD_LSP)
    list(APPEND UVIM_SOURCES
        src/lsp_client_query.cpp
    )
endif()

add_executable(uvim ${UVIM_SOURCES})
target_include_directories(uvim PRIVATE src)

# Release optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(uvim PRIVATE
        -O3
        -DNDEBUG
        -ffunction-sections
        -fdata-sections
    )
endif()

# Enable LTO / IPO
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

# clangd LSP dependencies
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

# Debug logging
if(UVIM_DEBUG_LOGGING)
    target_compile_definitions(uvim PRIVATE UVIM_DEBUG_LOGGING=1)
endif()
