#include "real_mode_test_utils.h"

using namespace uvim_test;

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

TEST(RealModeTransitionsTest, FuzzyFindCtrlOTogglesFilenameFirstRanking)
{
    auto root = make_temp_dir("uvim_fuzzy_filename_first_");
    write_file(root / "foo" / "bar.txt", "a\n");
    write_file(root / "nested" / "foo_target.txt", "b\n");
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    auto sm = makeMachine(editor, FuzzyFindMode{});

    sm.dispatch('f');
    sm.dispatch('o');
    sm.dispatch('o');

    auto* state = sm.getState<FuzzyFindMode>();
    ASSERT_NE(state, nullptr);
    ASSERT_FALSE(state->matches.empty());
    EXPECT_FALSE(state->filenameFirst);

    sm.dispatch(keyCode(control::ControlKey::CTRL_O));

    ASSERT_FALSE(state->matches.empty());
    EXPECT_TRUE(state->filenameFirst);
    EXPECT_EQ(state->matches.front().file.name, "foo_target.txt");
    ASSERT_FALSE(state->matches.front().matchPositions.empty());
    const size_t filenameOffset =
        state->matches.front().file.path.find("foo_target.txt");
    ASSERT_TRUE(text_utils::is_found(filenameOffset));
    EXPECT_GE(state->matches.front().matchPositions.front(),
              (int)filenameOffset);
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
