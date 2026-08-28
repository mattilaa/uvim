#include "editor_mode_controller.h"
#include "real_mode_test_utils.h"

#include <thread>

using namespace uvim_test;

namespace
{
void flushGrepDebounce(GrepSearchMode& state, Editor& editor)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(310));
    state.processIdle(editor);
    state.draw(editor);
}
} // namespace

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

TEST(RealModeTransitionsTest, SearchMatchContrastDefaultsHighAndPersists)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    ASSERT_EQ(editor.searchMatchContrast, 100);

    auto sm = makeMachine(editor, FuzzyFindMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_T));
    EXPECT_EQ(editor.searchMatchContrast, 35);

    sm.dispatch(keyCode(control::ControlKey::CTRL_A));
    ASSERT_STREQ(sm.currentStateName(), "GREP");
    EXPECT_EQ(editor.searchMatchContrast, 35);

    sm.dispatch(keyCode(control::ControlKey::CTRL_T));
    EXPECT_EQ(editor.searchMatchContrast, 100);
    sm.dispatch(keyCode(control::ControlKey::ESC));

    auto reopened = makeMachine(editor, FuzzyFindMode{});
    EXPECT_EQ(editor.searchMatchContrast, 100);
    EXPECT_STREQ(reopened.currentStateName(), "FUZZY");
}

TEST(RealModeTransitionsTest, SetSearchContrastAdjustsSharedPaletteStrength)
{
    Editor editor = Editor::createForTests();

    EXPECT_TRUE(editor.handleSetCommand("set searchcontrast=60"));
    EXPECT_EQ(editor.searchMatchContrast, 60);
    EXPECT_TRUE(editor.handleSetCommand("set searchcontrast+=25"));
    EXPECT_EQ(editor.searchMatchContrast, 85);
    EXPECT_TRUE(editor.handleSetCommand("set searchcontrast-=100"));
    EXPECT_EQ(editor.searchMatchContrast, 0);
    EXPECT_TRUE(editor.handleSetCommand("set searchcontrast=101"));
    EXPECT_EQ(editor.searchMatchContrast, 0);
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

TEST(RealModeTransitionsTest, GrepSearchCtrlAEntersBeforeIndexing)
{
    auto root = make_temp_dir("uvim_grep_lazy_");
    write_file(root / "a.txt", "needle\n");
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    editor.createNewBuffer();

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_A));

    EXPECT_STREQ(sm.currentStateName(), "GREP");
    EXPECT_FALSE(editor.grepFileIndexInitialized);

    for(char c : std::string("needle"))
        sm.dispatch(c);

    auto* state = sm.getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);
    flushGrepDebounce(*state, editor);
    ASSERT_EQ(state->matches.size(), 1u);
    EXPECT_TRUE(text_utils::is_found(state->matches[0].filepath.find("a.txt")));
}

TEST(RealModeTransitionsTest, GrepSearchCtrlAReentersWithFreshVisibleState)
{
    auto root = make_temp_dir("uvim_grep_reenter_");
    write_file(root / "a.txt", "needle\n");
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    editor.createNewBuffer();

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_A));
    ASSERT_STREQ(sm.currentStateName(), "GREP");

    sm.dispatch(keyCode(control::ControlKey::ESC));
    ASSERT_STREQ(sm.currentStateName(), "NORMAL");

    sm.dispatch(keyCode(control::ControlKey::CTRL_A));
    EXPECT_STREQ(sm.currentStateName(), "GREP");
    auto* state = sm.getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(state->query.empty());
    EXPECT_TRUE(state->matches.empty());
    EXPECT_TRUE(editor.needsFullRedraw);
}

TEST(RealModeTransitionsTest, DiscardPendingInputClearsBufferedKeys)
{
    Terminal::unreadKey('x');
    ASSERT_TRUE(Terminal::hasBufferedKeys());

    Terminal::discardPendingInput();

    EXPECT_FALSE(Terminal::hasBufferedKeys());
}

TEST(RealModeTransitionsTest, FuzzyFindReusesInMemoryFileIndexUntilRefresh)
{
    auto root = make_temp_dir("uvim_fuzzy_memory_cache_");
    auto otherRoot = make_temp_dir("uvim_fuzzy_other_project_");
    write_file(root / "alpha.txt", "alpha\n");
    write_file(otherRoot / "gamma.txt", "gamma\n");
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    editor.createNewBuffer();

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_P));
    auto* fuzzy = sm.getState<FuzzyFindMode>();
    ASSERT_NE(fuzzy, nullptr);
    ASSERT_EQ(fuzzy->projectFiles.size(), 1u);

    sm.dispatch(keyCode(control::ControlKey::ESC));
    ASSERT_TRUE(editor.fuzzyFileIndexInitialized);
    ASSERT_EQ(editor.fuzzyProjectFiles.size(), 1u);

    write_file(root / "beta.txt", "beta\n");
    sm.dispatch(keyCode(control::ControlKey::CTRL_P));
    fuzzy = sm.getState<FuzzyFindMode>();
    ASSERT_NE(fuzzy, nullptr);
    EXPECT_EQ(fuzzy->projectFiles.size(), 1u);

    fuzzy->refreshFileIndex(editor);
    EXPECT_EQ(fuzzy->projectFiles.size(), 2u);

    sm.dispatch(keyCode(control::ControlKey::ESC));
    std::filesystem::current_path(otherRoot);
    sm.dispatch(keyCode(control::ControlKey::CTRL_P));
    fuzzy = sm.getState<FuzzyFindMode>();
    ASSERT_NE(fuzzy, nullptr);
    ASSERT_EQ(fuzzy->projectFiles.size(), 1u);
    EXPECT_EQ(fuzzy->projectFiles.front().name, "gamma.txt");
}

TEST(RealModeTransitionsTest, CtrlAOpensGrepSearchFromInsertMode)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"abc"};
    *editor.cursorX = 3;

    auto sm = makeMachine(editor, InsertMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_A));

    EXPECT_STREQ(sm.currentStateName(), "GREP");
    auto* state = sm.getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);

    sm.dispatch('v');

    EXPECT_EQ(state->query, "v");
    EXPECT_EQ(editor.currentBuffer->lines[0], "abc");
}

TEST(RealModeTransitionsTest, CtrlAOpensGrepSearchFromReplaceMode)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"abc"};
    *editor.cursorX = 1;

    auto sm = makeMachine(editor, ReplaceMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_A));

    EXPECT_STREQ(sm.currentStateName(), "GREP");
    auto* state = sm.getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);

    sm.dispatch('v');

    EXPECT_EQ(state->query, "v");
    EXPECT_EQ(editor.currentBuffer->lines[0], "abc");
}

TEST(RealModeTransitionsTest, ControllerCtrlAOpensGrepEvenWithStaleModeState)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"abc"};
    *editor.cursorX = 3;
    editor.setModeStateMachineForTests(std::make_unique<ModeStateMachine>(
        createModeContext(&editor), InsertMode{}));
    editor.currentMode = NORMAL;
    EditorModeController controller(editor);

    controller.handleKeypress(keyCode(control::ControlKey::CTRL_A));

    auto* sm = editor.getModeStateMachine();
    ASSERT_NE(sm, nullptr);
    EXPECT_STREQ(sm->currentStateName(), "GREP");
    EXPECT_EQ(editor.currentMode, GREP_SEARCH);

    controller.handleKeypress('v');

    auto* state = sm->getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->query, "v");
    EXPECT_EQ(editor.currentBuffer->lines[0], "abc");
}

TEST(RealModeTransitionsTest,
     ControllerCtrlAReopensGrepWhenCurrentModeIsStaleGrep)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"abc"};
    editor.setModeStateMachineForTests(std::make_unique<ModeStateMachine>(
        createModeContext(&editor), NormalMode{}));
    editor.currentMode = GREP_SEARCH;
    EditorModeController controller(editor);

    controller.handleKeypress(keyCode(control::ControlKey::CTRL_A));

    auto* sm = editor.getModeStateMachine();
    ASSERT_NE(sm, nullptr);
    EXPECT_STREQ(sm->currentStateName(), "GREP");
    EXPECT_EQ(editor.currentMode, GREP_SEARCH);

    controller.handleKeypress('v');

    auto* state = sm->getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->query, "v");
    EXPECT_EQ(editor.currentBuffer->lines[0], "abc");
}

TEST(RealModeTransitionsTest, GrepSearchBatchesQueuedPrintableInput)
{
    auto root = make_temp_dir("uvim_grep_batch_");
    write_file(root / "a.txt", "needle\n");
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    editor.createNewBuffer();

    auto sm = makeMachine(editor, GrepSearchMode{});
    Terminal::unreadKey('e');
    Terminal::unreadKey('l');
    Terminal::unreadKey('d');
    Terminal::unreadKey('e');
    Terminal::unreadKey('e');

    sm.dispatch('n');

    auto* state = sm.getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->query, "needle");
    flushGrepDebounce(*state, editor);
    ASSERT_EQ(state->matches.size(), 1u);
    EXPECT_FALSE(Terminal::hasBufferedKeys());
}

TEST(RealModeTransitionsTest, GrepOpenSeedsSingleSearchMatchLazily)
{
    auto root = make_temp_dir("uvim_grep_open_seed_");
    write_file(root / "a.txt", "needle one\nother\nneedle two\n");
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    editor.createNewBuffer();

    auto sm = makeMachine(editor, GrepSearchMode{});
    for(char c : std::string("needle"))
        sm.dispatch(c);

    auto* state = sm.getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);
    flushGrepDebounce(*state, editor);
    ASSERT_EQ(state->matches.size(), 2u);

    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    EXPECT_TRUE(
        text_utils::is_found(editor.currentBuffer->filename.find("a.txt")));
    EXPECT_EQ(*editor.cursorY, 0);
    ASSERT_EQ(editor.searchMatches.size(), 1u);
    EXPECT_TRUE(editor.searchMatchesPartial);
    EXPECT_EQ(editor.searchMatches[0].row, 0);
    EXPECT_EQ(editor.searchMatches[0].col, 0);

    sm.dispatch(keyCode(typed::TypedKey::KEY_N));

    EXPECT_FALSE(editor.searchMatchesPartial);
    ASSERT_EQ(editor.searchMatches.size(), 2u);
    EXPECT_EQ(*editor.cursorY, 2);
}

#ifdef UVIM_ENABLE_RG_CACHE
TEST(RealModeTransitionsTest, OpenFilePromotesMatchingRgCacheLines)
{
    auto root = make_temp_dir("uvim_rg_cache_open_");
    write_file(root / "a.txt", "disk line\n");
    ScopedCurrentPath cwd(root);

    std::error_code ec;
    const auto size = std::filesystem::file_size(root / "a.txt", ec);
    ASSERT_FALSE(ec);
    const auto mtime = std::filesystem::last_write_time(root / "a.txt", ec);
    ASSERT_FALSE(ec);

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.rgCacheLoaded = true;

    Editor::RgCachedFile cached;
    cached.path = "a.txt";
    cached.size = size;
    cached.mtime = static_cast<long long>(mtime.time_since_epoch().count());
    cached.lines = {"rg cached"};
    cached.lowerLines = {"rg cached"};
    editor.rgCachedFiles.push_back(std::move(cached));

    editor.openFile("a.txt", false);

    ASSERT_NE(editor.currentBuffer, nullptr);
    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "rg cached");
}

TEST(RealModeTransitionsTest, GrepSearchBuildsAndUpdatesRgCache)
{
    auto root = make_temp_dir("uvim_rg_cache_");
    write_file(root / "a.txt", "alpha needle\n");
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    editor.createNewBuffer();

    auto sm = makeMachine(editor, GrepSearchMode{});
    for(char c : std::string("needle"))
        sm.dispatch(c);

    auto* state = sm.getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);
    flushGrepDebounce(*state, editor);
    ASSERT_EQ(state->matches.size(), 1u);
    EXPECT_TRUE(std::filesystem::exists(root / ".rg" / "index.tsv"));

    write_file(root / "a.txt", "alpha\n");
    state->refreshFileIndex(editor);
    sm.dispatch(keyCode(control::ControlKey::BACKSPACE));
    sm.dispatch('e');

    state = sm.getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);
    flushGrepDebounce(*state, editor);
    EXPECT_TRUE(state->matches.empty());
}

TEST(RealModeTransitionsTest, GrepSearchIndexesMultipleFilesConsistently)
{
    auto root = make_temp_dir("uvim_rg_cache_workers_");
    constexpr int fileCount = 24;
    for(int i = 0; i < fileCount; ++i)
    {
        write_file(root / ("file_" + std::to_string(i) + ".txt"),
                   "parallel needle " + std::to_string(i) + "\n");
    }
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    editor.createNewBuffer();

    auto sm = makeMachine(editor, GrepSearchMode{});
    for(char c : std::string("needle"))
        sm.dispatch(c);

    auto* state = sm.getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);
    flushGrepDebounce(*state, editor);

    EXPECT_EQ(state->matches.size(), static_cast<size_t>(fileCount));
    EXPECT_EQ(editor.rgCachedFiles.size(), static_cast<size_t>(fileCount));
    EXPECT_TRUE(std::filesystem::exists(root / ".rg" / "index.tsv"));
}
#endif
