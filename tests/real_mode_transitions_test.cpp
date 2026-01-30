#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include <gtest/gtest.h>
#include <utility>

namespace
{
template <typename InitialState>
ModeStateMachine makeMachine(Editor& editor, InitialState&& initial)
{
    return ModeStateMachine(createModeContext(&editor),
                            std::forward<InitialState>(initial));
}
} // namespace

TEST(RealModeTransitionsTest, WelcomeEscStaysInWelcome)
{
    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, WelcomeMode{});

    sm.dispatch(Terminal::ESC);

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

    sm.dispatch(Terminal::ESC);
    EXPECT_EQ(editor.statusMessage, "Message");

    sm.dispatch(Terminal::ESC);
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
