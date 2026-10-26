#include "real_mode_test_utils.h"

#include <thread>

using namespace uvim_test;

namespace
{
void flushGrepDebounce(GrepSearchMode& state, Editor& editor)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(310));
    state.processIdle(editor);
    // Searches and rg cache indexing run on the task queue; apply finished
    // jobs the way the editor main loop does.
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while(!editor.tasks.idle() && std::chrono::steady_clock::now() < deadline)
    {
        editor.tasks.pollCompletions();
        state.processIdle(editor);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    editor.tasks.pollCompletions();
    state.processIdle(editor);
    state.draw(editor);
}
} // namespace

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

TEST(RealModeTransitionsTest, VisualPasteReplacesManyWithSameYank)
{
    // v E y once, then v E p on other lines: every replacement uses the
    // originally yanked word (the replaced text must not be yanked).
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.useSystemClipboard = false;
    editor.currentBuffer
        ->lines = {"someWord first", "other line", "third row", "fourth row"};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('v');
    sm.dispatch('E');
    sm.dispatch('y');
    EXPECT_EQ(editor.yankBuffer, "someWord");

    for(int row = 1; row <= 3; ++row)
    {
        *editor.cursorY = row;
        *editor.cursorX = 0;
        sm.dispatch('v');
        sm.dispatch('E');
        sm.dispatch('p');
        EXPECT_EQ(editor.yankBuffer, "someWord") << "row " << row;
    }

    EXPECT_EQ(editor.currentBuffer->lines[1], "someWord line");
    EXPECT_EQ(editor.currentBuffer->lines[2], "someWord row");
    EXPECT_EQ(editor.currentBuffer->lines[3], "someWord row");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, VisualInnerWordReplacesOnlyTheWord)
{
    // yiw on "some" (cursor mid-word), then viwp on other words: only the
    // word is replaced, "->blah" stays, and the yank is reused each time.
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.useSystemClipboard = false;
    editor.currentBuffer
        ->lines = {"some->blah", "someElse->blah", "anotherOne->blah"};
    *editor.cursorX = 2;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('y');
    sm.dispatch('i');
    sm.dispatch('w');
    EXPECT_EQ(editor.yankBuffer, "some");

    for(int row = 1; row <= 2; ++row)
    {
        *editor.cursorY = row;
        *editor.cursorX = 3; // inside the word
        sm.dispatch('v');
        sm.dispatch('i');
        sm.dispatch('w');
        sm.dispatch('p');
        EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    }
    EXPECT_EQ(editor.currentBuffer->lines[1], "some->blah");
    EXPECT_EQ(editor.currentBuffer->lines[2], "some->blah");
    EXPECT_EQ(editor.yankBuffer, "some");
}

TEST(RealModeTransitionsTest, WordTextObjectsFollowVimClasses)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"x = obj->field + 1"};
    *editor.cursorY = 0;
    int sy, sx, ey, ex;

    *editor.cursorX = 5; // 'b' in obj
    ASSERT_TRUE(editor.getTextObjectRange('w', false, sy, sx, ey, ex));
    EXPECT_EQ(sx, 4);
    EXPECT_EQ(ex, 6) << "iw: just 'obj'";

    *editor.cursorX = 7; // '-' of '->'
    ASSERT_TRUE(editor.getTextObjectRange('w', false, sy, sx, ey, ex));
    EXPECT_EQ(sx, 7);
    EXPECT_EQ(ex, 8) << "iw on punctuation: the '->' run";

    *editor.cursorX = 5;
    ASSERT_TRUE(editor.getTextObjectRange('W', false, sy, sx, ey, ex));
    EXPECT_EQ(sx, 4);
    EXPECT_EQ(ex, 13) << "iW: 'obj->field'";

    *editor.cursorX = 10; // in 'field'
    ASSERT_TRUE(editor.getTextObjectRange('w', true, sy, sx, ey, ex));
    EXPECT_EQ(sx, 9);
    EXPECT_EQ(ex, 14) << "aw: 'field' plus the trailing space";
}

TEST(RealModeTransitionsTest, OperatorYankReachesSystemClipboard)
{
    // With the system clipboard on, yiw must copy to it; otherwise viwp
    // (which prefers the clipboard) pastes older clipboard contents.
#ifdef _WIN32
    GTEST_SKIP()
        << "System clipboard command lookup is not implemented on Windows";
#else
    auto root = make_temp_dir("uvim_clipboard_yiw_");
    auto binDir = root / "bin";
    auto clipFile = root / "clipboard.txt";
    std::filesystem::create_directories(binDir);
    write_file(clipFile, "stale clipboard");

    auto makeScript = [&](const std::string& name, const std::string& body)
    {
        auto path = binDir / name;
        write_file(path, "#!/bin/sh\n" + body + "\n");
        std::filesystem::permissions(
            path,
            std::filesystem::perms::owner_exec |
                std::filesystem::perms::owner_read |
                std::filesystem::perms::owner_write,
            std::filesystem::perm_options::add);
    };
#ifdef __APPLE__
    makeScript("pbcopy", "cat > \"$UVIM_TEST_CLIPBOARD\"");
    makeScript("pbpaste", "cat \"$UVIM_TEST_CLIPBOARD\"");
#else
    // One fake xclip for both directions: "-o" reads, otherwise writes.
    makeScript(
        "xclip",
        "for a in \"$@\"; do [ \"$a\" = -o ] && "
        "exec cat \"$UVIM_TEST_CLIPBOARD\"; done\n"
        "cat > \"$UVIM_TEST_CLIPBOARD\"");
#endif

    const char* oldPath = std::getenv("PATH");
    std::string path = binDir.string();
    if(oldPath && *oldPath)
        path += std::string(":") + oldPath;
    ScopedEnv pathEnv("PATH", path);
    ScopedEnv clipEnv("UVIM_TEST_CLIPBOARD", clipFile.string());

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.useSystemClipboard = true;
    editor.currentBuffer->lines = {"some->blah", "someElse->foo"};
    *editor.cursorX = 1;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('y');
    sm.dispatch('i');
    sm.dispatch('w');
    EXPECT_EQ(read_file(clipFile), "some");

    *editor.cursorY = 1;
    *editor.cursorX = 3;
    sm.dispatch('v');
    sm.dispatch('i');
    sm.dispatch('w');
    sm.dispatch('p');
    EXPECT_EQ(editor.currentBuffer->lines[1], "some->foo");
    EXPECT_EQ(read_file(clipFile), "some") << "the replaced word stays out";
#endif
}

TEST(RealModeTransitionsTest, VisualCapitalYYanksWholeLines)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.useSystemClipboard = false;
    editor.currentBuffer->lines = {"alpha beta", "gamma delta"};
    *editor.cursorX = 6;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('v');
    sm.dispatch('j');
    sm.dispatch('Y');
    EXPECT_EQ(editor.yankBuffer, "alpha beta\ngamma delta\n");
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
    std::filesystem::permissions(
        pasteCmd,
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
    std::filesystem::permissions(
        pasteCmd,
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
    std::filesystem::permissions(
        pasteCmd,
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
    EXPECT_GE(
        state->matches.front().matchPositions.front(),
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
    flushGrepDebounce(*state, editor);
    ASSERT_EQ(state->matches.size(), 1u);
    EXPECT_TRUE(
        text_utils::is_found(
            state->matches.front().filepath.find("match.txt")));
    EXPECT_EQ(state->matches.front().lineNumber, 1);
}
