#include "asm_documentation.h"
#include "color_constant.h"
#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#include "widgets/status_bar.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <memory>
#include <utility>

using namespace editor::statemachine;

namespace
{
template <typename InitialState>
ModeStateMachine makeMachine(Editor& editor, InitialState&& initial)
{
    return ModeStateMachine(createModeContext(&editor),
                            std::forward<InitialState>(initial));
}

std::filesystem::path make_temp_dir(const std::string& prefix)
{
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path base =
        std::filesystem::temp_directory_path() / (prefix + std::to_string(now));
    std::filesystem::create_directories(base);
    return base;
}

void write_file(const std::filesystem::path& path, std::string_view content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

void dispatch_command(ModeStateMachine& sm, std::string_view cmd)
{
    sm.dispatch(':');
    for(char c : cmd)
        sm.dispatch(c);
    sm.dispatch(keyCode(control::ControlKey::ENTER));
}

void set_buffer_filename(Editor& editor, std::string filename)
{
    editor.currentBuffer->filename = std::move(filename);
    if(editor.filename)
        *editor.filename = editor.currentBuffer->filename;
}

void set_env_var(const std::string& name, const std::string& value)
{
#ifdef _WIN32
    _putenv_s(name.c_str(), value.c_str());
#else
    setenv(name.c_str(), value.c_str(), 1);
#endif
}

void unset_env_var(const std::string& name)
{
#ifdef _WIN32
    _putenv_s(name.c_str(), "");
#else
    unsetenv(name.c_str());
#endif
}

class ScopedCurrentPath
{
public:
    explicit ScopedCurrentPath(const std::filesystem::path& next)
    {
        std::error_code ec;
        previous = std::filesystem::current_path(ec);
        if(!ec)
            std::filesystem::current_path(next, ec);
    }

    ~ScopedCurrentPath()
    {
        std::error_code ec;
        if(!previous.empty())
            std::filesystem::current_path(previous, ec);
    }

private:
    std::filesystem::path previous;
};

class ScopedEnv
{
public:
    ScopedEnv(const char* name, std::string value) : name(name)
    {
        const char* current = std::getenv(name);
        if(current)
        {
            hadValue = true;
            previous = current;
        }
        set_env_var(name, value);
    }

    ~ScopedEnv()
    {
        if(hadValue)
            set_env_var(name, previous);
        else
            unset_env_var(name);
    }

private:
    std::string name;
    bool hadValue = false;
    std::string previous;
};
} // namespace

TEST(EditorFileControllerTest, FindsSameDirectoryHeaderForSource)
{
    const auto root = make_temp_dir("uvim_alt_same_dir_");
    write_file(root / "CMakeLists.txt",
               "cmake_minimum_required(VERSION 3.20)\n");
    write_file(root / "src" / "cpp_constants.cpp",
               "#include \"cpp_constants.h\"\n");
    write_file(root / "src" / "cpp_constants.h", "#pragma once\n");

    Editor editor = Editor::createForTests();

    EXPECT_EQ(
        editor.findAlternateFile((root / "src" / "cpp_constants.cpp").string()),
        (root / "src" / "cpp_constants.h").string());
}

TEST(EditorFileControllerTest, FindsIncludedHeaderWhenSourceNameDiffers)
{
    const auto root = make_temp_dir("uvim_alt_include_");
    write_file(root / "CMakeLists.txt",
               "cmake_minimum_required(VERSION 3.20)\n");
    write_file(root / "src" / "syntax_tables.cpp",
               "#include \"cpp_constants.h\"\n");
    write_file(root / "src" / "cpp_constants.h", "#pragma once\n");

    Editor editor = Editor::createForTests();

    EXPECT_EQ(
        editor.findAlternateFile((root / "src" / "syntax_tables.cpp").string()),
        (root / "src" / "cpp_constants.h").string());
}

TEST(EditorFileControllerTest, FindsSourceIncludingHeaderWhenNameDiffers)
{
    const auto root = make_temp_dir("uvim_alt_reverse_include_");
    write_file(root / "CMakeLists.txt",
               "cmake_minimum_required(VERSION 3.20)\n");
    write_file(root / "src" / "syntax_tables.cpp",
               "#include \"cpp_constants.h\"\n");
    write_file(root / "src" / "cpp_constants.h", "#pragma once\n");

    Editor editor = Editor::createForTests();

    EXPECT_EQ(
        editor.findAlternateFile((root / "src" / "cpp_constants.h").string()),
        (root / "src" / "syntax_tables.cpp").string());
}

TEST(RealModeTransitionsTest, WelcomeEscStaysInWelcome)
{
    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, WelcomeMode{});

    sm.dispatch(keyCode(control::ControlKey::ESC));

    EXPECT_STREQ(sm.currentStateName(), "WELCOME");
}

TEST(RealModeTransitionsTest, WelcomeLeaderXOpensFileBrowser)
{
    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, WelcomeMode{});

    sm.dispatch(' ');
    sm.dispatch('x');

    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
}

TEST(RealModeTransitionsTest, LeaderXOpensFileBrowserAtCurrentFile)
{
    const auto root = make_temp_dir("uvim_leader_x_current_");
    write_file(root / "alpha.txt", "alpha\n");
    write_file(root / "target.txt", "target\n");
    write_file(root / "zeta.txt", "zeta\n");

    Editor editor = Editor::createForTests();
    editor.openFile((root / "target.txt").string());
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_X));

    ASSERT_STREQ(sm.currentStateName(), "BROWSE");
    auto* fb = sm.getState<FileBrowserMode>();
    ASSERT_NE(fb, nullptr);
    ASSERT_GE(fb->browserCursor, 0);
    ASSERT_LT(fb->browserCursor, static_cast<int>(fb->fileList.size()));
    EXPECT_EQ(fb->fileList[fb->browserCursor].name, "target.txt");
}

TEST(RealModeTransitionsTest, LeaderXXOpensFileBrowserAtDirectoryTop)
{
    const auto root = make_temp_dir("uvim_leader_xx_top_");
    write_file(root / "alpha.txt", "alpha\n");
    write_file(root / "target.txt", "target\n");

    Editor editor = Editor::createForTests();
    editor.openFile((root / "target.txt").string());
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    Terminal::unreadKey(keyCode(typed::TypedKey::KEY_X));
    sm.dispatch(keyCode(typed::TypedKey::KEY_X));

    ASSERT_STREQ(sm.currentStateName(), "BROWSE");
    auto* fb = sm.getState<FileBrowserMode>();
    ASSERT_NE(fb, nullptr);
    ASSERT_FALSE(fb->fileList.empty());
    EXPECT_EQ(fb->browserCursor, 0);
}

TEST(RealModeTransitionsTest, LspInfoQuitWithNoBufferReturnsWelcome)
{
    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, LspInfoMode{});

    sm.dispatch('q');

    EXPECT_STREQ(sm.currentStateName(), "WELCOME");
}

TEST(RealModeTransitionsTest, DoubleEscClearsStatusMessageInWelcome)
{
    Editor editor = Editor::createForTests();
    editor.setStatusMessage("Message");
    auto sm = makeMachine(editor, WelcomeMode{});

    sm.dispatch(keyCode(control::ControlKey::ESC));
    EXPECT_EQ(editor.statusMessage, "Message");

    sm.dispatch(keyCode(control::ControlKey::ESC));
    EXPECT_TRUE(editor.statusMessage.empty());
}

TEST(RealModeTransitionsTest, VisualPasteReplacesSelectionWithYankBuffer)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.useSystemClipboard = false;
    editor.currentBuffer->lines = {"one two three"};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch('v');
    sm.dispatch('l');
    sm.dispatch('l');
    sm.dispatch('y');

    *editor.cursorX = 4;
    *editor.cursorY = 0;

    sm.dispatch('v');
    sm.dispatch('l');
    sm.dispatch('l');
    sm.dispatch('p');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "one one three");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, LeaderBdClosesCurrentBuffer)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.createNewBuffer();
    ASSERT_EQ(editor.buffers.size(), 2u);
    ASSERT_EQ(editor.currentBufferIndex, 1);

    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(' ');
    sm.dispatch('b');
    sm.dispatch('d');

    EXPECT_EQ(editor.buffers.size(), 1u);
    EXPECT_EQ(editor.currentBufferIndex, 0);
    EXPECT_TRUE(editor.commandBuffer.empty());
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, MarkJumpReopensMarkedFile)
{
    auto dir = make_temp_dir("uvim_marks_");
    auto first = dir / "first.txt";
    auto second = dir / "second.txt";
    write_file(first, "one\ntwo\nthree\n");
    write_file(second, "alpha\nbeta\n");

    Editor editor = Editor::createForTests();
    editor.openFile(first.string());
    *editor.cursorY = 2;
    *editor.cursorX = 3;
    editor.setMark('a');

    editor.openFile(second.string());
    ASSERT_EQ(std::filesystem::canonical(second).string(), *editor.filename);

    editor.jumpToMark('a');

    EXPECT_EQ(std::filesystem::canonical(first).string(), *editor.filename);
    EXPECT_EQ(*editor.cursorY, 2);
    EXPECT_EQ(*editor.cursorX, 3);
}

TEST(RealModeTransitionsTest, NormalCtrlJAndCtrlKMoveCurrentLineWithUndoRedo)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"one", "two", "three"};
    set_buffer_filename(editor, "/tmp/uvim_move_line_test.cpp");
    *editor.cursorY = 1;
    *editor.cursorX = 1;
    editor.saveState();
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_EQ(editor.currentBuffer->lines,
              (std::vector<std::string>{"one", "three", "two"}));
    EXPECT_EQ(*editor.cursorY, 2);

    sm.dispatch(keyCode(typed::TypedKey::KEY_U));
    EXPECT_EQ(editor.currentBuffer->lines,
              (std::vector<std::string>{"one", "two", "three"}));
    EXPECT_EQ(*editor.cursorY, 1);

    sm.dispatch(keyCode(control::ControlKey::CTRL_R));
    EXPECT_EQ(editor.currentBuffer->lines,
              (std::vector<std::string>{"one", "three", "two"}));
    EXPECT_EQ(*editor.cursorY, 2);

    sm.dispatch(keyCode(control::ControlKey::CTRL_K));
    EXPECT_EQ(editor.currentBuffer->lines,
              (std::vector<std::string>{"one", "two", "three"}));
    EXPECT_EQ(*editor.cursorY, 1);
}

TEST(RealModeTransitionsTest, VisualLineCtrlJAndCtrlKMoveSelection)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"a", "b", "c", "d", "e"};
    set_buffer_filename(editor, "/tmp/uvim_move_visual_line_test.cpp");
    *editor.cursorY = 1;
    *editor.cursorX = 0;
    editor.saveState();
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_CAP_V));
    sm.dispatch(keyCode(typed::TypedKey::KEY_J));
    ASSERT_STREQ(sm.currentStateName(), "VISUAL LINE");
    ASSERT_EQ(editor.currentBuffer->visualStartY, 1);
    ASSERT_EQ(editor.currentBuffer->visualEndY, 2);

    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_EQ(editor.currentBuffer->lines,
              (std::vector<std::string>{"a", "d", "b", "c", "e"}));
    EXPECT_EQ(editor.currentBuffer->visualStartY, 2);
    EXPECT_EQ(editor.currentBuffer->visualEndY, 3);
    EXPECT_EQ(*editor.cursorY, 3);
    EXPECT_STREQ(sm.currentStateName(), "VISUAL LINE");

    sm.dispatch(keyCode(control::ControlKey::CTRL_K));
    EXPECT_EQ(editor.currentBuffer->lines,
              (std::vector<std::string>{"a", "b", "c", "d", "e"}));
    EXPECT_EQ(editor.currentBuffer->visualStartY, 1);
    EXPECT_EQ(editor.currentBuffer->visualEndY, 2);
    EXPECT_EQ(*editor.cursorY, 2);
    EXPECT_STREQ(sm.currentStateName(), "VISUAL LINE");
}

TEST(RealModeTransitionsTest, VisualLineMoveCanUndoAndRedo)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"a", "b", "c", "d"};
    set_buffer_filename(editor, "/tmp/uvim_move_visual_line_undo_test.cpp");
    *editor.cursorY = 1;
    *editor.cursorX = 0;
    editor.saveState();
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_CAP_V));
    sm.dispatch(keyCode(typed::TypedKey::KEY_J));
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_EQ(editor.currentBuffer->lines,
              (std::vector<std::string>{"a", "d", "b", "c"}));

    sm.dispatch(keyCode(control::ControlKey::ESC));
    sm.dispatch(keyCode(typed::TypedKey::KEY_U));
    EXPECT_EQ(editor.currentBuffer->lines,
              (std::vector<std::string>{"a", "b", "c", "d"}));

    sm.dispatch(keyCode(control::ControlKey::CTRL_R));
    EXPECT_EQ(editor.currentBuffer->lines,
              (std::vector<std::string>{"a", "d", "b", "c"}));
}

TEST(RealModeTransitionsTest, UndoToOldestChangeKeepsFirstEditedLine)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"one", "two", "three", "four"};
    set_buffer_filename(editor, "/tmp/uvim_undo_oldest_cursor_test.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 0;
    editor.saveState();
    editor.currentBuffer->savedUndoIndex = editor.currentBuffer->undoIndex;

    *editor.cursorY = 2;
    *editor.cursorX = 1;
    editor.currentBuffer->lines[2] = "changed";
    editor.saveState();

    editor.undo();
    EXPECT_EQ(editor.currentBuffer->lines,
              (std::vector<std::string>{"one", "two", "three", "four"}));
    EXPECT_EQ(*editor.cursorY, 2);
    EXPECT_EQ(*editor.cursorX, 1);
}

TEST(RealModeTransitionsTest, EmitAsmCommandCreatesAssemblyBufferWithFlags)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"#ifndef __OPTIMIZE__",
                                   "#error expected optimization",
                                   "#endif",
                                   "int square(int value)",
                                   "{",
                                   "    return value * value;",
                                   "}"};
    set_buffer_filename(editor, "/tmp/uvim_emit_asm_test.cpp");
    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "emitasm -O2");

    ASSERT_TRUE(editor.currentBuffer);
    EXPECT_TRUE(text_utils::is_found(
        editor.currentBuffer->filename.find("uvim_emit_asm_test.cpp.s")));
    EXPECT_FALSE(editor.currentBuffer->dirty);
    EXPECT_TRUE(editor.isFileType<FileType::Asm>());
    std::string output;
    for(const std::string& line : editor.currentBuffer->lines)
    {
        output += line;
        output += '\n';
    }
    EXPECT_TRUE(text_utils::is_found(output.find("square")));
    EXPECT_TRUE(text_utils::is_not_found(output.find("uvim_emit_asm_anchor")));
    EXPECT_EQ(editor.statusMessage, "emitasm: wrote assembly buffer");
}

TEST(RealModeTransitionsTest, EmitAsmCommandAnchorsHeaderTypes)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"#include <string>",
                                   "struct HeaderType",
                                   "{",
                                   "    HeaderType() : value(\"ok\") {}",
                                   "    std::string value;",
                                   "};"};
    set_buffer_filename(editor, "/tmp/uvim_emit_asm_header_test.h");
    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "emitasm -O2");

    ASSERT_TRUE(editor.currentBuffer);
    std::string output;
    for(const std::string& line : editor.currentBuffer->lines)
    {
        output += line;
        output += '\n';
    }
    EXPECT_TRUE(text_utils::is_found(output.find("uvim_emit_asm_anchor")));
    EXPECT_TRUE(text_utils::is_found(
        editor.currentBuffer->filename.find("uvim_emit_asm_header_test.h.s")));
    EXPECT_TRUE(editor.isFileType<FileType::Asm>());
}

TEST(RealModeTransitionsTest, EmitAsmCommandAnchorsDeclarationOnlyCppSource)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"struct SomeStruct", "{",
                                   "    int value = 42;", "};"};
    set_buffer_filename(editor, "/tmp/uvim_emit_asm_struct_only.cpp");
    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "emitasm -O2");

    ASSERT_TRUE(editor.currentBuffer);
    std::string output;
    for(const std::string& line : editor.currentBuffer->lines)
    {
        output += line;
        output += '\n';
    }
    EXPECT_TRUE(text_utils::is_found(output.find("uvim_emit_asm_anchor")));
    EXPECT_TRUE(text_utils::is_found(editor.currentBuffer->filename.find(
        "uvim_emit_asm_struct_only.cpp.s")));
    EXPECT_TRUE(editor.isFileType<FileType::Asm>());
}

TEST(RealModeTransitionsTest, EmitAsmCommandAnchorsStandaloneFunction)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"static inline int add(int a, int b)", "{",
                                   "    return a + b;", "}"};
    set_buffer_filename(editor, "/tmp/uvim_emit_asm_function_only.cpp");
    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "emitasm -O2");

    ASSERT_TRUE(editor.currentBuffer);
    std::string output;
    for(const std::string& line : editor.currentBuffer->lines)
    {
        output += line;
        output += '\n';
    }
    EXPECT_TRUE(text_utils::is_found(output.find("add")));
    EXPECT_TRUE(
        text_utils::is_not_found(output.find("uvim_emit_asm_function_anchor")));
    EXPECT_TRUE(text_utils::is_not_found(output.find(".build_version")));
    EXPECT_TRUE(text_utils::is_found(editor.currentBuffer->filename.find(
        "uvim_emit_asm_function_only.cpp.s")));
    EXPECT_TRUE(editor.isFileType<FileType::Asm>());
}

TEST(RealModeTransitionsTest, EmitAsmCommandRawOutputKeepsClangAssembler)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int add(int a, int b)", "{",
                                   "    return a + b;", "}"};
    set_buffer_filename(editor, "/tmp/uvim_emit_asm_raw.cpp");
    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "emitasm --raw -O2");

    ASSERT_TRUE(editor.currentBuffer);
    std::string output;
    for(const std::string& line : editor.currentBuffer->lines)
    {
        output += line;
        output += '\n';
    }
    EXPECT_TRUE(text_utils::is_found(output.find("add")));
    EXPECT_TRUE(text_utils::is_found(output.find(".globl")) ||
                text_utils::is_found(output.find(".global")) ||
                text_utils::is_found(output.find("__TEXT,__text")) ||
                text_utils::is_found(output.find(".text")));
    EXPECT_TRUE(editor.isFileType<FileType::Asm>());
}

#ifdef UVIM_ENABLE_ASM_DOCS
TEST(RealModeTransitionsTest, GdOnX86AssemblyInstructionOpensDocs)
{
    auto docsRoot = make_temp_dir("uvim_asm_docs_");
    set_env_var("UVIM_ASM_DOCS_CACHE_DIR", docsRoot.string());

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    movq %rdi, %rax"};
    set_buffer_filename(editor, "/tmp/uvim_asm_doc_test.s");
    *editor.cursorY = 0;
    *editor.cursorX = 4;
    editor.goToDefinition();

    ASSERT_TRUE(editor.currentBuffer);
    EXPECT_TRUE(
        text_utils::is_found(editor.currentBuffer->filename.find("x86.md")));
    ASSERT_GE(*editor.cursorY, 0);
    ASSERT_LT(*editor.cursorY, (int)editor.currentBuffer->lines.size());
    EXPECT_EQ(editor.currentBuffer->lines[*editor.cursorY], "## mov");
    std::string output;
    for(const std::string& line : editor.currentBuffer->lines)
    {
        output += line;
        output += '\n';
    }
    EXPECT_TRUE(text_utils::is_found(
        output.find("Documentation: Copies the source operand")));
    EXPECT_TRUE(
        text_utils::is_found(editor.statusMessage.find("gd (asm x86)")));

    unset_env_var("UVIM_ASM_DOCS_CACHE_DIR");
}

TEST(RealModeTransitionsTest, GdOnAarch64AssemblyInstructionOpensDocs)
{
    auto docsRoot = make_temp_dir("uvim_asm_docs_");
    set_env_var("UVIM_ASM_DOCS_CACHE_DIR", docsRoot.string());

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    ldr x0, [sp]"};
    set_buffer_filename(editor, "/tmp/uvim_asm_doc_test.s");
    *editor.cursorY = 0;
    *editor.cursorX = 4;
    editor.goToDefinition();

    ASSERT_TRUE(editor.currentBuffer);
    EXPECT_TRUE(text_utils::is_found(
        editor.currentBuffer->filename.find("aarch64.md")));
    ASSERT_GE(*editor.cursorY, 0);
    ASSERT_LT(*editor.cursorY, (int)editor.currentBuffer->lines.size());
    EXPECT_EQ(editor.currentBuffer->lines[*editor.cursorY], "## ldr");
    EXPECT_TRUE(
        text_utils::is_found(editor.statusMessage.find("gd (asm aarch64)")));

    unset_env_var("UVIM_ASM_DOCS_CACHE_DIR");
}

TEST(RealModeTransitionsTest, GdOnAssemblyInstructionCanFetchOriginalDocs)
{
#ifdef _WIN32
    GTEST_SKIP() << "fake curl PATH test is POSIX-only";
#else
    auto root = make_temp_dir("uvim_asm_docs_fetch_");
    auto docsRoot = root / "cache";
    auto binDir = root / "bin";
    std::filesystem::create_directories(binDir);
    auto curlPath = binDir / "curl";
    write_file(curlPath,
               "#!/bin/sh\n"
               "printf '%s\\n' '{\"html\":\"<p>Compiler Explorer move "
               "documentation.</p>\",\"tooltip\":\"Move data\","
               "\"url\":\"https://www.felixcloutier.com/x86/mov\"}'\n");
    std::filesystem::permissions(curlPath,
                                 std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add);

    const char* oldPathEnv = std::getenv("PATH");
    std::string oldPath = oldPathEnv ? oldPathEnv : "";
    set_env_var("PATH", binDir.string() + ":" + oldPath);
    set_env_var("UVIM_ASM_DOCS_CACHE_DIR", docsRoot.string());
    asm_documentation::setFetchOriginalDocs(true);

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    movq %rdi, %rax"};
    set_buffer_filename(editor, "/tmp/uvim_asm_doc_fetch_test.s");
    *editor.cursorY = 0;
    *editor.cursorX = 4;
    editor.goToDefinition();

    ASSERT_TRUE(editor.currentBuffer);
    EXPECT_TRUE(text_utils::is_found(editor.currentBuffer->filename.find(
        "fetched/compiler-explorer/x86/mov.md")));
    std::string output;
    for(const std::string& line : editor.currentBuffer->lines)
    {
        output += line;
        output += '\n';
    }
    EXPECT_TRUE(
        text_utils::is_found(output.find("Compiler Explorer documentation:")));
    EXPECT_TRUE(text_utils::is_found(
        output.find("Compiler Explorer move documentation.")));
    EXPECT_TRUE(text_utils::is_found(
        output.find("API: https://godbolt.org/api/asm/amd64/mov")));

    asm_documentation::setFetchOriginalDocs(false);
    set_env_var("PATH", oldPath);
    unset_env_var("UVIM_ASM_DOCS_CACHE_DIR");
#endif
}

TEST(RealModeTransitionsTest, LeaderGaShowsAssemblyDocsPopup)
{
    auto docsRoot = make_temp_dir("uvim_asm_docs_popup_");
    set_env_var("UVIM_ASM_DOCS_CACHE_DIR", docsRoot.string());
    asm_documentation::setFetchOriginalDocs(false);

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    movq %rdi, %rax"};
    set_buffer_filename(editor, "/tmp/uvim_asm_doc_popup_test.s");
    *editor.cursorY = 0;
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    Terminal::unreadKey('a');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_TRUE(editor.symbolPopupModal);
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_NE(editor.symbolPopupText.find("## mov"), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("Documentation: Copies the source"),
              std::string::npos);

    unset_env_var("UVIM_ASM_DOCS_CACHE_DIR");
}

TEST(RealModeTransitionsTest, LeaderGaAssemblyDocsPopupScrollsAndCloses)
{
    auto docsRoot = make_temp_dir("uvim_asm_docs_popup_scroll_");
    set_env_var("UVIM_ASM_DOCS_CACHE_DIR", docsRoot.string());
    asm_documentation::setFetchOriginalDocs(false);

    Editor editor = Editor::createForTests();
    editor.screenRows = 5;
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    movq %rdi, %rax"};
    set_buffer_filename(editor, "/tmp/uvim_asm_doc_popup_scroll_test.s");
    *editor.cursorY = 0;
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    Terminal::unreadKey('a');
    sm.dispatch('g');
    ASSERT_TRUE(editor.symbolPopupActive);

    editor.needsFullRedraw = false;
    sm.dispatch('j');
    EXPECT_GT(editor.symbolPopupScroll, 0);
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_TRUE(editor.needsFullRedraw);

    sm.dispatch('q');
    EXPECT_FALSE(editor.symbolPopupActive);
    EXPECT_FALSE(editor.symbolPopupModal);

    unset_env_var("UVIM_ASM_DOCS_CACHE_DIR");
}
#endif

TEST(RealModeTransitionsTest, GsShowsStructSizePopup)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"struct Foo { int a; char b; };",
                                   "int main(){", "    Foo value{};",
                                   "    return value.a;", "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 7;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_TRUE(editor.symbolPopupModal);
    EXPECT_EQ(editor.symbolPopupText,
              "sizeof(Foo) = 8 bytes\na: 4 bytes\nb: 1 bytes");
}

TEST(RealModeTransitionsTest, GsShowsLocalVariableSizePopup)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"struct Foo { int a; char b; };",
                                   "int main(){", "    Foo value{};",
                                   "    return value.a;", "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 3;
    *editor.cursorX = 11;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_TRUE(editor.symbolPopupModal);
    EXPECT_EQ(editor.symbolPopupText,
              "sizeof(value) = 8 bytes\na: 4 bytes\nb: 1 bytes");
}

TEST(RealModeTransitionsTest, GsResolvesQualifiedTemplateTypeAtCursor)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"#include <optional>", "int main(){",
                                   "    std::optional<int> value{};",
                                   "    return value.value_or(0);", "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 2;
    *editor.cursorX = 9;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_TRUE(editor.symbolPopupModal);
    EXPECT_NE(editor.symbolPopupText.find("sizeof(std::optional<int>) = "),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find(" bytes"), std::string::npos);
    EXPECT_EQ(editor.symbolPopupText.find("?"), std::string::npos);
}

TEST(RealModeTransitionsTest, GsOmitsMembersWithUnresolvedSize)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"struct Flags { unsigned a:3; int b; };",
                                   "int main(){", "    Flags value{};",
                                   "    return value.b;", "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 7;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_NE(editor.symbolPopupText.find("sizeof(Flags) = "),
              std::string::npos);
    EXPECT_EQ(editor.symbolPopupText.find("a: ?"), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("b: 4 bytes"), std::string::npos);
}

TEST(RealModeTransitionsTest, GsShowsPaddingBetweenMembers)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"struct Padded { char a; int b; char c; };",
                                   "int main(){", "    Padded value{};",
                                   "    return value.b;", "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 7;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_EQ(editor.symbolPopupText,
              "sizeof(Padded) = 12 bytes\na: 1 bytes\npad: 3 bytes\nb: 4 "
              "bytes\nc: 1 bytes");
}

TEST(RealModeTransitionsTest, GsShowsMembersForStructWithMethods)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"#include <string>",
                                   "#include <unordered_set>",
                                   "#include <vector>",
                                   "struct CommandOutputMode",
                                   "{",
                                   "    static constexpr const char* name()",
                                   "    {",
                                   "        return \"RUN\";",
                                   "    }",
                                   "",
                                   "    std::string command;",
                                   "    std::vector<std::string> lines;",
                                   "    int cursor = 0;",
                                   "    int offset = 0;",
                                   "    bool visualMode = false;",
                                   "    std::unordered_set<int> selectedLines;",
                                   "",
                                   "private:",
                                   "    int contentRows() const;",
                                   "};"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 3;
    *editor.cursorX = 8;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_NE(editor.symbolPopupText.find("sizeof(CommandOutputMode) = "),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("command: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("lines: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("cursor: 4 bytes"),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("offset: 4 bytes"),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("visualMode: 1 bytes"),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("selectedLines: "),
              std::string::npos);
    EXPECT_EQ(editor.symbolPopupText.find("contentRows"), std::string::npos);
}

TEST(RealModeTransitionsTest, GsShowsMultipleNestedStructMembers)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"#include <string>",
                                   "#include <vector>",
                                   "struct BufferLike",
                                   "{",
                                   "    struct BlameEntry",
                                   "    {",
                                   "        std::string hash;",
                                   "        std::string author;",
                                   "        bool valid = false;",
                                   "    };",
                                   "",
                                   "    struct EditState",
                                   "    {",
                                   "        std::vector<std::string> lines;",
                                   "        int cursorX = 0;",
                                   "        int cursorY = 0;",
                                   "        BlameEntry current;",
                                   "    };",
                                   "",
                                   "    BlameEntry blame;",
                                   "    EditState state;",
                                   "    int index = 0;",
                                   "};"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 2;
    *editor.cursorX = 8;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_NE(editor.symbolPopupText.find("sizeof(BufferLike) = "),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("BlameEntry: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("  hash: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("  author: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("  valid: 1 bytes"),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("EditState: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("  cursorX: 4 bytes"),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("  cursorY: 4 bytes"),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("  current: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("    hash: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("blame: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("state: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("index: 4 bytes"), std::string::npos);
}

TEST(RealModeTransitionsTest, GsExpandsMembersFromLocalIncludedHeaders)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    const std::filesystem::path root = make_temp_dir("uvim_gs_include_");
    write_file(root / "syntax_state.h", "#pragma once\n"
                                        "struct CppMethodScanState\n"
                                        "{\n"
                                        "    bool inBlockComment = false;\n"
                                        "    bool inMethod = false;\n"
                                        "    int braceDepth = 0;\n"
                                        "};\n");

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"#include \"syntax_state.h\"",
                                   "struct BufferLike",
                                   "{",
                                   "    struct SyntaxCacheLine",
                                   "    {",
                                   "        bool valid = false;",
                                   "        CppMethodScanState methodState;",
                                   "    };",
                                   "",
                                   "    SyntaxCacheLine cache;",
                                   "};"};
    set_buffer_filename(editor, (root / "buffer_like.h").string());
    *editor.cursorY = 1;
    *editor.cursorX = 8;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_NE(editor.symbolPopupText.find("SyntaxCacheLine: "),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("  methodState: "),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("    inBlockComment: 1 bytes"),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("    inMethod: 1 bytes"),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("    braceDepth: 4 bytes"),
              std::string::npos);
}

TEST(RealModeTransitionsTest, GsShowsMarkerWhenNestedStructLimitIsReached)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"struct Outer",
                                   "{",
                                   "    struct Level1",
                                   "    {",
                                   "        struct Level2",
                                   "        {",
                                   "            struct Level3",
                                   "            {",
                                   "                struct Level4",
                                   "                {",
                                   "                    int tooDeep = 0;",
                                   "                };",
                                   "                Level4 next;",
                                   "            };",
                                   "            Level3 next;",
                                   "        };",
                                   "        Level2 next;",
                                   "    };",
                                   "    Level1 next;",
                                   "};"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 8;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_NE(editor.symbolPopupText.find("Level1: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("  Level2: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("    Level3: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("      Level4: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("        ..."), std::string::npos);
    EXPECT_EQ(editor.symbolPopupText.find("tooDeep: "), std::string::npos);
}

TEST(RealModeTransitionsTest, GsSizePopupIsModalAndClosesWithQ)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"struct Foo { int a; char b; };",
                                   "int main(){", "    Foo value{};",
                                   "    return value.a;", "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 7;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');
    ASSERT_TRUE(editor.symbolPopupActive);
    editor.needsFullRedraw = false;

    sm.dispatch('j');
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_FALSE(editor.needsFullRedraw);

    sm.dispatch('q');
    EXPECT_FALSE(editor.symbolPopupActive);
    EXPECT_FALSE(editor.symbolPopupModal);
    EXPECT_TRUE(editor.needsFullRedraw);
}

TEST(RealModeTransitionsTest, GsSizePopupScrollsWithJAndK)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int value;"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    editor.screenRows = 5;
    editor.symbolPopupActive = true;
    editor.symbolPopupModal = true;
    editor.symbolPopupText = "total\na\nb\nc\nd";
    editor.symbolPopupCursorX = 0;
    editor.symbolPopupCursorY = 0;
    *editor.cursorX = 0;
    *editor.cursorY = 0;
    auto sm = makeMachine(editor, NormalMode{});

    editor.needsFullRedraw = false;
    sm.dispatch('j');
    EXPECT_EQ(editor.symbolPopupScroll, 1);
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_TRUE(editor.needsFullRedraw);

    editor.needsFullRedraw = false;
    sm.dispatch('j');
    sm.dispatch('j');
    EXPECT_EQ(editor.symbolPopupScroll, 2);
    EXPECT_TRUE(editor.needsFullRedraw);

    editor.needsFullRedraw = false;
    sm.dispatch('k');
    EXPECT_EQ(editor.symbolPopupScroll, 1);
    EXPECT_TRUE(editor.needsFullRedraw);
}

TEST(RealModeTransitionsTest, GsSizePopupClosesWithGs)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"struct Foo { int a; char b; };",
                                   "int main(){", "    Foo value{};",
                                   "    return value.a;", "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 7;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');
    ASSERT_TRUE(editor.symbolPopupActive);
    editor.needsFullRedraw = false;

    Terminal::unreadKey('s');
    sm.dispatch('g');
    EXPECT_FALSE(editor.symbolPopupActive);
    EXPECT_FALSE(editor.symbolPopupModal);
    EXPECT_TRUE(editor.needsFullRedraw);
}

TEST(RealModeTransitionsTest, InsertModeAutoPairsDoubleQuote)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {""};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('i');
    sm.dispatch('"');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "\"\"");
    EXPECT_EQ(*editor.cursorX, 1);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");

    sm.dispatch('"');
    EXPECT_EQ(editor.currentBuffer->lines[0], "\"\"");
    EXPECT_EQ(*editor.cursorX, 2);
}

TEST(RealModeTransitionsTest, InsertModeAutoPairsSingleQuote)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {""};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('i');
    sm.dispatch('\'');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "''");
    EXPECT_EQ(*editor.cursorX, 1);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");

    sm.dispatch('\'');
    EXPECT_EQ(editor.currentBuffer->lines[0], "''");
    EXPECT_EQ(*editor.cursorX, 2);
}

TEST(RealModeTransitionsTest, InsertModeAutoPairsBacktick)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {""};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('i');
    sm.dispatch('`');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "``");
    EXPECT_EQ(*editor.cursorX, 1);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");

    sm.dispatch('`');
    EXPECT_EQ(editor.currentBuffer->lines[0], "``");
    EXPECT_EQ(*editor.cursorX, 2);
}

TEST(RealModeTransitionsTest, InsertModeAutoQuotesDisabled)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.autoQuotes = false;
    editor.currentBuffer->lines = {""};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('i');
    sm.dispatch('"');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "\"");
    EXPECT_EQ(*editor.cursorX, 1);
}

TEST(RealModeTransitionsTest, InsertModeAutoBracesInStrings)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {""};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('i');
    sm.dispatch('"');
    sm.dispatch('{');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "\"{}\"");
    EXPECT_EQ(*editor.cursorX, 2);
}

TEST(RealModeTransitionsTest, InsertModeAutoBracesInStringsDisabled)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.autoBracesInStrings = false;
    editor.currentBuffer->lines = {""};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('i');
    sm.dispatch('"');
    sm.dispatch('{');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "\"{\"");
    EXPECT_EQ(*editor.cursorX, 2);
}

TEST(RealModeTransitionsTest, EmojiAcceptInNormalModeStaysNormal)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"ab"};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    Editor::EmojiPopupEntry entry;
    entry.emoji = "\xF0\x9F\x98\x80"; // 😀
    entry.emojiDisplay = entry.emoji;
    entry.name = "grinning_face";
    entry.label = entry.emoji + " " + entry.name;
    editor.emojiEntries = {entry};
    editor.emojiFiltered = {0};
    editor.emojiSelected = 0;
    editor.emojiPopupActive = true;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0],
              std::string("a") + "\xF0\x9F\x98\x80" + "b");
    EXPECT_EQ(*editor.cursorX, 1);
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, EscFromInsertAfterEmojiMovesToUtf8Boundary)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {std::string("a") + "\xF0\x9F\x98\x80" + "b"};
    *editor.cursorX = 5; // between emoji and 'b'
    *editor.cursorY = 0;
    editor.utf8Mode = true;

    auto sm = makeMachine(editor, InsertMode{});
    sm.dispatch(keyCode(control::ControlKey::ESC));

    EXPECT_EQ(*editor.cursorX, 1); // emoji start, not inside UTF-8 bytes
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, AutoBraceInsertUsesIndentWidth)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"fn main()"};
    set_buffer_filename(editor, "main.mla");
    editor.currentBuffer->clangIndentWidthValid = true;
    editor.currentBuffer->clangIndentWidth = 4;
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('A');
    sm.dispatch('{');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "fn main(){");
    EXPECT_EQ(editor.currentBuffer->lines[1], "    ");
    EXPECT_EQ(editor.currentBuffer->lines[2], "}");
    EXPECT_EQ(*editor.cursorY, 1);
    EXPECT_EQ(*editor.cursorX, 4);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, EnterAfterCppBraceJumpsToColumn4)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.autoBraces = false;
    editor.currentBuffer->lines = {"fn main() {"};
    set_buffer_filename(editor, "main.cpp");
    editor.currentBuffer->clangIndentWidthValid = true;
    editor.currentBuffer->clangIndentWidth = 4;
    *editor.cursorX = (int)editor.currentBuffer->lines[0].size();
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, InsertMode{});
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 2u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "fn main() {");
    EXPECT_EQ(editor.currentBuffer->lines[1], "    ");
    EXPECT_EQ(*editor.cursorY, 1);
    EXPECT_EQ(*editor.cursorX, 4);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, LeaderCiTogglesCppLineCommentOnCurrentLine)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int value = 1;"};
    set_buffer_filename(editor, "main.cpp");
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "// int value = 1;");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, LeaderCiAppliesWithoutEnter)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int value = 1;"};
    set_buffer_filename(editor, "main.cpp");
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "// int value = 1;");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, LeaderCiUndoRestoresCursorRow)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int first = 1;", "int second = 2;",
                                   "int third = 3;"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorY = 2;
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));

    EXPECT_EQ(editor.currentBuffer->lines[2], "// int third = 3;");

    editor.undo();

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[2], "int third = 3;");
    EXPECT_EQ(*editor.cursorY, 2);
    EXPECT_EQ(*editor.cursorX, 4);
}

TEST(RealModeTransitionsTest, LeaderCiiWrapsCppCurrentLineWithBlockRows)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    int value = 1;"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "    /*");
    EXPECT_EQ(editor.currentBuffer->lines[1], "    int value = 1;");
    EXPECT_EQ(editor.currentBuffer->lines[2], "    */");
    EXPECT_EQ(*editor.cursorY, 1);

    editor.undo();

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "    int value = 1;");
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_EQ(*editor.cursorX, 4);
}

TEST(RealModeTransitionsTest, LeaderCiiAppliesWithoutEnter)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    int value = 1;"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "    /*");
    EXPECT_EQ(editor.currentBuffer->lines[1], "    int value = 1;");
    EXPECT_EQ(editor.currentBuffer->lines[2], "    */");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, LeaderCiiUndoRestoresCursorRow)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int first = 1;", "int second = 2;",
                                   "    int third = 3;"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorY = 2;
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 5u);

    editor.undo();

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[2], "    int third = 3;");
    EXPECT_EQ(*editor.cursorY, 2);
    EXPECT_EQ(*editor.cursorX, 4);
}

TEST(RealModeTransitionsTest, LeaderCitInsertsTodoLineComment)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    int value = 1;"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_T));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 2u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "    // TODO: ");
    EXPECT_EQ(editor.currentBuffer->lines[1], "    int value = 1;");
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_EQ(*editor.cursorX, 13);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, LeaderCiitInsertsTodoBlockComment)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    int value = 1;"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_T));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "    /** TODO: ");
    EXPECT_EQ(editor.currentBuffer->lines[1], "     */");
    EXPECT_EQ(editor.currentBuffer->lines[2], "    int value = 1;");
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_EQ(*editor.cursorX, 14);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, LeaderCiitTypedCharsEnterInsertMode)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    int value = 1;"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(' ');
    sm.dispatch('c');
    sm.dispatch('i');
    sm.dispatch('i');
    sm.dispatch('t');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "    /** TODO: ");
    EXPECT_EQ(editor.currentBuffer->lines[1], "     */");
    EXPECT_EQ(editor.currentBuffer->lines[2], "    int value = 1;");
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_EQ(*editor.cursorX, 14);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, LeaderCiiWaitsForTodoSuffix)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    int value = 1;"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "    /*");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");

    sm.dispatch(keyCode(typed::TypedKey::KEY_T));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "    /** TODO: ");
    EXPECT_EQ(editor.currentBuffer->lines[1], "     */");
    EXPECT_EQ(editor.currentBuffer->lines[2], "    int value = 1;");
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_EQ(*editor.cursorX, 14);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, LeaderCiitUndoRestoresCursorRow)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int first = 1;", "int second = 2;",
                                   "    int third = 3;"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorY = 2;
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_T));

    EXPECT_STREQ(sm.currentStateName(), "INSERT");

    editor.undo();

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[2], "    int third = 3;");
    EXPECT_EQ(*editor.cursorY, 2);
    EXPECT_EQ(*editor.cursorX, 4);
}

TEST(RealModeTransitionsTest, LeaderCommentPendingEscReturnsToNormalAfterCi)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int value = 1;"};
    set_buffer_filename(editor, "main.cpp");
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(control::ControlKey::ESC));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "// int value = 1;");
    EXPECT_TRUE(editor.commandBuffer.empty());
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, VisualLineLeaderCommentPendingEscStaysVisualLine)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int one = 1;", "int two = 2;"};
    set_buffer_filename(editor, "main.cpp");
    auto sm = makeMachine(editor, VisualLineMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_J));
    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(control::ControlKey::ESC));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 2u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "int one = 1;");
    EXPECT_EQ(editor.currentBuffer->lines[1], "int two = 2;");
    EXPECT_STREQ(sm.currentStateName(), "VISUAL LINE");
}

TEST(RealModeTransitionsTest, VisualLineLeaderCitInsertsTodoLineComment)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    int one = 1;", "    int two = 2;"};
    set_buffer_filename(editor, "main.cpp");
    auto sm = makeMachine(editor, VisualLineMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_J));
    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_T));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "    // TODO: ");
    EXPECT_EQ(editor.currentBuffer->lines[1], "    int one = 1;");
    EXPECT_EQ(editor.currentBuffer->lines[2], "    int two = 2;");
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_EQ(*editor.cursorX, 13);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, VisualLineLeaderCiiWrapsCppRangeWithBlockRows)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    int one = 1;", "    int two = 2;"};
    set_buffer_filename(editor, "main.cpp");
    auto sm = makeMachine(editor, VisualLineMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_J));
    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 4u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "    /*");
    EXPECT_EQ(editor.currentBuffer->lines[1], "    int one = 1;");
    EXPECT_EQ(editor.currentBuffer->lines[2], "    int two = 2;");
    EXPECT_EQ(editor.currentBuffer->lines[3], "    */");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, VisualLineLeaderCiitInsertsTodoBlockComment)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    int one = 1;", "    int two = 2;"};
    set_buffer_filename(editor, "main.cpp");
    auto sm = makeMachine(editor, VisualLineMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_J));
    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_T));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 4u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "    /** TODO: ");
    EXPECT_EQ(editor.currentBuffer->lines[1], "    int one = 1;");
    EXPECT_EQ(editor.currentBuffer->lines[2], "    int two = 2;");
    EXPECT_EQ(editor.currentBuffer->lines[3], "     */");
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_EQ(*editor.cursorX, 14);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, VisualLeaderCiiWrapsOnlySelectedText)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"foo(one, two);"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, VisualMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_L));
    sm.dispatch(keyCode(typed::TypedKey::KEY_L));
    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "foo(/* one */, two);");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, VisualInnerParenSelectsTextObject)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"foo(one, two);"};
    *editor.cursorX = 5;
    auto sm = makeMachine(editor, VisualMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(command::CommandKey::KEY_RIGHT_PAREN));

    EXPECT_STREQ(sm.currentStateName(), "VISUAL");
    EXPECT_EQ(editor.currentBuffer->visualStartX, 4);
    EXPECT_EQ(editor.currentBuffer->visualEndX, 11);
    EXPECT_EQ(*editor.cursorX, 11);
}

TEST(RealModeTransitionsTest, VisualInnerQuoteSelectsTextObject)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"auto s = \"hello\";"};
    *editor.cursorX = 11;
    auto sm = makeMachine(editor, VisualMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(command::CommandKey::KEY_DOUBLE_QUOTE));

    EXPECT_STREQ(sm.currentStateName(), "VISUAL");
    EXPECT_EQ(editor.currentBuffer->visualStartX, 10);
    EXPECT_EQ(editor.currentBuffer->visualEndX, 14);
    EXPECT_EQ(*editor.cursorX, 14);
}

TEST(RealModeTransitionsTest, VisualInnerSingleQuoteSelectsTextObject)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"auto c = 'x';"};
    *editor.cursorX = 10;
    auto sm = makeMachine(editor, VisualMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(command::CommandKey::KEY_APOSTROPHE));

    EXPECT_STREQ(sm.currentStateName(), "VISUAL");
    EXPECT_EQ(editor.currentBuffer->visualStartX, 10);
    EXPECT_EQ(editor.currentBuffer->visualEndX, 10);
    EXPECT_EQ(*editor.cursorX, 10);
}

TEST(RealModeTransitionsTest, VisualInnerLeftBracketSelectsTextObject)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"items[index + 1];"};
    *editor.cursorX = 8;
    auto sm = makeMachine(editor, VisualMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(command::CommandKey::KEY_LEFT_BRACKET));

    EXPECT_STREQ(sm.currentStateName(), "VISUAL");
    EXPECT_EQ(editor.currentBuffer->visualStartX, 6);
    EXPECT_EQ(editor.currentBuffer->visualEndX, 14);
    EXPECT_EQ(*editor.cursorX, 14);
}

TEST(RealModeTransitionsTest, VisualInnerRightBracketSelectsTextObject)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"items[index + 1];"};
    *editor.cursorX = 8;
    auto sm = makeMachine(editor, VisualMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(command::CommandKey::KEY_RIGHT_BRACKET));

    EXPECT_STREQ(sm.currentStateName(), "VISUAL");
    EXPECT_EQ(editor.currentBuffer->visualStartX, 6);
    EXPECT_EQ(editor.currentBuffer->visualEndX, 14);
    EXPECT_EQ(*editor.cursorX, 14);
}

TEST(RealModeTransitionsTest, VisualInnerLeftBraceSelectsMultilineTextObject)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {
        "void f() {",
        "    int value = 1;",
        "    value++;",
        "}",
    };
    *editor.cursorY = 1;
    *editor.cursorX = 8;
    auto sm = makeMachine(editor, VisualMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(command::CommandKey::KEY_LEFT_BRACE));

    EXPECT_STREQ(sm.currentStateName(), "VISUAL");
    EXPECT_EQ(editor.currentBuffer->visualStartY, 1);
    EXPECT_EQ(editor.currentBuffer->visualStartX, 0);
    EXPECT_EQ(editor.currentBuffer->visualEndY, 2);
    EXPECT_EQ(editor.currentBuffer->visualEndX, 11);
    EXPECT_EQ(*editor.cursorY, 2);
    EXPECT_EQ(*editor.cursorX, 11);
}

TEST(RealModeTransitionsTest, VisualInnerRightBraceSelectsMultilineTextObject)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {
        "void f() {",
        "    int value = 1;",
        "    value++;",
        "}",
    };
    *editor.cursorY = 1;
    *editor.cursorX = 8;
    auto sm = makeMachine(editor, VisualMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(command::CommandKey::KEY_RIGHT_BRACE));

    EXPECT_STREQ(sm.currentStateName(), "VISUAL");
    EXPECT_EQ(editor.currentBuffer->visualStartY, 1);
    EXPECT_EQ(editor.currentBuffer->visualStartX, 0);
    EXPECT_EQ(editor.currentBuffer->visualEndY, 2);
    EXPECT_EQ(editor.currentBuffer->visualEndX, 11);
    EXPECT_EQ(*editor.cursorY, 2);
    EXPECT_EQ(*editor.cursorX, 11);
}

TEST(RealModeTransitionsTest, NormalVisualInnerParenSelectsTextObject)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"foo(one, two);"};
    *editor.cursorX = 5;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_V));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(command::CommandKey::KEY_RIGHT_PAREN));

    EXPECT_STREQ(sm.currentStateName(), "VISUAL");
    EXPECT_EQ(editor.currentBuffer->visualStartX, 4);
    EXPECT_EQ(editor.currentBuffer->visualEndX, 11);
    EXPECT_EQ(*editor.cursorX, 11);
}

TEST(RealModeTransitionsTest, NormalVisualInnerParenEscCancelsPending)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"foo(one, two);"};
    *editor.cursorX = 5;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_V));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(control::ControlKey::ESC));

    EXPECT_STREQ(sm.currentStateName(), "VISUAL");
    EXPECT_TRUE(editor.commandBuffer.empty());
    EXPECT_EQ(editor.currentBuffer->visualStartX, 5);
    EXPECT_EQ(editor.currentBuffer->visualEndX, 5);
    EXPECT_EQ(*editor.cursorX, 5);
}

TEST(RealModeTransitionsTest, NormalOpenBelowAfterCppBraceUsesIndentWidth)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"fn main() {"};
    set_buffer_filename(editor, "main.cpp");
    editor.currentBuffer->clangIndentWidthValid = true;
    editor.currentBuffer->clangIndentWidth = 4;
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('o');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 2u);
    EXPECT_EQ(editor.currentBuffer->lines[1], "    ");
    EXPECT_EQ(*editor.cursorY, 1);
    EXPECT_EQ(*editor.cursorX, 4);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, NormalOpenAboveClosingCppBraceUsesIndentWidth)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"fn main() {", "}"};
    set_buffer_filename(editor, "main.cpp");
    editor.currentBuffer->clangIndentWidthValid = true;
    editor.currentBuffer->clangIndentWidth = 4;
    *editor.cursorX = 0;
    *editor.cursorY = 1;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('O');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[1], "    ");
    EXPECT_EQ(editor.currentBuffer->lines[2], "}");
    EXPECT_EQ(*editor.cursorY, 1);
    EXPECT_EQ(*editor.cursorX, 4);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, NormalRnRequestsClangRename)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int value = 1;"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorX = 4;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    Terminal::unreadKey('n');
    sm.dispatch('r');

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    EXPECT_FALSE(editor.renamePopupActive);
    EXPECT_EQ(editor.statusMessage, "rn: clangd rename unavailable");
}

TEST(RealModeTransitionsTest, VisualRnRequestsClangRenameAndReturnsNormal)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int value = 1;"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorX = 4;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, VisualMode{});
    Terminal::unreadKey('n');
    sm.dispatch('r');

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    EXPECT_FALSE(editor.renamePopupActive);
    EXPECT_EQ(editor.statusMessage, "rn: clangd rename unavailable");
}

TEST(RealModeTransitionsTest, UndoRestoresAllFilesTouchedByRename)
{
    auto root = make_temp_dir("uvim_rename_undo_");
    auto current = root / "main.cpp";
    auto other = root / "other.cpp";
    write_file(current, "int value = 1;\n");
    write_file(other, "int value = 2;\n");

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int value = 1;"};
    set_buffer_filename(editor, current.string());
    editor.currentBuffer->dirty = false;

    Editor::RenameUndoFileSnapshot currentSnapshot;
    currentSnapshot.path = current.string();
    currentSnapshot.lines = {"int value = 1;"};
    currentSnapshot.hadOpenBuffer = true;
    currentSnapshot.fileExisted = true;
    currentSnapshot.dirty = false;

    Editor::RenameUndoFileSnapshot otherSnapshot;
    otherSnapshot.path = other.string();
    otherSnapshot.lines = {"int value = 2;"};
    otherSnapshot.fileExisted = true;

    editor.renameUndoAvailable = true;
    editor.renameUndoFiles = {currentSnapshot, otherSnapshot};

    editor.currentBuffer->lines = {"int renamed = 1;"};
    editor.currentBuffer->dirty = true;
    write_file(other, "int renamed = 2;\n");

    editor.undo();

    EXPECT_EQ(editor.currentBuffer->lines,
              std::vector<std::string>({"int value = 1;"}));
    EXPECT_FALSE(editor.currentBuffer->dirty);
    EXPECT_EQ(read_file(other), "int value = 2;");
    EXPECT_FALSE(editor.renameUndoAvailable);
    EXPECT_EQ(editor.statusMessage, "rn: reverted rename in 2 file(s)");
}

TEST(RealModeTransitionsTest, NormalPasteRefreshesSystemClipboard)
{
#ifdef _WIN32
    GTEST_SKIP()
        << "System clipboard command lookup is not implemented on Windows";
#else
    auto root = make_temp_dir("uvim_clipboard_");
    auto binDir = root / "bin";
    auto clipFile = root / "clipboard.txt";
    std::filesystem::create_directories(binDir);
    write_file(clipFile, "external");

#ifdef __APPLE__
    auto pasteCmd = binDir / "pbpaste";
    write_file(pasteCmd, "#!/bin/sh\ncat \"$UVIM_TEST_CLIPBOARD\"\n");
#else
    auto pasteCmd = binDir / "xclip";
    write_file(pasteCmd, "#!/bin/sh\ncat \"$UVIM_TEST_CLIPBOARD\"\n");
#endif
    std::filesystem::permissions(pasteCmd,
                                 std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add);

    const char* oldPath = std::getenv("PATH");
    std::string path = binDir.string();
    if(oldPath && *oldPath)
        path += std::string(":") + oldPath;

    ScopedEnv pathEnv("PATH", path);
    ScopedEnv clipEnv("UVIM_TEST_CLIPBOARD", clipFile.string());

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"alpha"};
    editor.yankBuffer = "stale";
    *editor.cursorX = 4;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('p');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "alphaexternal");
    EXPECT_EQ(editor.yankBuffer, "external");
#endif
}

TEST(RealModeTransitionsTest, VisualLinePasteUsesSystemClipboard)
{
#ifdef _WIN32
    GTEST_SKIP()
        << "System clipboard command lookup is not implemented on Windows";
#else
    auto root = make_temp_dir("uvim_visual_clipboard_");
    auto binDir = root / "bin";
    auto clipFile = root / "clipboard.txt";
    std::filesystem::create_directories(binDir);
    write_file(clipFile, "replacement\n");

#ifdef __APPLE__
    auto pasteCmd = binDir / "pbpaste";
    write_file(pasteCmd, "#!/bin/sh\ncat \"$UVIM_TEST_CLIPBOARD\"\n");
#else
    auto pasteCmd = binDir / "xclip";
    write_file(pasteCmd, "#!/bin/sh\ncat \"$UVIM_TEST_CLIPBOARD\"\n");
#endif
    std::filesystem::permissions(pasteCmd,
                                 std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add);

    const char* oldPath = std::getenv("PATH");
    std::string path = binDir.string();
    if(oldPath && *oldPath)
        path += std::string(":") + oldPath;

    ScopedEnv pathEnv("PATH", path);
    ScopedEnv clipEnv("UVIM_TEST_CLIPBOARD", clipFile.string());

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"void f() {", "    old();", "}", "after"};
    editor.yankBuffer = "stale\n";
    *editor.cursorX = 9;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, VisualLineMode{});
    sm.dispatch('%');
    sm.dispatch('p');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 2u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "replacement");
    EXPECT_EQ(editor.currentBuffer->lines[1], "after");
    EXPECT_EQ(editor.yankBuffer, "replacement\n");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
#endif
}

TEST(RealModeTransitionsTest, UndoVisualLinePasteRestoresVisualStartCursor)
{
#ifdef _WIN32
    GTEST_SKIP()
        << "System clipboard command lookup is not implemented on Windows";
#else
    auto root = make_temp_dir("uvim_visual_clipboard_undo_");
    auto binDir = root / "bin";
    auto clipFile = root / "clipboard.txt";
    std::filesystem::create_directories(binDir);
    write_file(clipFile, "replacement\n");

#ifdef __APPLE__
    auto pasteCmd = binDir / "pbpaste";
    write_file(pasteCmd, "#!/bin/sh\ncat \"$UVIM_TEST_CLIPBOARD\"\n");
#else
    auto pasteCmd = binDir / "xclip";
    write_file(pasteCmd, "#!/bin/sh\ncat \"$UVIM_TEST_CLIPBOARD\"\n");
#endif
    std::filesystem::permissions(pasteCmd,
                                 std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add);

    const char* oldPath = std::getenv("PATH");
    std::string path = binDir.string();
    if(oldPath && *oldPath)
        path += std::string(":") + oldPath;

    ScopedEnv pathEnv("PATH", path);
    ScopedEnv clipEnv("UVIM_TEST_CLIPBOARD", clipFile.string());

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"void f() {", "    old();", "}", "after"};
    *editor.cursorX = 9;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, VisualLineMode{});
    sm.dispatch('%');
    sm.dispatch('p');
    editor.undo();

    ASSERT_EQ(editor.currentBuffer->lines.size(), 4u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "void f() {");
    EXPECT_EQ(editor.currentBuffer->lines[1], "    old();");
    EXPECT_EQ(editor.currentBuffer->lines[2], "}");
    EXPECT_EQ(editor.currentBuffer->lines[3], "after");
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_EQ(*editor.cursorX, 9);
#endif
}

TEST(RealModeTransitionsTest, VisualLineBracketedPasteReplacesSelection)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"void f() {", "    old();", "}", "after"};
    editor.yankBuffer = "stale\n";
    *editor.cursorX = 9;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, VisualLineMode{});
    sm.dispatch('%');
    Terminal::setLastPasteTextForTests("replacement\n");
    sm.dispatch(keyCode(control::ControlKey::PASTE));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 2u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "replacement");
    EXPECT_EQ(editor.currentBuffer->lines[1], "after");
    EXPECT_EQ(editor.yankBuffer, "replacement\n");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, AutoBraceReturnInitializerStaysInlineInCpp)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"return "};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorX = (int)editor.currentBuffer->lines[0].size();
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, InsertMode{});
    sm.dispatch('{');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "return {}");
    EXPECT_EQ(*editor.cursorX, 8);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, AutoBraceReturnInitializerStaysInlineInMla)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"return "};
    set_buffer_filename(editor, "main.mla");
    *editor.cursorX = (int)editor.currentBuffer->lines[0].size();
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, InsertMode{});
    sm.dispatch('{');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "return {}");
    EXPECT_EQ(*editor.cursorX, 8);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, AutoBraceFunctionArgumentStaysInline)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"call()"};
    set_buffer_filename(editor, "main.mla");
    *editor.cursorX = 5;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, InsertMode{});
    sm.dispatch('{');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "call({})");
    EXPECT_EQ(*editor.cursorX, 6);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, LocListReturnsToFileBrowserWithCursorState)
{
    auto root = make_temp_dir("uvim_loc_return_");
    write_file(root / "a.cpp", "int main() { return 0; }\n");
    write_file(root / "b.cpp", "int add(int a,int b){return a+b;}\n");

    std::error_code ec;
    std::filesystem::current_path(root, ec);

    Editor editor = Editor::createForTests();
    FileBrowserMode browse{root.string()};
    browse.browserCursor = 1;
    browse.browserOffset = 0;

    auto sm = makeMachine(editor, browse);

    sm.dispatch(':');
    sm.dispatch('l');
    sm.dispatch('o');
    sm.dispatch('c');
    sm.dispatch(' ');
    sm.dispatch('.');
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm.currentStateName(), "LOC");

    sm.dispatch('q');

    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
    auto* fb = sm.getState<FileBrowserMode>();
    ASSERT_NE(fb, nullptr);
    EXPECT_EQ(fb->currentDirectory, root.string());
    EXPECT_EQ(fb->browserCursor, 1);
    EXPECT_EQ(fb->browserOffset, 0);
}

TEST(RealModeTransitionsTest, CompletionTrimsLeadingSpaceAfterDot)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"ctx."};
    *editor.cursorX = 4;
    *editor.cursorY = 0;

    CompletionEntry e;
    e.label = " cancelCommandPopup()";
    editor.completionAll = {e};
    editor.completionFiltered = {0};
    editor.completionSelected = 0;
    editor.completionActive = true;
    editor.completionAnchorX = 4;
    editor.completionAnchorY = 0;

    editor.acceptCompletion();

    EXPECT_EQ(editor.currentBuffer->lines[0], "ctx.cancelCommandPopup();");
    EXPECT_EQ(*editor.cursorX, 23);
}

TEST(RealModeTransitionsTest, CompletionAutoParensOmitsSemicolonInsideCall)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"outer(ca)"};
    *editor.cursorX = 8;
    *editor.cursorY = 0;

    CompletionEntry e;
    e.label = "callee()";
    editor.completionAll = {e};
    editor.completionFiltered = {0};
    editor.completionSelected = 0;
    editor.completionActive = true;
    editor.completionAnchorX = 6;
    editor.completionAnchorY = 0;

    editor.acceptCompletion();

    EXPECT_EQ(editor.currentBuffer->lines[0], "outer(callee())");
    EXPECT_EQ(*editor.cursorX, 13);
}

TEST(RealModeTransitionsTest,
     CompletionAutoParensOmitsSemicolonInsideIfCondition)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"if (rea)"};
    *editor.cursorX = 7;
    *editor.cursorY = 0;

    CompletionEntry e;
    e.label = "ready()";
    editor.completionAll = {e};
    editor.completionFiltered = {0};
    editor.completionSelected = 0;
    editor.completionActive = true;
    editor.completionAnchorX = 4;
    editor.completionAnchorY = 0;

    editor.acceptCompletion();

    EXPECT_EQ(editor.currentBuffer->lines[0], "if (ready())");
    EXPECT_EQ(*editor.cursorX, 10);
}

TEST(RealModeTransitionsTest, SearchPromptShowsFullTypedPattern)
{
    std::string output;
    widgets::MessageBarView view{
        .currentMode = SEARCH_FORWARD,
        .screenCols = 80,
        .commandBuffer = "/long_pattern",
    };

    widgets::appendMessageBar(output, view);

    EXPECT_EQ(output, "/long_pattern");
}

TEST(RealModeTransitionsTest, IncrementalForwardSearchStartsAtSavedCursor)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"foo one", "foo two", "foo three"};
    *editor.cursorX = 4;
    *editor.cursorY = 1;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('/');
    sm.dispatch('f');
    sm.dispatch('o');
    sm.dispatch('o');

    EXPECT_EQ(editor.commandBuffer, "/foo");
    ASSERT_EQ(editor.searchMatches.size(), 3u);
    EXPECT_EQ(*editor.cursorY, 2);
    EXPECT_EQ(*editor.cursorX, 0);
    EXPECT_EQ(editor.currentMatchIndex, 2);
    EXPECT_STREQ(sm.currentStateName(), "/");
}

TEST(RealModeTransitionsTest, SearchPromptAcceptsBracketedPaste)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"alpha one", "beta two", "alpha pasted"};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('/');
    Terminal::setLastPasteTextForTests("alpha pasted\n");
    sm.dispatch(keyCode(control::ControlKey::PASTE));

    EXPECT_EQ(editor.commandBuffer, "/alpha pasted");
    ASSERT_EQ(editor.searchMatches.size(), 1u);
    EXPECT_EQ(editor.searchMatches[0].row, 2);
    EXPECT_EQ(*editor.cursorY, 2);
    EXPECT_EQ(*editor.cursorX, 0);
    EXPECT_STREQ(sm.currentStateName(), "/");
}

TEST(RealModeTransitionsTest, BackwardSearchPromptAcceptsBracketedPaste)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"alpha pasted", "beta two", "alpha one"};
    *editor.cursorX = 0;
    *editor.cursorY = 2;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('?');
    Terminal::setLastPasteTextForTests("alpha pasted");
    sm.dispatch(keyCode(control::ControlKey::PASTE));

    EXPECT_EQ(editor.commandBuffer, "?alpha pasted");
    ASSERT_EQ(editor.searchMatches.size(), 1u);
    EXPECT_EQ(editor.searchMatches[0].row, 0);
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_EQ(*editor.cursorX, 0);
    EXPECT_STREQ(sm.currentStateName(), "?");
}

TEST(RealModeTransitionsTest, IncrementalForwardSearchRecomputesAfterEdit)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"abc here", "abd there"};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('/');
    sm.dispatch('a');
    sm.dispatch('b');
    sm.dispatch('c');
    sm.dispatch(keyCode(control::ControlKey::BACKSPACE));
    sm.dispatch('d');

    EXPECT_EQ(editor.commandBuffer, "/abd");
    ASSERT_EQ(editor.searchMatches.size(), 1u);
    EXPECT_EQ(editor.searchMatches[0].row, 1);
    EXPECT_EQ(*editor.cursorY, 1);
    EXPECT_EQ(*editor.cursorX, 0);
    EXPECT_EQ(editor.currentMatchIndex, 0);
}

TEST(RealModeTransitionsTest,
     IncrementalSearchClearsStaleHighlightsOnEmptyQuery)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"needle"};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('/');
    sm.dispatch('n');
    ASSERT_FALSE(editor.searchMatches.empty());
    EXPECT_TRUE(editor.isInSearchMatch(0, 0));

    editor.needsFullRedraw = false;
    sm.dispatch(keyCode(control::ControlKey::CTRL_U));

    EXPECT_EQ(editor.commandBuffer, "/");
    EXPECT_TRUE(editor.searchMatches.empty());
    EXPECT_FALSE(editor.isInSearchMatch(0, 0));
    EXPECT_TRUE(editor.needsFullRedraw);
}

TEST(RealModeTransitionsTest, FormatOnSaveCallsFormatterHookWhenEnabled)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.formatOnSave = true;
    editor.currentBuffer->filename = "/tmp/uvim_format_on_save.mla";
    editor.currentBuffer->lines = {"fn main() {}"};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    bool called = false;
    editor.formatOnSaveTestHook = [&]()
    {
        called = true;
        return true;
    };

    editor.saveFile();

    EXPECT_TRUE(called);
}

TEST(RealModeTransitionsTest, FormatOnSaveSkipsFormatterHookWhenDisabled)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.formatOnSave = false;
    editor.currentBuffer->filename = "/tmp/uvim_format_on_save_off.mla";
    editor.currentBuffer->lines = {"fn main() {}"};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    bool called = false;
    editor.formatOnSaveTestHook = [&]()
    {
        called = true;
        return true;
    };

    editor.saveFile();

    EXPECT_FALSE(called);
}

TEST(RealModeTransitionsTest, ClangFormatUndoRestoresSavedBuffer)
{
    if(std::system("/opt/homebrew/bin/clang-format --version >/dev/null 2>&1 "
                   "|| clang-format --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang-format is not available";

    auto root = make_temp_dir("uvim_clang_format_undo_");
    auto file = root / "format_me.cpp";
    write_file(root / ".clang-format", "BasedOnStyle: LLVM\n"
                                       "BreakBeforeBraces: Allman\n"
                                       "IndentWidth: 4\n");
    write_file(file, "int main(){\n"
                     "if(true){\n"
                     "return 1;\n"
                     "}\n"
                     "}\n");

    Editor editor = Editor::createForTests();
    editor.openFile(file.string(), false);

    const std::vector<std::string> originalLines = editor.currentBuffer->lines;
    ASSERT_TRUE(editor.formatBuffer());
    ASSERT_NE(editor.currentBuffer->lines, originalLines);
    EXPECT_TRUE(*editor.dirty);

    editor.undo();

    EXPECT_NE(editor.statusMessage, "Already at oldest change");
    EXPECT_EQ(editor.currentBuffer->lines, originalLines);
    EXPECT_FALSE(*editor.dirty);
}

TEST(RealModeTransitionsTest, ExCommandOpensFileBrowser)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(':');
    sm.dispatch('E');
    sm.dispatch('x');
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
}

TEST(RealModeTransitionsTest, ExCommandWithPathOpensFileBrowser)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(':');
    sm.dispatch('E');
    sm.dispatch('x');
    sm.dispatch(' ');
    sm.dispatch('s');
    sm.dispatch('r');
    sm.dispatch('c');
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
}

TEST(RealModeTransitionsTest, ExCommandFromCommandModeOpensFileBrowser)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(':');
    sm.dispatch('E');
    sm.dispatch('x');
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
}

TEST(RealModeTransitionsTest, ExLineNumberInputSuppressesCommandPopup)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(':');
    ASSERT_TRUE(editor.commandPopupActive);
    sm.dispatch('5');
    sm.dispatch('2');
    sm.dispatch('5');

    EXPECT_FALSE(editor.commandPopupActive);
    EXPECT_TRUE(editor.needsFullRedraw);
}

TEST(RealModeTransitionsTest, EnteringCommandModeClearsCompletionPopup)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.completionActive = true;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(':');

    EXPECT_FALSE(editor.completionActive);
}

TEST(RealModeTransitionsTest, FileBrowserFuzzyDisabledIgnoresTyping)
{
    Editor editor = Editor::createForTests();
    editor.fileBrowserFuzzy = false;
    auto sm = makeMachine(editor, FileBrowserMode{std::string(".")});

    sm.dispatch('a');

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_FALSE(state->filterActive);
    EXPECT_TRUE(state->filterQuery.empty());
    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
}

TEST(RealModeTransitionsTest,
     FileBrowserCommandSlashRegexSearchStaysInCurrentDirectory)
{
    auto root = make_temp_dir("uvim_browse_regex_local_");
    write_file(root / "a.txt", "a\n");
    write_file(root / "sub" / "needle.txt", "n\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int aIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "a.txt")
        {
            aIndex = i;
            break;
        }
    }
    ASSERT_GE(aIndex, 0);

    dispatch_command(sm, "/needle\\.txt");
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(text_utils::is_found(
        editor.statusMessage.find("No match for regex: needle\\.txt")));
    EXPECT_NE(state->browserCursor, aIndex);

    dispatch_command(sm, "/a\\.txt");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    ASSERT_NE(editor.currentBuffer, nullptr);
    EXPECT_TRUE(
        text_utils::is_found(editor.currentBuffer->filename.find("a.txt")));
}

TEST(RealModeTransitionsTest, FileBrowserCommandQuestionRegexSearchesBackward)
{
    auto root = make_temp_dir("uvim_browse_regex_back_");
    write_file(root / "alpha.txt", "a\n");
    write_file(root / "beta.txt", "b\n");
    write_file(root / "gamma.txt", "g\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int alphaIndex = -1;
    int betaIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "alpha.txt")
            alphaIndex = i;
        if(state->fileList[i].name == "beta.txt")
            betaIndex = i;
    }
    ASSERT_GE(alphaIndex, 0);
    ASSERT_GE(betaIndex, 0);

    state->browserCursor = betaIndex;
    dispatch_command(sm, "?^alpha\\.txt$");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    ASSERT_NE(editor.currentBuffer, nullptr);
    EXPECT_TRUE(
        text_utils::is_found(editor.currentBuffer->filename.find("alpha.txt")));
}

TEST(RealModeTransitionsTest, FileBrowserSlashKeyRunsLocalRegexSearch)
{
    auto root = make_temp_dir("uvim_browse_slash_key_");
    write_file(root / "alpha.txt", "a\n");
    write_file(root / "beta.txt", "b\n");
    write_file(root / "sub" / "alpha.txt", "nested\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int alphaIndex = -1;
    int betaIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "alpha.txt")
            alphaIndex = i;
        if(state->fileList[i].name == "beta.txt")
            betaIndex = i;
    }
    ASSERT_GE(alphaIndex, 0);
    ASSERT_GE(betaIndex, 0);

    state->browserCursor = betaIndex;
    sm.dispatch('/');
    sm.dispatch('a');
    sm.dispatch('l');
    sm.dispatch('p');
    sm.dispatch('h');
    sm.dispatch('a');
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    ASSERT_NE(editor.currentBuffer, nullptr);
    EXPECT_TRUE(
        text_utils::is_found(editor.currentBuffer->filename.find("alpha.txt")));
}

TEST(RealModeTransitionsTest, FileBrowserQuestionKeyRunsBackwardRegexSearch)
{
    auto root = make_temp_dir("uvim_browse_question_key_");
    write_file(root / "alpha.txt", "a\n");
    write_file(root / "beta.txt", "b\n");
    write_file(root / "gamma.txt", "g\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int alphaIndex = -1;
    int gammaIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "alpha.txt")
            alphaIndex = i;
        if(state->fileList[i].name == "gamma.txt")
            gammaIndex = i;
    }
    ASSERT_GE(alphaIndex, 0);
    ASSERT_GE(gammaIndex, 0);

    state->browserCursor = gammaIndex;
    sm.dispatch('?');
    sm.dispatch('a');
    sm.dispatch('l');
    sm.dispatch('p');
    sm.dispatch('h');
    sm.dispatch('a');
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    ASSERT_NE(editor.currentBuffer, nullptr);
    EXPECT_TRUE(
        text_utils::is_found(editor.currentBuffer->filename.find("alpha.txt")));
}

TEST(RealModeTransitionsTest, FileBrowserCtrlSStillOpensGrepSearch)
{
    auto root = make_temp_dir("uvim_browse_ctrls_");
    write_file(root / "a.txt", "a\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    sm.dispatch(keyCode(control::ControlKey::CTRL_S));

    EXPECT_STREQ(sm.currentStateName(), "GREP");
}

TEST(RealModeTransitionsTest, FuzzyFindAcceptsBracketedPaste)
{
    auto root = make_temp_dir("uvim_fuzzy_paste_");
    write_file(root / "alpha-pasted.txt", "a\n");
    write_file(root / "beta.txt", "b\n");
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    auto sm = makeMachine(editor, FuzzyFindMode{});

    Terminal::setLastPasteTextForTests("alpha-pasted\n");
    sm.dispatch(keyCode(control::ControlKey::PASTE));

    auto* state = sm.getState<FuzzyFindMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->query, "alpha-pasted");
    ASSERT_FALSE(state->matches.empty());
    EXPECT_EQ(state->matches.front().file.name, "alpha-pasted.txt");
}

TEST(RealModeTransitionsTest, GrepSearchAcceptsBracketedPaste)
{
    auto root = make_temp_dir("uvim_grep_paste_");
    write_file(root / "src" / "match.txt", "needle pasted\nother\n");
    write_file(root / "src" / "miss.txt", "needle only\n");
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    auto sm = makeMachine(editor, GrepSearchMode{});

    Terminal::setLastPasteTextForTests("needle pasted\n");
    sm.dispatch(keyCode(control::ControlKey::PASTE));

    auto* state = sm.getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->query, "needle pasted");
    ASSERT_EQ(state->matches.size(), 1u);
    EXPECT_TRUE(text_utils::is_found(
        state->matches.front().filepath.find("match.txt")));
    EXPECT_EQ(state->matches.front().lineNumber, 1);
}

TEST(RealModeTransitionsTest, CtrlXOpensRegexSearchForCurrentBuffer)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    set_buffer_filename(editor, "sample.cpp");
    editor.currentBuffer->lines = {
        "int value1 = 0;",
        "int other = 0;",
        "int value42 = 1;",
    };
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_X));

    ASSERT_STREQ(sm.currentStateName(), "REGEX");
    auto* state = sm.getState<RegexSearchMode>();
    ASSERT_NE(state, nullptr);

    for(char c : std::string("value[0-9]+"))
        sm.dispatch(c);

    state = sm.getState<RegexSearchMode>();
    ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->matches.size(), 2u);
    EXPECT_EQ(state->matches[0].lineNumber, 1);
    EXPECT_EQ(state->matches[0].matchText, "value1");

    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_EQ(*editor.cursorX, 4);
    EXPECT_FALSE(editor.searchMatches.empty());
}

TEST(RealModeTransitionsTest, RegexSearchCtrlSTogglesProjectFiles)
{
    auto root = make_temp_dir("uvim_regex_search_");
    write_file(root / "a.cpp", "int needle1 = 0;\n");
    write_file(root / "b.cpp", "int other = 0;\nint needle22 = 1;\n");
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    editor.createNewBuffer();
    set_buffer_filename(editor, (root / "current.cpp").string());
    editor.currentBuffer->lines = {"int local = 0;"};

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_X));
    sm.dispatch(keyCode(control::ControlKey::CTRL_S));
    for(char c : std::string("needle[0-9]+"))
        sm.dispatch(c);

    auto* state = sm.getState<RegexSearchMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(state->allFiles);
    ASSERT_EQ(state->matches.size(), 2u);
    EXPECT_TRUE(text_utils::is_found(state->matches[0].filepath.find("a.cpp")));
    EXPECT_TRUE(text_utils::is_found(state->matches[1].filepath.find("b.cpp")));
}

TEST(RealModeTransitionsTest, FileBrowserNAndNShiftWrapSearchMatches)
{
    auto root = make_temp_dir("uvim_browse_n_wrap_");
    write_file(root / "edit-a.txt", "a\n");
    write_file(root / "edit-b.txt", "b\n");
    write_file(root / "zzz.txt", "z\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int aIndex = -1;
    int bIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "edit-a.txt")
            aIndex = i;
        if(state->fileList[i].name == "edit-b.txt")
            bIndex = i;
    }
    ASSERT_GE(aIndex, 0);
    ASSERT_GE(bIndex, 0);

    state->searchMatches = {aIndex, bIndex};
    state->lastSearchPattern = "edit";
    state->lastSearchPrefix = '/';
    state->currentSearchMatch = 0;
    state->browserCursor = aIndex;

    sm.dispatch('n');
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->browserCursor, bIndex);

    sm.dispatch('n');
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->browserCursor, aIndex);

    sm.dispatch('N');
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->browserCursor, bIndex);
}

TEST(RealModeTransitionsTest, FileBrowserSearchTabCompletionCyclesMatches)
{
    auto root = make_temp_dir("uvim_browse_search_tab_");
    write_file(root / "edit-alpha.txt", "a\n");
    write_file(root / "edit-beta.txt", "b\n");
    write_file(root / "zzz.txt", "z\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int alphaIndex = -1;
    int betaIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "edit-alpha.txt")
            alphaIndex = i;
        if(state->fileList[i].name == "edit-beta.txt")
            betaIndex = i;
    }
    ASSERT_GE(alphaIndex, 0);
    ASSERT_GE(betaIndex, 0);

    sm.dispatch('/');
    sm.dispatch('e');
    sm.dispatch(keyCode(control::ControlKey::TAB));
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    ASSERT_NE(editor.currentBuffer, nullptr);
    EXPECT_TRUE(text_utils::is_found(
        editor.currentBuffer->filename.find("edit-alpha.txt")));

    Editor editor2 = Editor::createForTests();
    auto sm2 = makeMachine(editor2, FileBrowserMode{root.string()});
    sm2.dispatch('/');
    sm2.dispatch('e');
    sm2.dispatch(keyCode(control::ControlKey::SHIFT_TAB));
    sm2.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm2.currentStateName(), "NORMAL");
    ASSERT_NE(editor2.currentBuffer, nullptr);
    EXPECT_TRUE(
        text_utils::is_found(editor2.currentBuffer->filename.find("edit-")));
}

TEST(RealModeTransitionsTest, FileBrowserCtrlJKCyclesWhileSearchPromptActive)
{
    auto root = make_temp_dir("uvim_browse_ctrljk_prompt_");
    write_file(root / "edit-alpha.txt", "a\n");
    write_file(root / "edit-beta.txt", "b\n");
    write_file(root / "edit-gamma.txt", "g\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int alphaIndex = -1;
    int betaIndex = -1;
    int gammaIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "edit-alpha.txt")
            alphaIndex = i;
        if(state->fileList[i].name == "edit-beta.txt")
            betaIndex = i;
        if(state->fileList[i].name == "edit-gamma.txt")
            gammaIndex = i;
    }
    ASSERT_GE(alphaIndex, 0);
    ASSERT_GE(betaIndex, 0);
    ASSERT_GE(gammaIndex, 0);

    sm.dispatch('/');
    sm.dispatch('e');
    sm.dispatch('d');
    sm.dispatch('i');
    sm.dispatch('t');
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    ASSERT_TRUE(state->commandPrompt);
    EXPECT_TRUE(state->commandPrompt->isActive());
    EXPECT_EQ(state->commandPrompt->getInput(), "/edit");

    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    ASSERT_TRUE(state->commandPrompt);
    EXPECT_TRUE(state->commandPrompt->isActive());
    EXPECT_EQ(state->browserCursor, betaIndex);

    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->browserCursor, gammaIndex);

    sm.dispatch(keyCode(control::ControlKey::CTRL_K));
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->browserCursor, betaIndex);

    sm.dispatch(keyCode(control::ControlKey::CTRL_K));
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->browserCursor, alphaIndex);
}

TEST(RealModeTransitionsTest, FileBrowserEscClearsSearchBeforeExit)
{
    auto root = make_temp_dir("uvim_browse_esc_clear_search_");
    write_file(root / "edit-alpha.txt", "a\n");
    write_file(root / "edit-beta.txt", "b\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    sm.dispatch('/');
    sm.dispatch('e');
    sm.dispatch('d');
    sm.dispatch('i');
    sm.dispatch('t');
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    ASSERT_FALSE(state->searchMatches.empty());

    sm.dispatch(keyCode(control::ControlKey::ESC));
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
    EXPECT_TRUE(state->searchMatches.empty());
    EXPECT_TRUE(state->lastSearchPattern.empty());
}

TEST(RealModeTransitionsTest, FileBrowserSearchTypingMovesToFirstMatch)
{
    auto root = make_temp_dir("uvim_browse_live_first_match_");
    write_file(root / "alpha.txt", "a\n");
    write_file(root / "beta.txt", "b\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int alphaIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "alpha.txt")
            alphaIndex = i;
    }
    ASSERT_GE(alphaIndex, 0);

    sm.dispatch('/');
    sm.dispatch('a');

    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    ASSERT_TRUE(state->commandPrompt);
    EXPECT_TRUE(state->commandPrompt->isActive());
    EXPECT_EQ(state->browserCursor, alphaIndex);
}

TEST(RealModeTransitionsTest, FileBrowserSearchEnterOpensMatchedFile)
{
    auto root = make_temp_dir("uvim_browse_search_enter_file_");
    write_file(root / "target.txt", "hello\n");
    write_file(root / "other.txt", "other\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    sm.dispatch('/');
    sm.dispatch('t');
    sm.dispatch('a');
    sm.dispatch('r');
    sm.dispatch('g');
    sm.dispatch('e');
    sm.dispatch('t');
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    ASSERT_NE(editor.currentBuffer, nullptr);
    EXPECT_TRUE(text_utils::is_found(
        editor.currentBuffer->filename.find("target.txt")));
}

TEST(RealModeTransitionsTest,
     FileBrowserPromptSearchCtrlNEnterOpensSelectedMatches)
{
    auto root = make_temp_dir("uvim_browse_search_multi_open_");
    write_file(root / "match-alpha.txt", "alpha\n");
    write_file(root / "match-beta.txt", "beta\n");
    write_file(root / "other.txt", "other\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    sm.dispatch(':');
    sm.dispatch('/');
    for(char ch : std::string("match-"))
        sm.dispatch(ch);
    sm.dispatch(keyCode(control::ControlKey::CTRL_N));

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->selectedFiles.size(), 2u);

    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    EXPECT_EQ(editor.buffers.size(), 2u);
    ASSERT_NE(editor.currentBuffer, nullptr);
    EXPECT_TRUE(text_utils::is_found(
        editor.buffers[0]->filename.find("match-alpha.txt")));
    EXPECT_TRUE(text_utils::is_found(
        editor.buffers[1]->filename.find("match-beta.txt")));
}

TEST(RealModeTransitionsTest, FileBrowserSearchEnterOpensMatchedDirectory)
{
    auto root = make_temp_dir("uvim_browse_search_enter_dir_");
    write_file(root / "docs" / "readme.txt", "docs\n");
    write_file(root / "z.txt", "z\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    sm.dispatch('/');
    sm.dispatch('d');
    sm.dispatch('o');
    sm.dispatch('c');
    sm.dispatch('s');
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    const std::string mode = sm.currentStateName();
    EXPECT_TRUE(mode == "BROWSE" || mode == "NORMAL");
}

TEST(RealModeTransitionsTest, FileBrowserParentThenEnterSiblingDirectoryStays)
{
    auto root = make_temp_dir("uvim_browse_parent_then_sibling_");
    write_file(root / "from" / "a.txt", "from\n");
    write_file(root / "to" / "b.txt", "to\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{(root / "from").string()});

    // Enter parent via ".." (first entry).
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);

    int toIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "to")
        {
            toIndex = i;
            break;
        }
    }
    ASSERT_GE(toIndex, 0);

    for(int i = 0; i < toIndex; ++i)
        sm.dispatch('j');

    sm.dispatch(keyCode(control::ControlKey::ENTER));

    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    // Compare via path components to stay portable (Windows uses backslash).
    auto endsWithDir = [](const std::string& path,
                          const std::string& dirName) -> bool
    {
        std::filesystem::path p(path);
        return p.filename() == dirName;
    };
    EXPECT_TRUE(endsWithDir(state->currentDirectory, "to"));
    EXPECT_FALSE(endsWithDir(state->currentDirectory, "from"));
}

TEST(RealModeTransitionsTest, BufferBrowserSelectionCanJumpBackAndForward)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->filename = "one.txt";
    editor.currentBuffer->lines = {"one"};
    editor.createNewBuffer();
    editor.currentBuffer->filename = "two.txt";
    editor.currentBuffer->lines = {"two"};
    editor.switchToBuffer(0);
    *editor.cursorX = 2;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_W));
    ASSERT_STREQ(sm.currentStateName(), "BUFFERS");

    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_EQ(editor.currentBufferIndex, 1);
    ASSERT_EQ(editor.jumpBackStack.size(), 1u);
    ASSERT_STREQ(sm.currentStateName(), "NORMAL");

    sm.dispatch(keyCode(control::ControlKey::CTRL_O));

    EXPECT_EQ(editor.currentBufferIndex, 0);
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_EQ(*editor.cursorX, 2);
    ASSERT_EQ(editor.jumpForwardStack.size(), 1u);

    sm.dispatch(keyCode(control::ControlKey::CTRL_I));

    EXPECT_EQ(editor.currentBufferIndex, 1);
}

TEST(RealModeTransitionsTest, BufferBrowserCtrlXClosesSelectedBuffer)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->filename = "one.txt";
    editor.currentBuffer->lines = {"one"};
    editor.createNewBuffer();
    editor.currentBuffer->filename = "two.txt";
    editor.currentBuffer->lines = {"two"};
    editor.switchToBuffer(0);

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_W));
    ASSERT_STREQ(sm.currentStateName(), "BUFFERS");

    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    sm.dispatch(keyCode(control::ControlKey::CTRL_X));

    ASSERT_EQ(editor.buffers.size(), 1u);
    EXPECT_EQ(editor.currentBufferIndex, 0);
    EXPECT_EQ(editor.currentBuffer->filename, "one.txt");
    EXPECT_STREQ(sm.currentStateName(), "BUFFERS");
}

TEST(RealModeTransitionsTest, BufferBrowserCtrlXLastBufferReturnsWelcome)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->filename = "only.txt";
    editor.currentBuffer->lines = {"only"};

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_W));
    ASSERT_STREQ(sm.currentStateName(), "BUFFERS");

    sm.dispatch(keyCode(control::ControlKey::CTRL_X));

    EXPECT_TRUE(editor.buffers.empty());
    EXPECT_FALSE(editor.hasBuffer());
    EXPECT_EQ(editor.currentMode, WELCOME);
    EXPECT_STREQ(sm.currentStateName(), "WELCOME");
}

TEST(RealModeTransitionsTest, BufferBrowserCtrlShiftXClosesSearchedBuffers)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->filename = "one.cpp";
    editor.currentBuffer->lines = {"one"};
    editor.createNewBuffer();
    editor.currentBuffer->filename = "one.h";
    editor.currentBuffer->lines = {"one header"};
    editor.createNewBuffer();
    editor.currentBuffer->filename = "two.cpp";
    editor.currentBuffer->lines = {"two"};
    editor.switchToBuffer(2);

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_W));
    ASSERT_STREQ(sm.currentStateName(), "BUFFERS");

    sm.dispatch('o');
    sm.dispatch('n');
    sm.dispatch('e');
    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_X));

    ASSERT_EQ(editor.buffers.size(), 1u);
    EXPECT_EQ(editor.currentBufferIndex, 0);
    EXPECT_EQ(editor.currentBuffer->filename, "two.cpp");
    EXPECT_STREQ(sm.currentStateName(), "BUFFERS");
}

TEST(RealModeTransitionsTest, BufferBrowserCtrlShiftXAllMatchesReturnsWelcome)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->filename = "one.cpp";
    editor.currentBuffer->lines = {"one"};
    editor.createNewBuffer();
    editor.currentBuffer->filename = "one.h";
    editor.currentBuffer->lines = {"one header"};

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_W));
    ASSERT_STREQ(sm.currentStateName(), "BUFFERS");

    sm.dispatch('o');
    sm.dispatch('n');
    sm.dispatch('e');
    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_X));

    EXPECT_TRUE(editor.buffers.empty());
    EXPECT_FALSE(editor.hasBuffer());
    EXPECT_EQ(editor.currentMode, WELCOME);
    EXPECT_STREQ(sm.currentStateName(), "WELCOME");
}

#ifdef UVIM_ENABLE_COLOR_TOOLS
TEST(RealModeTransitionsTest, ColorPickerCanJumpToRgbSelector)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {""};

    auto sm = makeMachine(editor, ColorPickerMode{});
    sm.dispatch('l');
    sm.dispatch('l');
    sm.dispatch('s');

    ASSERT_STREQ(sm.currentStateName(), "COLOR_SELECTOR");
    auto* selector = sm.getState<ColorSelectorMode>();
    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->fgRed, 170);
    EXPECT_EQ(selector->fgGreen, 0);
    EXPECT_EQ(selector->fgBlue, 0);
    EXPECT_EQ(selector->active, 0);
}

TEST(RealModeTransitionsTest,
     ColorSelectorCommandRemovesAndReinsertsAnsiLiteralBeforeCursor)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"a \\x1b[31m text"};
    *editor.cursorX = 12;
    *editor.cursorY = 0;
    editor.saveState();

    auto smPtr =
        std::make_unique<ModeStateMachine>(createModeContext(&editor),
                                           NormalMode{});
    ModeStateMachine* sm = smPtr.get();
    editor.setModeStateMachineForTests(std::move(smPtr));
    dispatch_command(*sm, "colorselector");

    ASSERT_STREQ(sm->currentStateName(), "COLOR_SELECTOR");
    auto* selector = sm->getState<ColorSelectorMode>();
    ASSERT_NE(selector, nullptr);
    EXPECT_TRUE(selector->replacing);
    EXPECT_EQ(selector->replaceStartX, 2);
    EXPECT_EQ(selector->replaceLength, 0);
    EXPECT_EQ(editor.currentBuffer->lines[0], "a  text");

    sm->dispatch('L');
    sm->dispatch(keyCode(control::ControlKey::ENTER));

    const std::string expected =
        std::string("a ") + color::rgbLiteralFg(180, 0, 0) +
        color::rgbLiteralBg(0, 0, 0) + " text";
    EXPECT_EQ(editor.currentBuffer->lines[0], expected);
    EXPECT_STREQ(sm->currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, ColorSelectorCommandReadsRgbPairAtCursor)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {
        "\"\\x1b[38;2;255;255;85m\\x1b[48;2;41;66;0m\""};
    *editor.cursorX = 1;
    *editor.cursorY = 0;
    editor.saveState();

    auto smPtr =
        std::make_unique<ModeStateMachine>(createModeContext(&editor),
                                           NormalMode{});
    ModeStateMachine* sm = smPtr.get();
    editor.setModeStateMachineForTests(std::move(smPtr));
    dispatch_command(*sm, "colorselector");

    ASSERT_STREQ(sm->currentStateName(), "COLOR_SELECTOR");
    auto* selector = sm->getState<ColorSelectorMode>();
    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->fgRed, 255);
    EXPECT_EQ(selector->fgGreen, 255);
    EXPECT_EQ(selector->fgBlue, 85);
    EXPECT_EQ(selector->bgRed, 41);
    EXPECT_EQ(selector->bgGreen, 66);
    EXPECT_EQ(selector->bgBlue, 0);
    EXPECT_EQ(editor.currentBuffer->lines[0], "\"\"");
}

TEST(RealModeTransitionsTest, ColorSelectorCommandReadsRgbPairAfterCursor)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {
        "\"\\x1b[38;2;255;255;85m\\x1b[48;2;41;66;0m\""};
    *editor.cursorX = 0;
    *editor.cursorY = 0;
    editor.saveState();

    auto smPtr =
        std::make_unique<ModeStateMachine>(createModeContext(&editor),
                                           NormalMode{});
    ModeStateMachine* sm = smPtr.get();
    editor.setModeStateMachineForTests(std::move(smPtr));
    dispatch_command(*sm, "colorselector");

    ASSERT_STREQ(sm->currentStateName(), "COLOR_SELECTOR");
    auto* selector = sm->getState<ColorSelectorMode>();
    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->fgRed, 255);
    EXPECT_EQ(selector->fgGreen, 255);
    EXPECT_EQ(selector->fgBlue, 85);
    EXPECT_EQ(selector->bgRed, 41);
    EXPECT_EQ(selector->bgGreen, 66);
    EXPECT_EQ(selector->bgBlue, 0);
    EXPECT_EQ(editor.currentBuffer->lines[0], "\"\"");
}

TEST(RealModeTransitionsTest, ColorSelectorCommandReadsActualEscRgbPair)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {
        std::string("\x1b[38;2;255;255;85m\x1b[48;2;41;66;0mtext")};
    *editor.cursorX = 0;
    *editor.cursorY = 0;
    editor.saveState();

    auto smPtr =
        std::make_unique<ModeStateMachine>(createModeContext(&editor),
                                           NormalMode{});
    ModeStateMachine* sm = smPtr.get();
    editor.setModeStateMachineForTests(std::move(smPtr));
    dispatch_command(*sm, "colorselector");

    ASSERT_STREQ(sm->currentStateName(), "COLOR_SELECTOR");
    auto* selector = sm->getState<ColorSelectorMode>();
    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->fgRed, 255);
    EXPECT_EQ(selector->fgGreen, 255);
    EXPECT_EQ(selector->fgBlue, 85);
    EXPECT_EQ(selector->bgRed, 41);
    EXPECT_EQ(selector->bgGreen, 66);
    EXPECT_EQ(selector->bgBlue, 0);
    EXPECT_EQ(editor.currentBuffer->lines[0], "text");
}

TEST(RealModeTransitionsTest, ColorSelectCommandEscLeavesAnsiLiteralRemoved)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"a \\x1b[31m text"};
    *editor.cursorX = 12;
    *editor.cursorY = 0;
    editor.saveState();

    auto smPtr =
        std::make_unique<ModeStateMachine>(createModeContext(&editor),
                                           NormalMode{});
    ModeStateMachine* sm = smPtr.get();
    editor.setModeStateMachineForTests(std::move(smPtr));
    dispatch_command(*sm, "colorselect");

    ASSERT_STREQ(sm->currentStateName(), "COLOR_SELECTOR");
    EXPECT_EQ(editor.currentBuffer->lines[0], "a  text");

    sm->dispatch(keyCode(control::ControlKey::ESC));

    EXPECT_EQ(editor.currentBuffer->lines[0], "a  text");
    EXPECT_STREQ(sm->currentStateName(), "NORMAL");
}
#endif

TEST(RealModeTransitionsTest, UndoBackToSavedClearsDirty)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"one"};
    *editor.cursorX = 0;
    *editor.cursorY = 0;
    editor.saveState();
    editor.currentBuffer->savedUndoIndex = editor.currentBuffer->undoIndex;
    *editor.dirty = false;

    editor.currentBuffer->lines[0] = "two";
    *editor.dirty = true;
    editor.saveState();

    editor.undo();

    EXPECT_FALSE(*editor.dirty);
    EXPECT_EQ(editor.currentBuffer->lines[0], "one");
}

TEST(RealModeTransitionsTest, UndoBackToSavedClearsDirtyWithHash)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"one"};
    *editor.cursorX = 0;
    *editor.cursorY = 0;
    editor.saveState();
    editor.currentBuffer->savedUndoIndex = -1;
    auto hash_lines = [](const std::vector<std::string>& src) -> size_t
    {
        size_t h = 1469598103934665603ull;
        for(const auto& line : src)
        {
            for(unsigned char c : line)
            {
                h ^= c;
                h *= 1099511628211ull;
            }
            h ^= '\n';
            h *= 1099511628211ull;
        }
        return h;
    };
    editor.currentBuffer->savedContentHashValid = true;
    editor.currentBuffer->savedContentHash =
        hash_lines(editor.currentBuffer->lines);
    *editor.dirty = false;

    editor.currentBuffer->lines[0] = "two";
    *editor.dirty = true;
    editor.saveState();

    editor.undo();

    EXPECT_FALSE(*editor.dirty);
    EXPECT_EQ(editor.currentBuffer->lines[0], "one");
}
