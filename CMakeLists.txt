cmake_minimum_required(VERSION 3.14)
project(uvim VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build options
option(UVIM_ENABLE_CLANGD_LSP "Build uvim with clangd LSP support" OFF)
option(UVIM_EXPORT_COMPILE_COMMANDS "Export compile_commands.json" ON)
option(UVIM_DEBUG_LOGGING "Enable debug logging to /tmp/uvim_debug.txt" OFF)
option(UVIM_BUILD_TESTS "Build unit tests" ON)
option(UVIM_ENABLE_SANITIZERS "Enable ASan+UBSan (AddressSanitizer + UndefinedBehaviorSanitizer)" OFF)

if(UVIM_ENABLE_SANITIZERS)
    message(STATUS "UVIM_ENABLE_SANITIZERS=ON (ASan+UBSan)")
endif()

# Terminal backend selection (AUTO/POSIX/WIN32)
set(UVIM_TERMINAL_BACKEND "AUTO" CACHE STRING "Terminal backend: AUTO, POSIX, WIN32")
set_property(CACHE UVIM_TERMINAL_BACKEND PROPERTY STRINGS AUTO POSIX WIN32)

if(UVIM_EXPORT_COMPILE_COMMANDS)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# Include directories - make headers available to all targets
include_directories(${CMAKE_SOURCE_DIR}/src)

# Core editor sources
set(UVIM_CORE_SOURCES
    src/main.cpp
    src/terminal.cpp
    src/editor.cpp
    src/editor_lsp_info.cpp
    src/cursor_movement.cpp
    src/text_operations.cpp
    src/search.cpp
    src/syntax_highlighting.cpp
    src/undo_redo.cpp
    src/drawing.cpp
    src/lsp_completion.cpp
    src/cpp_constants.cpp
    src/editor_references.cpp
    src/stdlib_goto.cpp
    src/theme.cpp
)

# Mode state machine sources (refactored)
set(MODE_STATE_MACHINE_SOURCES
    src/mode_state_machine.cpp
    src/normal_mode.cpp
    src/welcome_mode.cpp
    src/insert_mode.cpp
    src/visual_mode.cpp
    src/command_mode.cpp
    src/search_mode.cpp
    src/operator_pending_mode.cpp
    src/references_mode.cpp
    src/lsp_info_mode.cpp
    src/help_mode.cpp
    src/buffer_browser_mode.cpp
    src/fuzzy_find_mode.cpp
    src/grep_search_mode.cpp
    src/file_browser_mode.cpp
)

# Combine all sources
set(UVIM_SOURCES
    ${UVIM_CORE_SOURCES}
    ${MODE_STATE_MACHINE_SOURCES}
)

# LSP client only needed when LSP is enabled
if(UVIM_ENABLE_CLANGD_LSP)
    list(APPEND UVIM_SOURCES
        src/lsp_client.cpp
    )
endif()

add_executable(uvim ${UVIM_SOURCES})
target_compile_definitions(uvim PRIVATE UVIM_VERSION="${PROJECT_VERSION}")

# ------------------------------------------------------------------------------
# Terminal backend selection -> compile definitions
# ------------------------------------------------------------------------------
set(_backend "${UVIM_TERMINAL_BACKEND}")
if(_backend STREQUAL "AUTO")
    if(WIN32)
        set(_backend "WIN32")
    else()
        set(_backend "POSIX")
    endif()
endif()

if(_backend STREQUAL "WIN32")
    target_compile_definitions(uvim PRIVATE UVIM_TERMINAL_WIN32=1)
elseif(_backend STREQUAL "POSIX")
    target_compile_definitions(uvim PRIVATE UVIM_TERMINAL_POSIX=1)
else()
    message(FATAL_ERROR "Invalid UVIM_TERMINAL_BACKEND='${UVIM_TERMINAL_BACKEND}'. Use AUTO, POSIX, or WIN32.")
endif()

message(STATUS "uvim terminal backend: ${_backend}")

# ------------------------------------------------------------------------------
# Compiler warnings / options (portable)
# ------------------------------------------------------------------------------
if(MSVC)
    target_compile_options(uvim PRIVATE /W4 /permissive-)
else()
    target_compile_options(uvim PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ------------------------------------------------------------------------------
# Sanitizers (ASan + UBSan) - behind a flag
# ------------------------------------------------------------------------------
function(uvim_enable_sanitizers target)
    if(NOT UVIM_ENABLE_SANITIZERS)
        return()
    endif()

    # MSVC does not support -fsanitize flags (unless using clang-cl in some setups).
    if(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
        message(WARNING "UVIM_ENABLE_SANITIZERS=ON requested, but MSVC does not support ASan/UBSan via -fsanitize. Ignoring for target: ${target}")
        return()
    endif()

    set(_uvim_sanitize_flags "-fsanitize=address,undefined")

    target_compile_options(${target} PRIVATE
        ${_uvim_sanitize_flags}
        -fno-omit-frame-pointer
        -fno-sanitize-recover=all
    )
    target_link_options(${target} PRIVATE
        ${_uvim_sanitize_flags}
    )
endfunction()

# Apply sanitizers to main binary
uvim_enable_sanitizers(uvim)


# Release optimizations (portable)
target_compile_definitions(uvim PRIVATE
    $<$<CONFIG:Release>:NDEBUG>
)

if(MSVC)
    target_compile_options(uvim PRIVATE
        $<$<CONFIG:Release>:/O2>
    )
else()
    target_compile_options(uvim PRIVATE
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-ffunction-sections>
        $<$<CONFIG:Release>:-fdata-sections>
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
    FetchContent_GetProperties(nlohmann_json)
    if(nlohmann_json_POPULATED)
        message(STATUS "Using cached nlohmann/json at: ${nlohmann_json_SOURCE_DIR}")
    else()
        message(STATUS "Fetching nlohmann/json (v3.11.3) ...")
    endif()
    FetchContent_Declare(
      nlohmann_json
      GIT_REPOSITORY https://github.com/nlohmann/json.git
      GIT_TAG v3.11.3
      GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)

    find_package(Threads REQUIRED)
    target_link_libraries(uvim PRIVATE nlohmann_json::nlohmann_json Threads::Threads)
endif()

# Debug logging
if(UVIM_DEBUG_LOGGING)
    target_compile_definitions(uvim PRIVATE UVIM_DEBUG_LOGGING=1)
endif()

# ============================================================================
# Unit Tests
# ============================================================================

if(UVIM_BUILD_TESTS)
    enable_testing()

    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )

    if(MSVC)
        # Prevent overriding the parent project's compiler/linker settings
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    endif()

    FetchContent_MakeAvailable(googletest)

    add_executable(state_machine_tests
        tests/state_machine_test.cpp
    )
    uvim_enable_sanitizers(state_machine_tests)
    target_link_libraries(state_machine_tests
        GTest::gtest_main
        GTest::gmock
    )
    target_include_directories(state_machine_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

    add_executable(mode_state_machine_tests
        tests/mode_state_machine_test.cpp
    )
    uvim_enable_sanitizers(mode_state_machine_tests)
    target_link_libraries(mode_state_machine_tests
        GTest::gtest_main
        GTest::gmock
    )
    target_include_directories(mode_state_machine_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

    include(GoogleTest)
    gtest_discover_tests(state_machine_tests)
    gtest_discover_tests(mode_state_machine_tests)

    add_custom_target(check
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS state_machine_tests mode_state_machine_tests
    )
endif()
