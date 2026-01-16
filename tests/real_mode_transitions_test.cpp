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
