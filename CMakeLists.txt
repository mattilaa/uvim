cmake_minimum_required(VERSION 3.14)
set(UVIM_VERSION_MAJOR 0)
set(UVIM_VERSION_MINOR 1)
option(UVIM_AUTO_INCREMENT_BUILD "Auto-increment build number on each CMake configure" ON)
set(UVIM_VERSION_BUILD 0 CACHE STRING "Build number")
set(UVIM_BUILD_NUMBER_FILE "${CMAKE_BINARY_DIR}/.uvim_build_number" CACHE FILEPATH
    "Path to persisted auto-increment build number")

if(UVIM_AUTO_INCREMENT_BUILD)
    set(_uvim_seed "${UVIM_VERSION_BUILD}")
    if(EXISTS "${UVIM_BUILD_NUMBER_FILE}")
        file(READ "${UVIM_BUILD_NUMBER_FILE}" _uvim_build_raw)
        string(STRIP "${_uvim_build_raw}" _uvim_build_raw)
        if(_uvim_build_raw MATCHES "^[0-9]+$")
            set(_uvim_seed "${_uvim_build_raw}")
        endif()
    endif()

    if(NOT _uvim_seed MATCHES "^[0-9]+$")
        set(_uvim_seed 0)
    endif()

    math(EXPR UVIM_VERSION_BUILD "${_uvim_seed} + 1")
    set(UVIM_VERSION_BUILD "${UVIM_VERSION_BUILD}" CACHE STRING "Build number" FORCE)
    file(WRITE "${UVIM_BUILD_NUMBER_FILE}" "${UVIM_VERSION_BUILD}\n")
endif()

project(uvim VERSION ${UVIM_VERSION_MAJOR}.${UVIM_VERSION_MINOR}.${UVIM_VERSION_BUILD})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build options
option(UVIM_ENABLE_CLANGD_LSP "Build uvim with clangd LSP support" OFF)
option(UVIM_EXPORT_COMPILE_COMMANDS "Export compile_commands.json" ON)
option(UVIM_DEBUG_LOGGING "Enable debug logging to /tmp/uvim_debug.txt" OFF)
option(UVIM_DEBUG_LSP "Enable LSP debug logging to /tmp/uvim_lsp_codeactions.log" OFF)
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
    src/editor_buffer_controller.cpp
    src/editor_command_controller.cpp
    src/editor_editing_controller.cpp
    src/editor_file_controller.cpp
    src/editor_git_controller.cpp
    src/editor_lsp_controller.cpp
    src/editor_settings_controller.cpp
    src/editor_split_controller.cpp
    src/editor_visual_controller.cpp
    src/editor_view_buffer_common.cpp
    src/editor_view_buffer_browser.cpp
    src/editor_view_command.cpp
    src/editor_view_command_output.cpp
    src/editor_view_file_browser.cpp
    src/editor_view_fuzzy_find.cpp
    src/editor_view_git_commit.cpp
    src/editor_view_git_fixup.cpp
    src/editor_view_git_log.cpp
    src/editor_view_git_patch.cpp
    src/editor_view_git_show.cpp
    src/editor_view_git_stage.cpp
    src/editor_view_grep_search.cpp
    src/editor_view_help.cpp
    src/editor_view_insert.cpp
    src/editor_view_loc_list.cpp
    src/editor_view_lsp_info.cpp
    src/editor_view_normal.cpp
    src/editor_view_operator_pending.cpp
    src/editor_view_references.cpp
    src/editor_view_replace.cpp
    src/editor_view_search_backward.cpp
    src/editor_view_search_forward.cpp
    src/editor_view_visual.cpp
    src/editor_view_visual_block.cpp
    src/editor_view_visual_line.cpp
    src/editor_view_welcome.cpp
    src/editor_utils.cpp
    src/editor_mode_handlers.cpp
    src/editor_lsp_info.cpp
    src/cursor_movement.cpp
    src/text_operations.cpp
    src/search.cpp
    src/formatter.cpp
    src/git_handler.cpp
    src/syntax_highlighter.cpp
    src/undo_redo.cpp
    src/drawing.cpp
    src/lsp_completion.cpp
    src/cpp_constants.cpp
    src/editor_references.cpp
    src/stdlib_goto.cpp
    src/theme.cpp
    src/widgets/command_popup.cpp
    src/widgets/command_history_popup.cpp
    src/widgets/completion_popup.cpp
    src/widgets/status_bar.cpp
)

# Mode state machine sources (refactored)
set(MODE_STATE_MACHINE_SOURCES
    src/mode_state_machine.cpp
    src/command_prompt.cpp
    src/normal_mode.cpp
    src/welcome_mode.cpp
    src/insert_mode.cpp
    src/visual_mode.cpp
    src/command_mode.cpp
    src/search_mode.cpp
    src/operator_pending_mode.cpp
    src/references_mode.cpp
    src/lsp_info_mode.cpp
    src/loc_mode.cpp
    src/help_mode.cpp
    src/git_show_mode.cpp
    src/git_log_mode.cpp
    src/git_stage_mode.cpp
    src/git_commit_mode.cpp
    src/git_fixup_mode.cpp
    src/git_patch_mode.cpp
    src/buffer_browser_mode.cpp
    src/fuzzy_find_mode.cpp
    src/grep_search_mode.cpp
    src/file_browser_mode.cpp
    src/command_output_mode.cpp
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
add_custom_command(TARGET uvim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Built uvim v${PROJECT_VERSION} - build ${PROJECT_VERSION_PATCH}"
)

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

# rapidjson (JSON parsing) - header-only, avoid subproject CMake
include(FetchContent)
FetchContent_Declare(
  rapidjson
  GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
  GIT_TAG v1.1.0
  GIT_PROGRESS TRUE
)
FetchContent_GetProperties(rapidjson)
if(rapidjson_POPULATED)
    message(STATUS "Using cached RapidJSON at: ${rapidjson_SOURCE_DIR}")
else()
    message(STATUS "Fetching RapidJSON (v1.1.0) ...")
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(rapidjson)
endif()
set(RAPIDJSON_INCLUDE_DIR ${rapidjson_SOURCE_DIR}/include)
target_include_directories(uvim PRIVATE ${RAPIDJSON_INCLUDE_DIR})

#fmt (formatting library)
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 12.1.0
  GIT_PROGRESS TRUE
)

FetchContent_GetProperties(fmt)

if(fmt_POPULATED)
    message(STATUS "Using cached fmt::format at: ${fmt_SOURCE_DIR}")
else()
    message(STATUS "Fetching fmt::format (v12.1.0) ...")
endif()

FetchContent_MakeAvailable(fmt)

target_link_libraries(uvim PRIVATE fmt::fmt)

# clangd LSP dependencies
if(UVIM_ENABLE_CLANGD_LSP)
    target_compile_definitions(uvim PRIVATE UVIM_ENABLE_CLANGD_LSP=1)

    find_package(Threads REQUIRED)
    target_link_libraries(uvim PRIVATE Threads::Threads fmt::fmt)
endif()

# Debug logging
if(UVIM_DEBUG_LOGGING)
    target_compile_definitions(uvim PRIVATE UVIM_DEBUG_LOGGING=1)
endif()

# LSP debug logging
if(UVIM_DEBUG_LSP)
    target_compile_definitions(uvim PRIVATE UVIM_DEBUG_LSP=1)
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
        tests/text_utils_test.cpp
    )
    uvim_enable_sanitizers(state_machine_tests)
    target_link_libraries(state_machine_tests
        GTest::gtest_main
        GTest::gmock
        fmt::fmt
    )
    target_include_directories(state_machine_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_include_directories(state_machine_tests PRIVATE ${RAPIDJSON_INCLUDE_DIR})

    add_executable(mode_state_machine_tests
        tests/mode_state_machine_test.cpp
    )
    uvim_enable_sanitizers(mode_state_machine_tests)
    target_link_libraries(mode_state_machine_tests
        GTest::gtest_main
        GTest::gmock
        fmt::fmt
    )
    target_include_directories(mode_state_machine_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_include_directories(mode_state_machine_tests PRIVATE ${RAPIDJSON_INCLUDE_DIR})

    add_executable(constants_tests
        tests/constants_test.cpp
    )
    uvim_enable_sanitizers(constants_tests)
    target_link_libraries(constants_tests
        GTest::gtest_main
        GTest::gmock
        fmt::fmt
    )
    target_include_directories(constants_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_include_directories(constants_tests PRIVATE ${RAPIDJSON_INCLUDE_DIR})

    set(UVIM_TEST_SOURCES
        ${UVIM_CORE_SOURCES}
        ${MODE_STATE_MACHINE_SOURCES}
    )
    list(REMOVE_ITEM UVIM_TEST_SOURCES src/main.cpp)
    if(UVIM_ENABLE_CLANGD_LSP)
        list(APPEND UVIM_TEST_SOURCES src/lsp_client.cpp)
    endif()

    add_executable(real_mode_transitions_tests
        tests/real_mode_transitions_test.cpp
        tests/git_log_view_test.cpp
        tests/git_stage_layout_test.cpp
        tests/editor_filetype_test.cpp
        tests/syntax_highlighter_test.cpp
        tests/js_ts_resolution_test.cpp
        tests/loc_count_test.cpp
        ${UVIM_TEST_SOURCES}
    )
    uvim_enable_sanitizers(real_mode_transitions_tests)
    target_link_libraries(real_mode_transitions_tests PRIVATE
        GTest::gtest_main
        GTest::gmock
        fmt::fmt
    )
    target_include_directories(real_mode_transitions_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_include_directories(real_mode_transitions_tests PRIVATE ${RAPIDJSON_INCLUDE_DIR})
    target_compile_definitions(real_mode_transitions_tests PRIVATE UVIM_TESTING=1)
    if(_backend STREQUAL "WIN32")
        target_compile_definitions(real_mode_transitions_tests PRIVATE UVIM_TERMINAL_WIN32=1)
    else()
        target_compile_definitions(real_mode_transitions_tests PRIVATE UVIM_TERMINAL_POSIX=1)
    endif()
    if(UVIM_ENABLE_CLANGD_LSP)
        target_compile_definitions(real_mode_transitions_tests PRIVATE UVIM_ENABLE_CLANGD_LSP=1)
        target_link_libraries(real_mode_transitions_tests PRIVATE
            Threads::Threads
        )
    endif()

    include(GoogleTest)
    gtest_discover_tests(state_machine_tests)
    gtest_discover_tests(mode_state_machine_tests)
    gtest_discover_tests(constants_tests)
    gtest_discover_tests(real_mode_transitions_tests)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME mlang_lsp_scaffold_integration
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/mlang_lsp_integration_test.py
        )
    endif()

    add_custom_target(check
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS state_machine_tests mode_state_machine_tests constants_tests real_mode_transitions_tests
    )
endif()

# ============================================================================
# Install (binary only)
# ============================================================================
install(CODE "message(STATUS \"Installing uvim v${PROJECT_VERSION} - build ${PROJECT_VERSION_PATCH}\")"
    COMPONENT uvim
)
install(TARGETS uvim
    RUNTIME DESTINATION bin
    COMPONENT uvim
)
