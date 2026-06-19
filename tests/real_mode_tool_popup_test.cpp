#include "real_mode_test_utils.h"

using namespace uvim_test;

TEST(RealModeTransitionsTest, GlyphSelectCommandOpensPopup)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {""};

    auto smPtr =
        std::make_unique<ModeStateMachine>(createModeContext(&editor),
                                           NormalMode{});
    ModeStateMachine* sm = smPtr.get();
    editor.setModeStateMachineForTests(std::move(smPtr));
    dispatch_command(*sm, "glyphselect");

    EXPECT_STREQ(sm->currentStateName(), "GLYPH_SELECT");
}

TEST(RealModeTransitionsTest, GlyphSelectEnterInsertsSelectedGlyph)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {""};

    auto sm = makeMachine(editor, GlyphSelectMode{});
    sm.dispatch('l');
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_EQ(editor.currentBuffer->lines[0], "\xE2\x86\x92");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, GlyphSelectJumpsRowsWithJAndK)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {""};

    auto sm = makeMachine(editor, GlyphSelectMode{});
    sm.dispatch('j');
    sm.dispatch('k');
    sm.dispatch('l');
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_EQ(editor.currentBuffer->lines[0], "\xE2\x86\x92");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, GlyphSelectEscCancelsWithoutInsert)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {""};

    auto sm = makeMachine(editor, GlyphSelectMode{});
    sm.dispatch('l');
    sm.dispatch(keyCode(control::ControlKey::ESC));

    EXPECT_EQ(editor.currentBuffer->lines[0], "");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

#ifdef UVIM_ENABLE_COLOR_TOOLS

TEST(RealModeTransitionsTest, AnsiToolsCommandOpensPopup)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {""};

    auto smPtr =
        std::make_unique<ModeStateMachine>(createModeContext(&editor),
                                           NormalMode{});
    ModeStateMachine* sm = smPtr.get();
    editor.setModeStateMachineForTests(std::move(smPtr));
    dispatch_command(*sm, "ansitools");

    EXPECT_STREQ(sm->currentStateName(), "ANSI_TOOLS");
}

TEST(RealModeTransitionsTest, AnsiToolsEnterInsertsSelectedLiteral)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {""};

    auto sm = makeMachine(editor, AnsiToolsMode{});
    sm.dispatch('j');
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_EQ(editor.currentBuffer->lines[0], "\\x1b[39m");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, AnsiToolsEscCancelsWithoutInsert)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {""};

    auto sm = makeMachine(editor, AnsiToolsMode{});
    sm.dispatch('j');
    sm.dispatch(keyCode(control::ControlKey::ESC));

    EXPECT_EQ(editor.currentBuffer->lines[0], "");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

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

TEST(RealModeTransitionsTest, VisualLeaderColorSelectWrapsSelection)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"hello"};
    *editor.cursorX = 1;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, VisualMode{});
    sm.dispatch('l');
    sm.dispatch('l');
    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch('c');
    sm.dispatch('s');

    ASSERT_STREQ(sm.currentStateName(), "COLOR_SELECTOR");
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    const std::string code =
        color::rgbLiteralFg(255, 255, 255) + color::rgbLiteralBg(0, 0, 0);
    EXPECT_EQ(editor.currentBuffer->lines[0],
              std::string("h") + code + "ell" +
                  color::literal(color::AnsiColor::Reset) + "o");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, VisualLeaderColorSelectWrapsUtf8Glyphs)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"std::cout << \"┌────┐\\n\";"};
    editor.utf8Mode = true;
    *editor.cursorX = (int)editor.currentBuffer->lines[0].find("┌");
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, VisualMode{});
    sm.dispatch('l');
    sm.dispatch('l');
    sm.dispatch('l');
    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch('c');
    sm.dispatch('s');

    ASSERT_STREQ(sm.currentStateName(), "COLOR_SELECTOR");
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    const std::string code =
        color::rgbLiteralFg(255, 255, 255) + color::rgbLiteralBg(0, 0, 0);
    EXPECT_EQ(editor.currentBuffer->lines[0],
              std::string("std::cout << \"") + code + "┌───" +
                  color::literal(color::AnsiColor::Reset) + "─┐\\n\";");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, VisualCommandColorPickerWrapsSelection)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"hello"};
    *editor.cursorX = 1;
    *editor.cursorY = 0;

    auto smPtr =
        std::make_unique<ModeStateMachine>(createModeContext(&editor),
                                           VisualMode{});
    ModeStateMachine* sm = smPtr.get();
    editor.setModeStateMachineForTests(std::move(smPtr));

    sm->dispatch('l');
    sm->dispatch('l');
    sm->dispatch(':');
    for(char ch : std::string("colorpicker"))
        sm->dispatch(ch);
    sm->dispatch(keyCode(control::ControlKey::ENTER));

    ASSERT_STREQ(sm->currentStateName(), "COLOR_PICKER");
    sm->dispatch('l');
    sm->dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_EQ(editor.currentBuffer->lines[0],
              std::string("h") + color::literal(color::AnsiColor::FgBlack) +
                  "ell" + color::literal(color::AnsiColor::Reset) + "o");
    EXPECT_STREQ(sm->currentStateName(), "NORMAL");
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
    dispatch_command(*sm, "colorselect");

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
    dispatch_command(*sm, "colorselect");

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
    dispatch_command(*sm, "colorselect");

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
    dispatch_command(*sm, "colorselect");

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
