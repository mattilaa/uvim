cmake_minimum_required(VERSION 3.14)
project(uvim VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build options
option(UVIM_ENABLE_CLANGD_LSP "Build uvim with clangd LSP support" OFF)
option(UVIM_EXPORT_COMPILE_COMMANDS "Export compile_commands.json" ON)
option(UVIM_DEBUG_LOGGING "Enable debug logging to /tmp/uvim_debug.txt" OFF)
option(UVIM_BUILD_TESTS "Build unit tests" ON)

if(UVIM_EXPORT_COMPILE_COMMANDS)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# Include directories - make headers available to all targets
include_directories(${CMAKE_SOURCE_DIR}/src)

# Sources - main editor with extracted components
set(UVIM_SOURCES
    src/main.cpp
    src/terminal.cpp
    src/editor_lsp_query.cpp
    src/cursor_movement.cpp
    src/text_operations.cpp
    #src/buffer_management.cpp
    src/missing_functions.cpp
    src/search.cpp
    src/fuzzy_finder.cpp
    src/buffer_browser.cpp
    src/grep_search.cpp
    src/file_browser.cpp
    src/syntax_highlighting.cpp
    src/undo_redo.cpp
    src/drawing.cpp
    src/lsp_completion.cpp
)

# Mode state machine sources
set(MODE_STATE_MACHINE_SOURCES
    src/mode_state_machine.cpp
    src/mode_handlers/normal_mode.cpp
    src/mode_handlers/insert_mode.cpp
    src/mode_handlers/visual_mode.cpp
    src/mode_handlers/command_mode.cpp
    src/mode_handlers/search_mode.cpp
    src/mode_handlers/browser_modes.cpp
    src/mode_handlers/operator_pending_mode.cpp
)

# Add state machine sources to main sources
list(APPEND UVIM_SOURCES ${MODE_STATE_MACHINE_SOURCES})

# LSP client only needed when LSP is enabled
if(UVIM_ENABLE_CLANGD_LSP)
    list(APPEND UVIM_SOURCES
        src/lsp_client.cpp
    )
endif()

add_executable(uvim ${UVIM_SOURCES})

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

# ============================================================================
# Unit Tests
# ============================================================================

if(UVIM_BUILD_TESTS)
    enable_testing()

    # Fetch Google Test
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    # State machine tests (standalone - doesn't need editor)
    add_executable(state_machine_tests
        tests/state_machine_test.cpp
    )
    target_link_libraries(state_machine_tests
        GTest::gtest_main
        GTest::gmock
    )
    target_include_directories(state_machine_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

    # Mode state machine tests (standalone - tests inheritance pattern)
    add_executable(mode_state_machine_tests
        tests/mode_state_machine_test.cpp
    )
    target_link_libraries(mode_state_machine_tests
        GTest::gtest_main
        GTest::gmock
    )
    target_include_directories(mode_state_machine_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

    # Register tests with CTest
    include(GoogleTest)
    gtest_discover_tests(state_machine_tests)
    gtest_discover_tests(mode_state_machine_tests)

    # Custom target to run all tests
    add_custom_target(check
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS state_machine_tests mode_state_machine_tests
    )
endif()
