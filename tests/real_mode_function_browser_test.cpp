#include "real_mode_test_utils.h"

#ifdef UVIM_ENABLE_BROWSER_TOOLS
using namespace uvim_test;

namespace
{
class FunctionBrowserTest : public ::testing::Test
{
protected:
    Editor editor = Editor::createForTests();

    void SetUp() override
    {
        editor.createNewBuffer();
        set_buffer_filename(editor, "/tmp/uvim_functions_unsaved.py");
        *editor.lines = {"# Unsaved buffer",
                         "def first(value: int):",
                         "    return value",
                         "",
                         "def second(name: str = \"Ada\"):",
                         "    return name"};
        *editor.dirty = true;
        editor.screenRows = 24;
        editor.screenCols = 100;
    }
};
} // namespace

TEST_F(FunctionBrowserTest, CommandAndLeaderOpenTheCurrentUnsavedOutline)
{
    auto sm = makeMachine(editor, NormalMode{});
    dispatch_command(sm, "functions");
    ASSERT_STREQ(sm.currentStateName(), "FUNCTIONS");
    auto* browser = sm.getState<FunctionBrowserMode>();
    ASSERT_NE(browser, nullptr);
    ASSERT_EQ(browser->symbols.size(), 2u);
    EXPECT_EQ(browser->symbols[1].signature,
              "def second(name: str = \"Ada\"):");
    EXPECT_EQ(modeForState(sm.state()), FUNCTION_BROWSER);
    sm.dispatch(keyCode(control::ControlKey::ESC));
    EXPECT_TRUE(*editor.dirty);
    sm.dispatch(' ');
    sm.dispatch('o');
    EXPECT_STREQ(sm.currentStateName(), "FUNCTIONS");
}

TEST_F(FunctionBrowserTest, BrowseAndJumpPreservesCodeAndSupportsJumpHistory)
{
    *editor.cursorY = 0;
    *editor.cursorX = 2;
    const auto original = *editor.lines;
    auto sm = makeMachine(editor, FunctionBrowserMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_EQ(*editor.cursorY, 0); // Browsing doesn't move the editor cursor.
    sm.dispatch(keyCode(control::ControlKey::CTRL_K));
    EXPECT_EQ(sm.getState<FunctionBrowserMode>()->cursor, 0);
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    EXPECT_EQ(*editor.cursorY, 4);
    EXPECT_EQ(*editor.cursorX, 4);
    EXPECT_EQ(*editor.wantedX, 4);
    EXPECT_EQ(*editor.lines, original);
    EXPECT_TRUE(*editor.dirty);
    ASSERT_EQ(editor.jumpBackStack.size(), 1u);
    sm.dispatch(keyCode(control::ControlKey::CTRL_O));
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_EQ(*editor.cursorX, 2);
    sm.dispatch(keyCode(control::ControlKey::CTRL_I));
    EXPECT_EQ(*editor.cursorY, 4);
}

TEST_F(FunctionBrowserTest, FilteringClearingAndEmptyResults)
{
    auto sm = makeMachine(editor, FunctionBrowserMode{});
    for(char c : std::string("second"))
        sm.dispatch(c);
    auto* browser = sm.getState<FunctionBrowserMode>();
    ASSERT_EQ(browser->matches.size(), 1u);
    EXPECT_EQ(browser->matches[0], 1);
    sm.dispatch(keyCode(control::ControlKey::CTRL_U));
    EXPECT_EQ(browser->matches.size(), 2u);
    sm.dispatch('z');
    EXPECT_TRUE(browser->matches.empty());
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    sm.dispatch(keyCode(control::ControlKey::CTRL_K));
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_STREQ(sm.currentStateName(), "FUNCTIONS");
    EXPECT_EQ(browser->cursor, 0);
    sm.dispatch(keyCode(control::ControlKey::BACKSPACE));
    EXPECT_EQ(browser->matches.size(), 2u);
    sm.dispatch(keyCode(control::ControlKey::ESC));
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_TRUE(editor.jumpBackStack.empty());
}

TEST_F(FunctionBrowserTest,
       OpeningFunctionCentersCodeByDefaultAndHonorsGdcenter)
{
    *editor.lines = std::vector<std::string>(120, "# context");
    (*editor.lines)[60] = "def target():";
    (*editor.lines)[61] = "    return 1";
    ASSERT_TRUE(editor.gdCenterScreen);
    auto sm = makeMachine(editor, FunctionBrowserMode{});
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_EQ(*editor.cursorY, 60);
    EXPECT_EQ(*editor.cursorY - *editor.offsetY, editor.contentRows() / 2);
    sm.dispatch(keyCode(control::ControlKey::CTRL_O));
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_EQ(*editor.offsetY, 0);

    dispatch_command(sm, "set gdcenter=false");
    ASSERT_FALSE(editor.gdCenterScreen);
    dispatch_command(sm, "functions");
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_EQ(*editor.cursorY, 60);
    EXPECT_EQ(*editor.cursorY - *editor.offsetY, editor.contentRows() - 1);
}

TEST_F(FunctionBrowserTest, StartsAtCurrentFunctionAndScrollsInSmallTerminal)
{
    editor.lines->clear();
    for(int i = 0; i < 60; ++i)
    {
        editor.lines->push_back("def function_" + std::to_string(i) + "():");
        editor.lines->push_back("    return 0");
    }
    *editor.cursorY = 71;
    editor.screenRows = 8;
    editor.screenCols = 40;
    auto sm = makeMachine(editor, FunctionBrowserMode{});
    auto* browser = sm.getState<FunctionBrowserMode>();
    ASSERT_NE(browser, nullptr);
    EXPECT_EQ(browser->cursor, 35);
    EXPECT_GT(browser->offset, 0);
    for(int i = 0; i < 30; ++i)
        sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_EQ(browser->cursor, 59);
    sm.dispatch(keyCode(navigation::NavigationKey::PAGE_UP));
    EXPECT_LT(browser->cursor, 59);
    testing::internal::CaptureStdout();
    browser->draw(editor);
    const std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("Functions:"), std::string::npos);
    EXPECT_NE(output.find("function_"), std::string::npos);
    EXPECT_NE(output.find("FUNCTIONS"), std::string::npos);
}

TEST_F(FunctionBrowserTest,
       SignaturesUseThemeSyntaxColorsAndKeepSelectionBackground)
{
    auto sm = makeMachine(editor, FunctionBrowserMode{});
    auto* browser = sm.getState<FunctionBrowserMode>();
    ASSERT_NE(browser, nullptr);
    testing::internal::CaptureStdout();
    browser->draw(editor);
    const std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find(editor.theme.syntax(TOKEN_KEYWORD) + "def"),
              std::string::npos);
    EXPECT_NE(output.find(editor.theme.syntax(TOKEN_FUNCTION) + "first"),
              std::string::npos);
    EXPECT_NE(output.find(editor.theme.syntax(TOKEN_STRING) + "\"Ada\""),
              std::string::npos);
    EXPECT_NE(output.find(editor.theme.uiGutter() + "> 2  "),
              std::string::npos);
    const auto selection = output.find(editor.theme.selection());
    const auto signatureEnd = output.find("value", selection);
    ASSERT_NE(selection, std::string::npos);
    ASSERT_NE(signatureEnd, std::string::npos);
    EXPECT_EQ(output.substr(selection, signatureEnd - selection)
                  .find(editor.theme.reset()),
              std::string::npos);
}

TEST_F(FunctionBrowserTest, UnsupportedAndBufferlessViewsAreSafeToCancel)
{
    set_buffer_filename(editor, "/tmp/functions.json");
    auto sm = makeMachine(editor, FunctionBrowserMode{});
    ASSERT_TRUE(sm.getState<FunctionBrowserMode>()->symbols.empty());
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    sm.dispatch(keyCode(control::ControlKey::ESC));
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");

    Editor empty = Editor::createForTests();
    auto emptyMachine = makeMachine(empty, WelcomeMode{});
    dispatch_command(emptyMachine, "functions");
    ASSERT_STREQ(emptyMachine.currentStateName(), "FUNCTIONS");
    empty.ensureBufferForMode(FUNCTION_BROWSER);
    EXPECT_FALSE(empty.hasBuffer());
    emptyMachine.dispatch(keyCode(control::ControlKey::ESC));
    EXPECT_STREQ(emptyMachine.currentStateName(), "WELCOME");
}

#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
TEST_F(FunctionBrowserTest, CommandPaletteCanOpenTheOutline)
{
    auto sm = makeMachine(editor, NormalMode{});
    dispatch_command(sm, "palette");
    editor.currentMode = COMMAND_PALETTE;
    for(char c : std::string("Browse function signatures"))
        sm.dispatch(c);
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_STREQ(sm.currentStateName(), "FUNCTIONS");
}
#endif

#ifdef UVIM_ENABLE_MULTI_PANE_SPLITS
TEST_F(FunctionBrowserTest, OpeningFunctionCentersWithinActiveHorizontalSplit)
{
    *editor.lines = std::vector<std::string>(120, "# context");
    (*editor.lines)[60] = "def target():";
    (*editor.lines)[61] = "    return 1";
    editor.enableSplit(false);
    ASSERT_TRUE(editor.splitActive);
    const int active = editor.activePane;
    const int other = active == 0 ? 1 : 0;
    const int otherOffset = editor.splitPanes[other].offsetY;
    auto sm = makeMachine(editor, FunctionBrowserMode{});
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_EQ(*editor.cursorY, 60);
    EXPECT_EQ(*editor.cursorY - *editor.offsetY, editor.contentRows() / 2);
    EXPECT_EQ(editor.splitPanes[other].offsetY, otherOffset);
}

TEST_F(FunctionBrowserTest, JumpChangesOnlyTheActiveSplitPane)
{
    editor.enableSplit(true);
    ASSERT_TRUE(editor.splitActive);
    const int active = editor.activePane;
    const int other = active == 0 ? 1 : 0;
    const int otherRow = editor.splitPanes[other].cursorY;
    auto sm = makeMachine(editor, FunctionBrowserMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_EQ(editor.activePane, active);
    EXPECT_EQ(*editor.cursorY, 4);
    EXPECT_EQ(editor.splitPanes[other].cursorY, otherRow);
}
#endif
#endif
