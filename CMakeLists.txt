cmake_minimum_required(VERSION 3.14)
if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()
set(UVIM_VERSION_MAJOR 0)
set(UVIM_VERSION_MINOR 2)
set(UVIM_DISPLAY_VERSION "${UVIM_VERSION_MAJOR}.${UVIM_VERSION_MINOR}")
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

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# Build options
option(UVIM_MINIMAL "Build a vi-style minimal uvim: no optional popups, LSP, tool views, git, fzf/rg search, help, or external integrations" OFF)
option(UVIM_ENABLE_BROWSER_TOOLS "Build file and buffer browser views" ON)
option(UVIM_ENABLE_AUXILIARY_VIEWS "Build auxiliary views and popup tools such as help, loc, lspinfo, glyph select, references, and command output" ON)
option(UVIM_ENABLE_CLANGD_LSP "Build uvim with clangd LSP support" OFF)
option(UVIM_ENABLE_ROBOT_LSP "Build uvim with Robot Framework LSP support" OFF)
option(UVIM_ENABLE_PYTHON_LSP "Build uvim with Python LSP support" OFF)
option(UVIM_ENABLE_MLANG_LSP "Build uvim with Mlang LSP support" OFF)
option(UVIM_MLANG_SEMANTIC_TOKENS_DEFAULT "Enable Mlang LSP semantic token coloring by default" ON)
option(UVIM_ENABLE_HTML_LSP "Build uvim with HTML LSP support" OFF)
option(UVIM_ENABLE_CSS_LSP "Build uvim with CSS LSP support" OFF)
option(UVIM_ENABLE_JSON_LSP "Build uvim with JSON LSP support" OFF)
option(UVIM_ENABLE_TS_LSP "Build uvim with TypeScript/JavaScript LSP support" OFF)
option(UVIM_EXPORT_COMPILE_COMMANDS "Export compile_commands.json" ON)
option(UVIM_DEBUG_LOGGING "Enable general uvim debug logging" OFF)
option(UVIM_DEBUG_LSP "Enable uvim LSP logging" OFF)
option(UVIM_BUILD_TESTS "Build unit tests" ON)
option(UVIM_BOOTSTRAP_CONFIG_ONLY "Configure only uvim-config for bootstrap" OFF)
option(UVIM_ENABLE_SANITIZERS "Enable ASan+UBSan (AddressSanitizer + UndefinedBehaviorSanitizer)" OFF)
option(UVIM_ENABLE_ASM_DOCS "Build uvim with assembly instruction gd documentation" ON)
option(UVIM_ENABLE_GIT_TOOLS "Build git views and git commands" ON)
option(UVIM_ENABLE_SEARCH_TOOLS "Build fuzzy, grep, and regex search views" ON)
option(UVIM_ENABLE_FORMATTERS "Build external formatter integrations" ON)
option(UVIM_ENABLE_SYSTEM_CLIPBOARD "Build system clipboard integration hooks" ON)
option(UVIM_ENABLE_STRUCT_SIZE_POPUP "Build clang-based struct/variable size popup support" ON)
option(UVIM_ENABLE_COLOR_TOOLS "Build ANSI color picker and RGB selector tools" ON)
option(UVIM_ENABLE_TERMINAL_COLORS "Build ANSI terminal coloring for UI and syntax highlighting" ON)
option(UVIM_ENABLE_LTO "Enable link-time optimization / IPO for release builds" ON)
option(UVIM_ENABLE_GC_SECTIONS "Enable function/data sections and linker garbage collection where supported" ON)
option(UVIM_STRIP_BINARY "Strip uvim after linking when the platform supports it" OFF)
option(UVIM_STATIC_LINK "Request static runtime/library linking where supported by the platform toolchain" OFF)
set(UVIM_OPTIMIZATION_LEVEL "O2" CACHE STRING "Release optimization level: O0, O1, O2, O3, Os, Oz")
set_property(CACHE UVIM_OPTIMIZATION_LEVEL PROPERTY STRINGS O0 O1 O2 O3 Os Oz)
set(UVIM_INSTALL_BINDIR "bin" CACHE STRING "Directory where the uvim executable is installed")

if(UVIM_STATIC_LINK)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
endif()

if(UVIM_MINIMAL)
    set(UVIM_ENABLE_CLANGD_LSP OFF CACHE BOOL "" FORCE)
    set(UVIM_ENABLE_ROBOT_LSP OFF CACHE BOOL "" FORCE)
    set(UVIM_ENABLE_PYTHON_LSP OFF CACHE BOOL "" FORCE)
    set(UVIM_ENABLE_MLANG_LSP OFF CACHE BOOL "" FORCE)
    set(UVIM_ENABLE_HTML_LSP OFF CACHE BOOL "" FORCE)
    set(UVIM_ENABLE_CSS_LSP OFF CACHE BOOL "" FORCE)
    set(UVIM_ENABLE_JSON_LSP OFF CACHE BOOL "" FORCE)
    set(UVIM_ENABLE_TS_LSP OFF CACHE BOOL "" FORCE)
    set(UVIM_ENABLE_ASM_DOCS OFF CACHE BOOL "" FORCE)
    set(UVIM_ENABLE_GIT_TOOLS OFF CACHE BOOL "" FORCE)
    set(UVIM_ENABLE_SEARCH_TOOLS OFF CACHE BOOL "" FORCE)
    set(UVIM_ENABLE_FORMATTERS OFF CACHE BOOL "" FORCE)
    set(UVIM_ENABLE_SYSTEM_CLIPBOARD OFF CACHE BOOL "" FORCE)
    set(UVIM_ENABLE_STRUCT_SIZE_POPUP OFF CACHE BOOL "" FORCE)
    set(UVIM_ENABLE_COLOR_TOOLS OFF CACHE BOOL "" FORCE)
    set(UVIM_ENABLE_TERMINAL_COLORS OFF CACHE BOOL "" FORCE)
    set(UVIM_ENABLE_BROWSER_TOOLS OFF CACHE BOOL "" FORCE)
    set(UVIM_ENABLE_AUXILIARY_VIEWS OFF CACHE BOOL "" FORCE)
endif()

set(UVIM_ENABLE_ANY_LSP OFF)
if(UVIM_ENABLE_CLANGD_LSP OR UVIM_ENABLE_ROBOT_LSP OR UVIM_ENABLE_PYTHON_LSP OR
   UVIM_ENABLE_MLANG_LSP OR UVIM_ENABLE_HTML_LSP OR UVIM_ENABLE_CSS_LSP OR
   UVIM_ENABLE_JSON_LSP OR UVIM_ENABLE_TS_LSP)
    set(UVIM_ENABLE_ANY_LSP ON)
endif()

if(UVIM_ENABLE_SANITIZERS)
    message(STATUS "UVIM_ENABLE_SANITIZERS=ON (ASan+UBSan)")
endif()

function(uvim_status_bool label value)
    if(${value})
        set(_uvim_status "ON")
    else()
        set(_uvim_status "OFF")
    endif()
    message(STATUS "  ${label}: ${_uvim_status}")
endfunction()

# Terminal backend selection (AUTO/POSIX/WIN32)
set(UVIM_TERMINAL_BACKEND "AUTO" CACHE STRING "Terminal backend: AUTO, POSIX, WIN32")
set_property(CACHE UVIM_TERMINAL_BACKEND PROPERTY STRINGS AUTO POSIX WIN32)

if(UVIM_EXPORT_COMPILE_COMMANDS)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
else()
    set(CMAKE_EXPORT_COMPILE_COMMANDS OFF)
endif()

# Include directories - make headers available to all targets
include_directories(${CMAKE_SOURCE_DIR}/src)

function(uvim_add_config_target)
    if(TARGET uvim-config)
        return()
    endif()

    add_executable(uvim-config src/uvim_configure.cpp)
    target_compile_definitions(uvim-config PRIVATE
        UVIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    if(MSVC)
        target_compile_options(uvim-config PRIVATE /W4 /permissive-)
    else()
        target_compile_options(uvim-config PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

if(UVIM_BOOTSTRAP_CONFIG_ONLY)
    uvim_add_config_target()
    return()
endif()

# Core editor sources
set(UVIM_CORE_SOURCES
    src/main.cpp
    src/terminal.cpp
    src/buffer_command_pending.cpp
    src/config_parser.cpp
    src/editor.cpp
    src/editor_buffer_controller.cpp
    src/editor_command_controller.cpp
    src/editor_command_execution_controller.cpp
    src/editor_execution_buffer_delete_command.cpp
    src/editor_execution_buffer_delete_force_command.cpp
    src/editor_execution_buffer_list_command.cpp
    src/editor_execution_buffer_next_command.cpp
    src/editor_execution_buffer_previous_command.cpp
    src/editor_execution_buffer_select_command.cpp
    src/editor_execution_cd_command.cpp
    src/editor_execution_cd_root_command.cpp
    src/editor_execution_command_helpers.cpp
    src/editor_execution_commands.cpp
    src/editor_execution_edit_command.cpp
    src/editor_execution_line_jump_command.cpp
    src/editor_execution_new_buffer_command.cpp
    src/editor_execution_no_buffer_only_command.cpp
    src/editor_execution_pwd_command.cpp
    src/editor_execution_quit_all_command.cpp
    src/editor_execution_quit_command.cpp
    src/editor_execution_quit_without_buffer_command.cpp
    src/editor_execution_reload_current_file_command.cpp
    src/editor_execution_requires_buffer_command.cpp
    src/editor_execution_search_command.cpp
    src/editor_execution_set_command.cpp
    src/editor_execution_split_command.cpp
    src/editor_execution_split_explorer_command.cpp
    src/editor_execution_tab_new_command.cpp
    src/editor_execution_tab_next_command.cpp
    src/editor_execution_tab_previous_command.cpp
    src/editor_execution_unknown_command.cpp
    src/editor_execution_write_all_command.cpp
    src/editor_execution_write_as_command.cpp
    src/editor_execution_write_command.cpp
    src/editor_execution_write_quit_all_command.cpp
    src/editor_execution_write_quit_command.cpp
    src/editor_cursor_controller.cpp
    src/editor_definition_controller.cpp
    src/editor_definition_cpp.cpp
    src/editor_definition_mlang.cpp
    src/editor_definition_python.cpp
    src/editor_definition_robot.cpp
    src/editor_definition_web.cpp
    src/editor_drawing_controller.cpp
    src/editor_buffer_view.cpp
    src/editor_editing_controller.cpp
    src/editor_file_controller.cpp
    src/editor_file_type_controller.cpp
    src/editor_indent_controller.cpp
    src/editor_lsp_controller.cpp
    src/editor_lsp_clangd_controller.cpp
    src/editor_lsp_robot_controller.cpp
    src/editor_lsp_python_controller.cpp
    src/editor_lsp_mlang_controller.cpp
    src/editor_lsp_html_controller.cpp
    src/editor_lsp_css_controller.cpp
    src/editor_lsp_json_controller.cpp
    src/editor_lsp_ts_controller.cpp
    src/editor_mode_controller.cpp
    src/editor_size_probe_controller.cpp
    src/editor_operator_controller.cpp
    src/editor_path_utilities.cpp
    src/editor_settings_controller.cpp
    src/editor_split_controller.cpp
    src/editor_visual_controller.cpp
    src/editor_view_buffer_common.cpp
    src/editor_view_command.cpp
    src/editor_view_insert.cpp
    src/editor_message_bar_view.cpp
    src/editor_view_normal.cpp
    src/editor_view_operator_pending.cpp
    src/editor_view_replace.cpp
    src/editor_view_search_backward.cpp
    src/editor_view_search_forward.cpp
    src/editor_status_bar_view.cpp
    src/editor_view_visual.cpp
    src/editor_view_visual_block.cpp
    src/editor_view_visual_line.cpp
    src/editor_view_welcome.cpp
    src/editor_utils.cpp
    src/editor_mode_controller_handlers.cpp
    src/text_utils.cpp
    src/text_operations.cpp
    src/search.cpp
    src/formatter.cpp
    src/syntax_highlighter.cpp
    src/undo_redo.cpp
    src/drawing.cpp
    src/lsp_completion.cpp
    src/cpp_navigation_utilities.cpp
    src/mlang_utilities.cpp
    src/robot_utilities.cpp
    src/symbol_popup_utilities.cpp
    src/cpp_constants.cpp
    src/stdlib_goto.cpp
    src/theme.cpp
    src/widgets/status_bar.cpp
)

if(NOT UVIM_MINIMAL)
    list(APPEND UVIM_CORE_SOURCES
        src/widgets/command_popup.cpp
        src/widgets/command_history_popup.cpp
        src/widgets/popup_base.cpp
    )
endif()

if(UVIM_ENABLE_BROWSER_TOOLS)
    list(APPEND UVIM_CORE_SOURCES
        src/editor_execution_explore_command.cpp
        src/editor_view_buffer_browser.cpp
        src/editor_view_file_browser.cpp
    )
endif()

if(UVIM_ENABLE_AUXILIARY_VIEWS)
    list(APPEND UVIM_CORE_SOURCES
        src/editor_execution_emoji_command.cpp
        src/editor_execution_glyph_select_command.cpp
        src/editor_execution_help_command.cpp
        src/editor_execution_loc_command.cpp
        src/editor_execution_loc_total_command.cpp
        src/editor_execution_lsp_info_command.cpp
        src/editor_view_command_output.cpp
        src/editor_view_help.cpp
        src/editor_view_loc_list.cpp
        src/editor_view_lsp_info.cpp
        src/editor_view_references.cpp
        src/editor_lsp_info.cpp
        src/editor_references_controller.cpp
        src/widgets/completion_popup.cpp
    )
else()
    list(APPEND UVIM_CORE_SOURCES
        src/editor_references_controller_stub.cpp
    )
endif()

if(UVIM_ENABLE_ASM_DOCS)
    list(APPEND UVIM_CORE_SOURCES src/editor_execution_emit_asm_command.cpp)
endif()

if(UVIM_ENABLE_FORMATTERS)
    list(APPEND UVIM_CORE_SOURCES
        src/editor_execution_format_command.cpp
        src/formatter_clang.cpp
        src/formatter_json_yaml.cpp
        src/formatter_mlang.cpp
        src/formatter_python.cpp
        src/formatter_robot.cpp
    )
endif()

# Mode state machine sources (refactored)
set(MODE_STATE_MACHINE_SOURCES
    src/mode_state_machine.cpp
    src/mode_context.cpp
    src/command_prompt.cpp
    src/comment_leader_pending.cpp
    src/comment_leader_pending_actions.cpp
    src/comment_leader_after_c.cpp
    src/comment_leader_after_ci.cpp
    src/comment_leader_after_ci_applied.cpp
    src/comment_leader_after_cii.cpp
    src/comment_leader_after_cii_applied.cpp
    src/comment_leader_after_cii_awaiting_suffix.cpp
    src/visual_text_object_pending.cpp
    src/normal_mode.cpp
    src/replace_mode.cpp
    src/welcome_mode.cpp
    src/insert_mode.cpp
    src/visual_mode.cpp
    src/visual_line_mode.cpp
    src/visual_block_mode.cpp
    src/command_mode.cpp
    src/search_forward_mode.cpp
    src/search_backward_mode.cpp
    src/operator_pending_mode.cpp
)

if(UVIM_ENABLE_BROWSER_TOOLS)
    list(APPEND MODE_STATE_MACHINE_SOURCES
        src/buffer_browser_mode.cpp
        src/file_browser_mode.cpp
    )
endif()

if(UVIM_ENABLE_AUXILIARY_VIEWS)
    list(APPEND MODE_STATE_MACHINE_SOURCES
        src/references_mode.cpp
        src/lsp_info_mode.cpp
        src/loc_mode.cpp
        src/help_mode.cpp
        src/glyph_select_mode.cpp
        src/command_output_mode.cpp
    )
endif()

# Optional feature sources are shared by the main binary and the integration
# style unit tests so feature-disabled builds link the same stubs everywhere.
set(UVIM_OPTIONAL_FEATURE_SOURCES)

# LSP client only needed when any language-server integration is enabled.
if(UVIM_ENABLE_ANY_LSP)
    list(APPEND UVIM_OPTIONAL_FEATURE_SOURCES
        src/lsp_client.cpp
    )
endif()

if(UVIM_ENABLE_GIT_TOOLS)
    list(APPEND UVIM_OPTIONAL_FEATURE_SOURCES
        src/editor_git_controller.cpp
        src/editor_execution_git_add_command.cpp
        src/editor_execution_git_blame_command.cpp
        src/editor_execution_git_commit_command.cpp
        src/editor_execution_git_diff_command.cpp
        src/editor_execution_git_fixup_command.cpp
        src/editor_execution_git_log_command.cpp
        src/editor_execution_git_pretty_log_command.cpp
        src/editor_execution_git_stage_command.cpp
        src/editor_execution_git_stash_command.cpp
        src/editor_execution_git_stash_pop_command.cpp
        src/editor_view_git_commit.cpp
        src/editor_view_git_fixup.cpp
        src/editor_view_git_log.cpp
        src/editor_view_git_patch.cpp
        src/editor_view_git_show.cpp
        src/editor_view_git_stage.cpp
        src/git_show_mode.cpp
        src/git_log_mode.cpp
        src/git_stage_mode.cpp
        src/git_commit_mode.cpp
        src/git_fixup_mode.cpp
        src/git_patch_mode.cpp
    )
else()
    list(APPEND UVIM_OPTIONAL_FEATURE_SOURCES
        src/editor_git_controller_stub.cpp
        src/git_modes_stub.cpp
    )
endif()

if(UVIM_ENABLE_SEARCH_TOOLS)
    list(APPEND UVIM_OPTIONAL_FEATURE_SOURCES
        src/editor_execution_refresh_file_search_command.cpp
        src/editor_view_fuzzy_find.cpp
        src/editor_view_grep_search.cpp
        src/fuzzy_find_mode.cpp
        src/grep_search_mode.cpp
        src/regex_search_mode.cpp
    )
else()
    list(APPEND UVIM_OPTIONAL_FEATURE_SOURCES src/search_modes_stub.cpp)
endif()

if(UVIM_ENABLE_ASM_DOCS)
    list(APPEND UVIM_OPTIONAL_FEATURE_SOURCES src/asm_documentation.cpp)
else()
    list(APPEND UVIM_OPTIONAL_FEATURE_SOURCES src/asm_documentation_stub.cpp)
endif()

if(UVIM_ENABLE_COLOR_TOOLS)
    list(APPEND UVIM_OPTIONAL_FEATURE_SOURCES
        src/editor_execution_ansi_tools_command.cpp
        src/editor_execution_color_picker_command.cpp
        src/editor_execution_color_selector_command.cpp
        src/ansi_tools_mode.cpp
        src/color_picker_mode.cpp
        src/color_selector_mode.cpp
    )
endif()

# Combine all sources
set(UVIM_SOURCES
    ${UVIM_CORE_SOURCES}
    ${MODE_STATE_MACHINE_SOURCES}
    ${UVIM_OPTIONAL_FEATURE_SOURCES}
)

add_executable(uvim ${UVIM_SOURCES})
target_compile_definitions(uvim PRIVATE
    UVIM_VERSION="${PROJECT_VERSION}"
    UVIM_DISPLAY_VERSION="${UVIM_DISPLAY_VERSION}"
)
add_custom_command(TARGET uvim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Built uvim v${PROJECT_VERSION} - build ${PROJECT_VERSION_PATCH}"
)

if(UVIM_STATIC_LINK)
    if(APPLE)
        message(WARNING "UVIM_STATIC_LINK=ON requested, but fully static executables are not supported on macOS. The build will still link system libraries dynamically.")
    elseif(MSVC)
        set_property(TARGET uvim PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    elseif(MINGW)
        target_link_options(uvim PRIVATE -static -static-libgcc -static-libstdc++)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_link_options(uvim PRIVATE -static -static-libgcc -static-libstdc++)
    else()
        message(WARNING "UVIM_STATIC_LINK=ON requested, but no static link flags are known for compiler '${CMAKE_CXX_COMPILER_ID}'.")
    endif()
endif()

uvim_add_config_target()

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
message(STATUS "uvim compile configuration:")
message(STATUS "  build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  terminal backend: ${_backend}")
message(STATUS "  optimization: ${UVIM_OPTIMIZATION_LEVEL}")
uvim_status_bool("minimal vi-style build" UVIM_MINIMAL)
uvim_status_bool("tests" UVIM_BUILD_TESTS)
uvim_status_bool("compile_commands" UVIM_EXPORT_COMPILE_COMMANDS)
uvim_status_bool("auto increment build" UVIM_AUTO_INCREMENT_BUILD)
uvim_status_bool("LTO" UVIM_ENABLE_LTO)
uvim_status_bool("GC sections" UVIM_ENABLE_GC_SECTIONS)
uvim_status_bool("strip binary" UVIM_STRIP_BINARY)
uvim_status_bool("static link" UVIM_STATIC_LINK)
uvim_status_bool("sanitizers" UVIM_ENABLE_SANITIZERS)
uvim_status_bool("debug logging" UVIM_DEBUG_LOGGING)
uvim_status_bool("LSP debug logging" UVIM_DEBUG_LSP)
message(STATUS "uvim compiled features:")
uvim_status_bool("asm docs" UVIM_ENABLE_ASM_DOCS)
uvim_status_bool("browser tools" UVIM_ENABLE_BROWSER_TOOLS)
uvim_status_bool("auxiliary views" UVIM_ENABLE_AUXILIARY_VIEWS)
uvim_status_bool("git tools" UVIM_ENABLE_GIT_TOOLS)
uvim_status_bool("search tools" UVIM_ENABLE_SEARCH_TOOLS)
uvim_status_bool("formatters" UVIM_ENABLE_FORMATTERS)
uvim_status_bool("system clipboard" UVIM_ENABLE_SYSTEM_CLIPBOARD)
uvim_status_bool("struct size popup" UVIM_ENABLE_STRUCT_SIZE_POPUP)
uvim_status_bool("color tools" UVIM_ENABLE_COLOR_TOOLS)
uvim_status_bool("terminal colors" UVIM_ENABLE_TERMINAL_COLORS)
message(STATUS "uvim compiled LSP support:")
uvim_status_bool("any LSP client code" UVIM_ENABLE_ANY_LSP)
uvim_status_bool("clangd" UVIM_ENABLE_CLANGD_LSP)
uvim_status_bool("robot" UVIM_ENABLE_ROBOT_LSP)
uvim_status_bool("python" UVIM_ENABLE_PYTHON_LSP)
uvim_status_bool("mlang" UVIM_ENABLE_MLANG_LSP)
uvim_status_bool("mlang semantic token default" UVIM_MLANG_SEMANTIC_TOKENS_DEFAULT)
uvim_status_bool("html" UVIM_ENABLE_HTML_LSP)
uvim_status_bool("css" UVIM_ENABLE_CSS_LSP)
uvim_status_bool("json" UVIM_ENABLE_JSON_LSP)
uvim_status_bool("typescript/javascript" UVIM_ENABLE_TS_LSP)

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
    UVIM_MLANG_SEMANTIC_TOKENS_DEFAULT=$<IF:$<BOOL:${UVIM_MLANG_SEMANTIC_TOKENS_DEFAULT}>,1,0>
)

if(MSVC)
    target_compile_options(uvim PRIVATE
        $<$<CONFIG:Release>:/O2>
    )
else()
    if(NOT UVIM_OPTIMIZATION_LEVEL MATCHES "^(O0|O1|O2|O3|Os|Oz)$")
        message(FATAL_ERROR "Invalid UVIM_OPTIMIZATION_LEVEL='${UVIM_OPTIMIZATION_LEVEL}'. Use O0, O1, O2, O3, Os, or Oz.")
    endif()
    target_compile_options(uvim PRIVATE
        $<$<CONFIG:Release>:-${UVIM_OPTIMIZATION_LEVEL}>
    )
    if(UVIM_ENABLE_GC_SECTIONS)
        target_compile_options(uvim PRIVATE
            $<$<CONFIG:Release>:-ffunction-sections>
            $<$<CONFIG:Release>:-fdata-sections>
        )
        if(APPLE)
            target_link_options(uvim PRIVATE
                $<$<CONFIG:Release>:-Wl,-dead_strip>
            )
        else()
            target_link_options(uvim PRIVATE
                $<$<CONFIG:Release>:-Wl,--gc-sections>
            )
        endif()
    endif()
endif()

# Enable LTO / IPO
if(UVIM_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    else()
        message(WARNING "UVIM_ENABLE_LTO=ON requested, but IPO is not supported: ${ipo_error}")
    endif()
endif()

if(UVIM_STRIP_BINARY)
    if(CMAKE_STRIP AND NOT MSVC)
        if(APPLE)
            add_custom_command(TARGET uvim POST_BUILD
                COMMAND ${CMAKE_STRIP} -x $<TARGET_FILE:uvim>
                COMMENT "Stripping uvim binary"
            )
        else()
            add_custom_command(TARGET uvim POST_BUILD
                COMMAND ${CMAKE_STRIP} $<TARGET_FILE:uvim>
                COMMENT "Stripping uvim binary"
            )
        endif()
    else()
        message(WARNING "UVIM_STRIP_BINARY=ON requested, but no supported strip tool is available")
    endif()
endif()

# yyjson (JSON parsing)
add_library(yyjson STATIC third_party/yyjson/yyjson.c)
target_include_directories(yyjson PUBLIC ${CMAKE_SOURCE_DIR}/third_party/yyjson)

function(uvim_target_link_yyjson target_name)
    target_link_libraries(${target_name} PRIVATE yyjson)
endfunction()

uvim_target_link_yyjson(uvim)

#fmt (formatting library)
include(FetchContent)
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

# LSP dependencies and compile gates. UVIM_ENABLE_CLANGD_LSP remains the
# shared client gate for older code paths; the per-server definitions gate
# individual integrations.
if(UVIM_ENABLE_ANY_LSP)
    target_compile_definitions(uvim PRIVATE UVIM_ENABLE_CLANGD_LSP=1)
    find_package(Threads REQUIRED)
    target_link_libraries(uvim PRIVATE Threads::Threads fmt::fmt)
endif()

foreach(_uvim_lsp CLANGD ROBOT PYTHON MLANG HTML CSS JSON TS)
    if(UVIM_ENABLE_${_uvim_lsp}_LSP)
        target_compile_definitions(uvim PRIVATE UVIM_ENABLE_${_uvim_lsp}_LSP=1)
    endif()
endforeach()

# Debug logging
if(UVIM_DEBUG_LOGGING)
    target_compile_definitions(uvim PRIVATE UVIM_DEBUG_LOGGING=1)
endif()

# LSP debug logging
if(UVIM_DEBUG_LSP)
    target_compile_definitions(uvim PRIVATE UVIM_DEBUG_LSP=1)
endif()

if(UVIM_MINIMAL)
    target_compile_definitions(uvim PRIVATE UVIM_MINIMAL=1)
endif()
if(UVIM_ENABLE_BROWSER_TOOLS)
    target_compile_definitions(uvim PRIVATE UVIM_ENABLE_BROWSER_TOOLS=1)
endif()
if(UVIM_ENABLE_AUXILIARY_VIEWS)
    target_compile_definitions(uvim PRIVATE UVIM_ENABLE_AUXILIARY_VIEWS=1)
endif()

if(UVIM_ENABLE_ASM_DOCS)
    target_compile_definitions(uvim PRIVATE UVIM_ENABLE_ASM_DOCS=1)
endif()

if(UVIM_ENABLE_GIT_TOOLS)
    target_compile_definitions(uvim PRIVATE UVIM_ENABLE_GIT_TOOLS=1)
endif()

if(UVIM_ENABLE_SEARCH_TOOLS)
    target_compile_definitions(uvim PRIVATE UVIM_ENABLE_SEARCH_TOOLS=1)
endif()

if(UVIM_ENABLE_FORMATTERS)
    target_compile_definitions(uvim PRIVATE UVIM_ENABLE_FORMATTERS=1)
endif()

if(UVIM_ENABLE_SYSTEM_CLIPBOARD)
    target_compile_definitions(uvim PRIVATE UVIM_ENABLE_SYSTEM_CLIPBOARD=1)
endif()

if(UVIM_ENABLE_STRUCT_SIZE_POPUP)
    target_compile_definitions(uvim PRIVATE UVIM_ENABLE_STRUCT_SIZE_POPUP=1)
endif()
if(UVIM_ENABLE_COLOR_TOOLS)
    target_compile_definitions(uvim PRIVATE UVIM_ENABLE_COLOR_TOOLS=1)
endif()
if(UVIM_ENABLE_TERMINAL_COLORS)
    target_compile_definitions(uvim PRIVATE UVIM_ENABLE_TERMINAL_COLORS=1)
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
        yyjson
    )
    target_include_directories(state_machine_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

    add_executable(mode_state_machine_tests
        tests/mode_state_machine_test.cpp
    )
    uvim_enable_sanitizers(mode_state_machine_tests)
    target_link_libraries(mode_state_machine_tests
        GTest::gtest_main
        GTest::gmock
        fmt::fmt
        yyjson
    )
    target_include_directories(mode_state_machine_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

    add_executable(constants_tests
        tests/constants_test.cpp
        src/config_parser.cpp
    )
    uvim_enable_sanitizers(constants_tests)
    target_link_libraries(constants_tests
        GTest::gtest_main
        GTest::gmock
        fmt::fmt
        yyjson
    )
    target_include_directories(constants_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

    add_executable(logging_tests
        tests/logging_test.cpp
    )
    uvim_enable_sanitizers(logging_tests)
    target_link_libraries(logging_tests
        GTest::gtest_main
        fmt::fmt
    )
    target_include_directories(logging_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(logging_tests PRIVATE UVIM_DEBUG_LSP=1)

    set(UVIM_TEST_SOURCES
        ${UVIM_CORE_SOURCES}
        ${MODE_STATE_MACHINE_SOURCES}
        ${UVIM_OPTIONAL_FEATURE_SOURCES}
    )
    list(REMOVE_ITEM UVIM_TEST_SOURCES src/main.cpp)

    set(UVIM_REAL_MODE_TEST_FILES
        tests/editor_alternate_file_test.cpp
        tests/real_mode_assembly_test.cpp
        tests/real_mode_browser_test.cpp
        tests/real_mode_clipboard_paste_test.cpp
        tests/real_mode_comment_test.cpp
        tests/real_mode_completion_format_test.cpp
        tests/real_mode_insert_test.cpp
        tests/real_mode_lsp_rename_test.cpp
        tests/real_mode_search_test.cpp
        tests/real_mode_struct_size_test.cpp
        tests/real_mode_tool_popup_test.cpp
        tests/real_mode_transitions_test.cpp
        tests/real_mode_undo_test.cpp
        tests/real_mode_visual_text_object_test.cpp
        tests/editor_filetype_test.cpp
        tests/syntax_highlighter_test.cpp
        tests/js_ts_resolution_test.cpp
        tests/loc_count_test.cpp
    )
    if(UVIM_ENABLE_GIT_TOOLS)
        list(APPEND UVIM_REAL_MODE_TEST_FILES
            tests/git_log_view_test.cpp
            tests/git_stage_layout_test.cpp
        )
    endif()

    add_executable(real_mode_transitions_tests
        ${UVIM_REAL_MODE_TEST_FILES}
        ${UVIM_TEST_SOURCES}
    )
    uvim_enable_sanitizers(real_mode_transitions_tests)
    target_link_libraries(real_mode_transitions_tests PRIVATE
        GTest::gtest_main
        GTest::gmock
        fmt::fmt
        yyjson
    )
    target_include_directories(real_mode_transitions_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(real_mode_transitions_tests PRIVATE UVIM_TESTING=1)
    target_compile_definitions(real_mode_transitions_tests PRIVATE
        UVIM_MLANG_SEMANTIC_TOKENS_DEFAULT=$<IF:$<BOOL:${UVIM_MLANG_SEMANTIC_TOKENS_DEFAULT}>,1,0>
    )
    if(_backend STREQUAL "WIN32")
        target_compile_definitions(real_mode_transitions_tests PRIVATE UVIM_TERMINAL_WIN32=1)
    else()
        target_compile_definitions(real_mode_transitions_tests PRIVATE UVIM_TERMINAL_POSIX=1)
    endif()
    if(UVIM_ENABLE_ANY_LSP)
        target_compile_definitions(real_mode_transitions_tests PRIVATE UVIM_ENABLE_CLANGD_LSP=1)
        target_link_libraries(real_mode_transitions_tests PRIVATE
            Threads::Threads
        )
    endif()
    foreach(_uvim_lsp CLANGD ROBOT PYTHON MLANG HTML CSS JSON TS)
        if(UVIM_ENABLE_${_uvim_lsp}_LSP)
            target_compile_definitions(real_mode_transitions_tests PRIVATE UVIM_ENABLE_${_uvim_lsp}_LSP=1)
        endif()
    endforeach()
    if(UVIM_ENABLE_ASM_DOCS)
        target_compile_definitions(real_mode_transitions_tests PRIVATE UVIM_ENABLE_ASM_DOCS=1)
    endif()
    if(UVIM_MINIMAL)
        target_compile_definitions(real_mode_transitions_tests PRIVATE UVIM_MINIMAL=1)
    endif()
    if(UVIM_ENABLE_BROWSER_TOOLS)
        target_compile_definitions(real_mode_transitions_tests PRIVATE UVIM_ENABLE_BROWSER_TOOLS=1)
    endif()
    if(UVIM_ENABLE_AUXILIARY_VIEWS)
        target_compile_definitions(real_mode_transitions_tests PRIVATE UVIM_ENABLE_AUXILIARY_VIEWS=1)
    endif()
    if(UVIM_ENABLE_GIT_TOOLS)
        target_compile_definitions(real_mode_transitions_tests PRIVATE UVIM_ENABLE_GIT_TOOLS=1)
    endif()
    if(UVIM_ENABLE_SEARCH_TOOLS)
        target_compile_definitions(real_mode_transitions_tests PRIVATE UVIM_ENABLE_SEARCH_TOOLS=1)
    endif()
    if(UVIM_ENABLE_COLOR_TOOLS)
        target_compile_definitions(real_mode_transitions_tests PRIVATE UVIM_ENABLE_COLOR_TOOLS=1)
    endif()
    if(UVIM_ENABLE_TERMINAL_COLORS)
        target_compile_definitions(real_mode_transitions_tests PRIVATE UVIM_ENABLE_TERMINAL_COLORS=1)
    endif()
    if(UVIM_ENABLE_FORMATTERS)
        target_compile_definitions(real_mode_transitions_tests PRIVATE UVIM_ENABLE_FORMATTERS=1)
    endif()
    if(UVIM_ENABLE_SYSTEM_CLIPBOARD)
        target_compile_definitions(real_mode_transitions_tests PRIVATE UVIM_ENABLE_SYSTEM_CLIPBOARD=1)
    endif()
    if(UVIM_ENABLE_STRUCT_SIZE_POPUP)
        target_compile_definitions(real_mode_transitions_tests PRIVATE UVIM_ENABLE_STRUCT_SIZE_POPUP=1)
    endif()

    include(GoogleTest)
    gtest_discover_tests(state_machine_tests)
    gtest_discover_tests(mode_state_machine_tests)
    gtest_discover_tests(constants_tests)
    gtest_discover_tests(logging_tests)
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
        DEPENDS state_machine_tests mode_state_machine_tests constants_tests logging_tests real_mode_transitions_tests
    )
endif()

# ============================================================================
# Install (binary only)
# ============================================================================
install(CODE "message(STATUS \"Installing uvim v${PROJECT_VERSION} - build ${PROJECT_VERSION_PATCH}\")"
    COMPONENT uvim
)
install(TARGETS uvim
    RUNTIME DESTINATION "${UVIM_INSTALL_BINDIR}"
    COMPONENT uvim
)
