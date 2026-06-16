#include "real_mode_test_utils.h"

using namespace uvim_test;

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
