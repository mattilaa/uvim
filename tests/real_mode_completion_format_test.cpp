#include "real_mode_test_utils.h"

using namespace uvim_test;

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

TEST(RealModeTransitionsTest, LeaderMfCollectsMlangFormatErrorsAndJumps)
{
#ifdef _WIN32
    GTEST_SKIP() << "fake mlang-format PATH test is POSIX-only";
#else
    auto root = make_temp_dir("uvim_mlang_format_errors_");
    auto binDir = root / "bin";
    auto source = root / "bad.mla";
    std::filesystem::create_directories(binDir);
    write_file(source, "fn main() {\n  broken\n}\n");

    auto formatCmd = binDir / "mlang-format";
    write_file(formatCmd,
               "#!/bin/sh\n"
               "echo \"$2: [2:3, 2:9) expected token\" >&2\n"
               "exit 1\n");
    std::filesystem::permissions(formatCmd,
                                 std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add);

    const char* oldPath = std::getenv("PATH");
    std::string path = binDir.string();
    if(oldPath && *oldPath)
        path += std::string(":") + oldPath;
    ScopedEnv pathEnv("PATH", path);
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.projectRoot = root.string();
    editor.openFile(source.string());
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    Terminal::unreadKey(keyCode(typed::TypedKey::KEY_F));
    sm.dispatch(keyCode(typed::TypedKey::KEY_M));

    ASSERT_EQ(editor.mlangFormatErrors.size(), 1u);
    EXPECT_STREQ(sm.currentStateName(), "MLANG FORMAT");

    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    ASSERT_TRUE(editor.cursorY);
    ASSERT_TRUE(editor.cursorX);
    EXPECT_EQ(*editor.cursorY, 1);
    EXPECT_EQ(*editor.cursorX, 2);
#endif
}

TEST(RealModeTransitionsTest, LeaderMwOpensMlangWarningsList)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    set_buffer_filename(editor, "/tmp/uvim_mlang_warning_list.mla");
    editor.currentBuffer->lines = {"fn main() {}\n"};
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    Terminal::unreadKey(keyCode(typed::TypedKey::KEY_W));
    sm.dispatch(keyCode(typed::TypedKey::KEY_M));

    EXPECT_STREQ(sm.currentStateName(), "MLANG FORMAT");
    auto* mode = sm.getState<MlangFormatErrorsMode>();
    ASSERT_NE(mode, nullptr);
    EXPECT_TRUE(mode->warnings);
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
