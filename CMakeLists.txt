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

# Common sources
set(UVIM_SOURCES
    src/main.cpp
    src/terminal.cpp
)

if(UVIM_ENABLE_CLANGD_LSP)
    # Refactored editor with separate component classes
    list(APPEND UVIM_SOURCES
        # Core context and main editor
        src/editor.cpp

        # Component classes
        src/buffer_manager.cpp
        src/cursor_movement.cpp
        src/text_operations.cpp
        src/undo_manager.cpp
        src/search_engine.cpp
        src/file_io.cpp

        # Rendering
        src/renderer.cpp
        src/syntax_highlighter.cpp

        # Mode handlers
        src/mode_normal.cpp
        src/mode_insert.cpp
        src/mode_visual.cpp
        src/mode_command.cpp

        # Features
        src/file_browser.cpp
        src/fuzzy_finder.cpp
        src/grep_search.cpp
        src/lsp_integration.cpp
        src/code_formatter.cpp
        src/operator_pending.cpp

        # LSP client
        src/lsp_client_query.cpp
    )
else()
    # Non-LSP build uses simpler editor
    list(APPEND UVIM_SOURCES
        src/editor_simple.cpp
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
