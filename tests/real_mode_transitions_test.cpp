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

TEST(RealModeTransitionsTest, ExCommandOpensFileBrowser)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(':');
    sm.dispatch('E');
    sm.dispatch('x');
    sm.dispatch(Terminal::ENTER);

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
    sm.dispatch(Terminal::ENTER);

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
    sm.dispatch(Terminal::ENTER);

    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
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
