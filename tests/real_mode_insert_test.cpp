#include "real_mode_test_utils.h"

using namespace uvim_test;

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

TEST(RealModeTransitionsTest, EmojiAcceptInNormalModeStaysNormal)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"ab"};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    Editor::EmojiPopupEntry entry;
    entry.emoji = "\xF0\x9F\x98\x80"; // 😀
    entry.emojiDisplay = entry.emoji;
    entry.name = "grinning_face";
    entry.label = entry.emoji + " " + entry.name;
    editor.emojiEntries = {entry};
    editor.emojiFiltered = {0};
    editor.emojiSelected = 0;
    editor.emojiPopupActive = true;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0],
              std::string("a") + "\xF0\x9F\x98\x80" + "b");
    EXPECT_EQ(*editor.cursorX, 1);
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, EscFromInsertAfterEmojiMovesToUtf8Boundary)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {std::string("a") + "\xF0\x9F\x98\x80" + "b"};
    *editor.cursorX = 5; // between emoji and 'b'
    *editor.cursorY = 0;
    editor.utf8Mode = true;

    auto sm = makeMachine(editor, InsertMode{});
    sm.dispatch(keyCode(control::ControlKey::ESC));

    EXPECT_EQ(*editor.cursorX, 1); // emoji start, not inside UTF-8 bytes
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, AutoBraceInsertUsesIndentWidth)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"fn main()"};
    set_buffer_filename(editor, "main.mla");
    editor.currentBuffer->clangIndentWidthValid = true;
    editor.currentBuffer->clangIndentWidth = 4;
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('A');
    sm.dispatch('{');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "fn main(){");
    EXPECT_EQ(editor.currentBuffer->lines[1], "    ");
    EXPECT_EQ(editor.currentBuffer->lines[2], "}");
    EXPECT_EQ(*editor.cursorY, 1);
    EXPECT_EQ(*editor.cursorX, 4);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, EnterAfterCppBraceJumpsToColumn4)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.autoBraces = false;
    editor.currentBuffer->lines = {"fn main() {"};
    set_buffer_filename(editor, "main.cpp");
    editor.currentBuffer->clangIndentWidthValid = true;
    editor.currentBuffer->clangIndentWidth = 4;
    *editor.cursorX = (int)editor.currentBuffer->lines[0].size();
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, InsertMode{});
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 2u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "fn main() {");
    EXPECT_EQ(editor.currentBuffer->lines[1], "    ");
    EXPECT_EQ(*editor.cursorY, 1);
    EXPECT_EQ(*editor.cursorX, 4);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, NormalOpenBelowAfterCppBraceUsesIndentWidth)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"fn main() {"};
    set_buffer_filename(editor, "main.cpp");
    editor.currentBuffer->clangIndentWidthValid = true;
    editor.currentBuffer->clangIndentWidth = 4;
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('o');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 2u);
    EXPECT_EQ(editor.currentBuffer->lines[1], "    ");
    EXPECT_EQ(*editor.cursorY, 1);
    EXPECT_EQ(*editor.cursorX, 4);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, NormalOpenAboveClosingCppBraceUsesIndentWidth)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"fn main() {", "}"};
    set_buffer_filename(editor, "main.cpp");
    editor.currentBuffer->clangIndentWidthValid = true;
    editor.currentBuffer->clangIndentWidth = 4;
    *editor.cursorX = 0;
    *editor.cursorY = 1;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('O');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[1], "    ");
    EXPECT_EQ(editor.currentBuffer->lines[2], "}");
    EXPECT_EQ(*editor.cursorY, 1);
    EXPECT_EQ(*editor.cursorX, 4);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, AutoBraceReturnInitializerStaysInlineInCpp)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"return "};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorX = (int)editor.currentBuffer->lines[0].size();
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, InsertMode{});
    sm.dispatch('{');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "return {}");
    EXPECT_EQ(*editor.cursorX, 8);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, AutoBraceReturnInitializerStaysInlineInMla)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"return "};
    set_buffer_filename(editor, "main.mla");
    *editor.cursorX = (int)editor.currentBuffer->lines[0].size();
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, InsertMode{});
    sm.dispatch('{');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "return {}");
    EXPECT_EQ(*editor.cursorX, 8);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, AutoBraceFunctionArgumentStaysInline)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"call()"};
    set_buffer_filename(editor, "main.mla");
    *editor.cursorX = 5;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, InsertMode{});
    sm.dispatch('{');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "call({})");
    EXPECT_EQ(*editor.cursorX, 6);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}
