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
    EXPECT_EQ(
        editor.currentBuffer->lines[0],
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

TEST(RealModeTransitionsTest, EnterBetweenBracesAlignsClosingBraceWithBlock)
{
    for(const char* filename :
        {"main.cpp", "main.mla", "main.js", "untitled.txt"})
    {
        for(const std::string indent : {"", "    ", "\t"})
        {
            SCOPED_TRACE(std::string(filename) + " indent=" + indent);
            Editor editor = Editor::createForTests();
            editor.createNewBuffer();
            set_buffer_filename(editor, filename);
            editor.currentBuffer->lines = {
                indent + "if (something) {  }; // keep"};
            *editor.cursorX = static_cast<int>(indent.size()) + 16;
            *editor.cursorY = 0;

            auto sm = makeMachine(editor, InsertMode{});
            editor.currentBuffer->clangIndentWidthValid = true;
            editor.currentBuffer->clangIndentWidth = 2;
            sm.dispatch(keyCode(control::ControlKey::ENTER));

            EXPECT_EQ(
                editor.currentBuffer->lines,
                (std::vector<std::string>{
                    indent + "if (something) {",
                    indent + "  ",
                    indent + "}; // keep"}));
            EXPECT_EQ(*editor.cursorY, 1);
            EXPECT_EQ(*editor.cursorX, static_cast<int>(indent.size()) + 2);
            EXPECT_STREQ(sm.currentStateName(), "INSERT");
        }
    }
}

TEST(RealModeTransitionsTest, EnterExpandsTypedInlineBracePair)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    set_buffer_filename(editor, "main.cpp");
    editor.currentBuffer->lines = {"    return "};
    *editor.cursorX = static_cast<int>(editor.currentBuffer->lines[0].size());
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, InsertMode{});
    sm.dispatch('{');
    ASSERT_EQ(editor.currentBuffer->lines[0], "    return {}");
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_EQ(
        editor.currentBuffer->lines,
        (std::vector<std::string>{"    return {", "        ", "    }"}));
    EXPECT_EQ(*editor.cursorY, 1);
    EXPECT_EQ(*editor.cursorX, 8);
}

TEST(RealModeTransitionsTest, SplitBracesSettingRestoresPreviousEnterBehavior)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    set_buffer_filename(editor, "main.cpp");
    EXPECT_TRUE(editor.splitBraces);
    ASSERT_TRUE(editor.handleSetCommand("set splitbraces=false"));
    EXPECT_FALSE(editor.splitBraces);
    ASSERT_TRUE(editor.handleSetCommand("set splitbraces=invalid"));
    EXPECT_FALSE(editor.splitBraces);

    editor.currentBuffer->lines = {"    if (something) {}"};
    *editor.cursorX = 20;
    *editor.cursorY = 0;
    auto sm = makeMachine(editor, InsertMode{});
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_EQ(
        editor.currentBuffer->lines,
        (std::vector<std::string>{"    if (something) {", "        }"}));
    EXPECT_EQ(*editor.cursorX, 8);

    ASSERT_TRUE(editor.handleSetCommand("set splitbraces=true"));
    EXPECT_TRUE(editor.splitBraces);
    ASSERT_TRUE(editor.handleSetCommand("set nosplitbraces"));
    EXPECT_FALSE(editor.splitBraces);
    ASSERT_TRUE(editor.handleSetCommand("set splitbraces"));
    EXPECT_TRUE(editor.splitBraces);
    ASSERT_TRUE(editor.handleSetCommand("set splitbraces?"));
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

TEST(RealModeTransitionsTest, SquareBracketPairTypingSkippingAndBackspace)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, InsertMode{});
    sm.dispatch('[');
    ASSERT_EQ(editor.currentBuffer->lines[0], "[]");
    EXPECT_EQ(*editor.cursorX, 1);
    sm.dispatch(']');
    EXPECT_EQ(editor.currentBuffer->lines[0], "[]");
    EXPECT_EQ(*editor.cursorX, 2);

    *editor.cursorX = 1;
    sm.dispatch(keyCode(control::ControlKey::BACKSPACE));
    EXPECT_EQ(editor.currentBuffer->lines[0], "");
    EXPECT_EQ(*editor.cursorX, 0);

    sm.dispatch('[');
    sm.dispatch('[');
    EXPECT_EQ(editor.currentBuffer->lines[0], "[[]]");
    sm.dispatch(keyCode(control::ControlKey::BACKSPACE));
    EXPECT_EQ(editor.currentBuffer->lines[0], "[]");
    sm.dispatch('x');
    sm.dispatch(keyCode(control::ControlKey::BACKSPACE));
    EXPECT_EQ(editor.currentBuffer->lines[0], "[]");
}

TEST(RealModeTransitionsTest, EnterBetweenSquareBracketsAlignsClosingBracket)
{
    for(const char* filename :
        {"main.cpp", "main.js", "data.json", "untitled.txt"})
    {
        for(const std::string indent : {"", "    ", "\t"})
        {
            SCOPED_TRACE(std::string(filename) + " indent=" + indent);
            Editor editor = Editor::createForTests();
            editor.createNewBuffer();
            set_buffer_filename(editor, filename);
            editor.currentBuffer
                ->lines = {indent + "items = ; // keep", "next"};
            *editor.cursorX = static_cast<int>(indent.size()) + 8;
            *editor.cursorY = 0;
            auto sm = makeMachine(editor, InsertMode{});
            editor.currentBuffer->clangIndentWidthValid = true;
            editor.currentBuffer->clangIndentWidth = 2;
            sm.dispatch('[');
            ASSERT_EQ(
                editor.currentBuffer->lines[0],
                indent + "items = []; // keep");
            sm.dispatch(keyCode(control::ControlKey::ENTER));
            EXPECT_EQ(
                editor.currentBuffer->lines,
                (std::vector<std::string>{
                    indent + "items = [",
                    indent + "  ",
                    indent + "]; // keep",
                    "next"}));
            EXPECT_EQ(*editor.cursorY, 1);
            EXPECT_EQ(*editor.cursorX, static_cast<int>(indent.size()) + 2);
        }
    }
}

TEST(RealModeTransitionsTest, SquareBracketSettingsControlPairingAndEnter)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, InsertMode{});
    EXPECT_TRUE(editor.autoBrackets);
    EXPECT_TRUE(editor.splitBrackets);
    ASSERT_TRUE(editor.handleSetCommand("set autobrackets=false"));
    sm.dispatch('[');
    EXPECT_EQ(editor.currentBuffer->lines[0], "[");
    editor.currentBuffer->lines = {"[]"};
    *editor.cursorX = 1;
    sm.dispatch(']');
    EXPECT_EQ(editor.currentBuffer->lines[0], "[]]");
    editor.currentBuffer->lines = {"[]"};
    *editor.cursorX = 1;
    sm.dispatch(keyCode(control::ControlKey::BACKSPACE));
    EXPECT_EQ(editor.currentBuffer->lines[0], "]");

    ASSERT_TRUE(editor.handleSetCommand("set autobrackets"));
    EXPECT_TRUE(editor.autoBrackets);
    ASSERT_TRUE(editor.handleSetCommand("set noautobrackets"));
    EXPECT_FALSE(editor.autoBrackets);
    ASSERT_TRUE(editor.handleSetCommand("set autobrackets=true"));
    ASSERT_TRUE(editor.handleSetCommand("set autobrackets?"));
    ASSERT_TRUE(editor.handleSetCommand("set autobrackets=invalid"));
    EXPECT_TRUE(editor.autoBrackets);

    ASSERT_TRUE(editor.handleSetCommand("set splitbrackets=false"));
    editor.currentBuffer->lines = {"    items = []"};
    *editor.cursorX = 13;
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_EQ(
        editor.currentBuffer->lines,
        (std::vector<std::string>{"    items = [", "    ]"}));
    EXPECT_EQ(*editor.cursorX, 4);
    ASSERT_TRUE(editor.handleSetCommand("set splitbrackets=true"));
    EXPECT_TRUE(editor.splitBrackets);
    ASSERT_TRUE(editor.handleSetCommand("set nosplitbrackets"));
    EXPECT_FALSE(editor.splitBrackets);
    ASSERT_TRUE(editor.handleSetCommand("set splitbrackets"));
    ASSERT_TRUE(editor.handleSetCommand("set splitbrackets?"));
    ASSERT_TRUE(editor.handleSetCommand("set splitbrackets=invalid"));
    EXPECT_TRUE(editor.splitBrackets);
    EXPECT_TRUE(editor.splitBraces);
}

TEST(RealModeTransitionsTest, SquareBracketsRespectStringAndMasterPairSettings)
{
    for(const bool inStrings : {true, false})
    {
        Editor editor = Editor::createForTests();
        editor.createNewBuffer();
        editor.currentBuffer->lines = {"\"\""};
        *editor.cursorX = 1;
        auto sm = makeMachine(editor, InsertMode{});
        editor.autoBracesInStrings = inStrings;
        sm.dispatch('[');
        EXPECT_EQ(
            editor.currentBuffer->lines[0],
            inStrings ? "\"[]\"" : "\"[\"");
    }
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.autoBraces = false;
    auto sm = makeMachine(editor, InsertMode{});
    sm.dispatch('[');
    EXPECT_EQ(editor.currentBuffer->lines[0], "[");
}

TEST(RealModeTransitionsTest, EnterIndentsCollectionItemsAfterComma)
{
    for(const std::string prefix :
        {"    auto v = ['a', ", "    auto v = {'a': 1, ", "    call('a', "})
    {
        Editor editor = Editor::createForTests();
        editor.createNewBuffer();
        editor.currentBuffer->lines = {prefix + "'b'];"};
        *editor.cursorX = static_cast<int>(prefix.size());
        auto sm = makeMachine(editor, InsertMode{});
        sm.dispatch(keyCode(control::ControlKey::ENTER));
        EXPECT_EQ(
            editor.currentBuffer->lines,
            (std::vector<std::string>{prefix, "        'b'];"}));
        EXPECT_EQ(*editor.cursorX, 8);
    }
}

TEST(RealModeTransitionsTest, EnterContinuesListIndentWithoutAccumulatingIt)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    v = ['a',", "        'b', 'c'];"};
    *editor.cursorY = 1;
    *editor.cursorX = 13;
    auto sm = makeMachine(editor, InsertMode{});
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_EQ(
        editor.currentBuffer->lines,
        (std::vector<
            std::string>{"    v = ['a',", "        'b', ", "        'c'];"}));
    EXPECT_EQ(*editor.cursorX, 8);
}

TEST(
    RealModeTransitionsTest,
    ContinuationIndentIgnoresQuotedAndCommentDelimiters)
{
    for(const std::string line :
        {"    text = '[', next",
         "    text = \"{\", next",
         "    // [, next",
         "    /* [ */ next"})
    {
        Editor editor = Editor::createForTests();
        editor.createNewBuffer();
        editor.currentBuffer->lines = {line};
        *editor.cursorX = static_cast<int>(line.size());
        auto sm = makeMachine(editor, InsertMode{});
        sm.dispatch(keyCode(control::ControlKey::ENTER));
        EXPECT_EQ(editor.currentBuffer->lines[1], "    ");
    }
}

TEST(
    RealModeTransitionsTest,
    CollectionIndentSettingsControlContinuationAndWidth)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, InsertMode{});
    ASSERT_TRUE(editor.handleSetCommand("set continuationindent=false"));
    editor.currentBuffer->lines = {"    v = ['a', 'b'];"};
    *editor.cursorX = 14;
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_EQ(editor.currentBuffer->lines[1], "    'b'];");
    ASSERT_TRUE(editor.handleSetCommand("set continuationindent"));
    ASSERT_TRUE(editor.handleSetCommand("set collectionindentwidth=2"));
    editor.currentBuffer->lines = {"    v = ['a', 'b'];"};
    *editor.cursorY = 0;
    *editor.cursorX = 14;
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_EQ(editor.currentBuffer->lines[1], "      'b'];");
    ASSERT_TRUE(editor.handleSetCommand("set collectionindentwidth=99"));
    EXPECT_EQ(editor.collectionIndentSpaces, 2);
    ASSERT_TRUE(editor.handleSetCommand("set collectionindentwidth=2oops"));
    EXPECT_EQ(editor.collectionIndentSpaces, 2);
    ASSERT_TRUE(editor.handleSetCommand("set collectionindentwidth?"));
    ASSERT_TRUE(editor.handleSetCommand("set continuationindent?"));
}

TEST(
    RealModeTransitionsTest,
    CollectionShortcutReindentsNestedMapAndListAndUndoes)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    const std::vector<std::string> before =
        {"    v = {", "'a': [", "'x',", "'y'],", "'b': 2,", "};", "untouched"};
    editor.currentBuffer->lines = before;
    editor.saveState();
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_T));
    EXPECT_EQ(
        editor.currentBuffer->lines,
        (std::vector<std::string>{
            "    v = {",
            "        'a': [",
            "            'x',",
            "            'y'],",
            "        'b': 2,",
            "    };",
            "untouched"}));
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    EXPECT_TRUE(editor.currentBuffer->lspSyncNeeded);
    editor.undo();
    EXPECT_EQ(editor.currentBuffer->lines, before);
}

TEST(
    RealModeTransitionsTest,
    CollectionShortcutWorksInInsertModeAndCanBeRebound)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"v = [", "'a',", "'b'];"};
    *editor.cursorY = 1;
    *editor.cursorX = 2;
    auto sm = makeMachine(editor, InsertMode{});
    ASSERT_TRUE(editor.handleSetCommand("set collectionindentkey=ctrl-y"));
    sm.dispatch(keyCode(control::ControlKey::CTRL_T));
    EXPECT_EQ(editor.currentBuffer->lines[1], "'a',");
    sm.dispatch(keyCode(control::ControlKey::CTRL_Y));
    EXPECT_EQ(editor.currentBuffer->lines[1], "    'a',");
    EXPECT_EQ(editor.currentBuffer->lines[2], "    'b'];");
    EXPECT_EQ(*editor.cursorX, 6);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
    ASSERT_TRUE(editor.handleSetCommand("set collectionindentkey=off"));
    EXPECT_EQ(editor.collectionIndentKey, 0);
    ASSERT_TRUE(editor.handleSetCommand("set collectionindentkey=invalid"));
    EXPECT_EQ(editor.collectionIndentKey, 0);
    ASSERT_TRUE(editor.handleSetCommand("set collectionindentkey?"));
}

TEST(
    RealModeTransitionsTest,
    CollectionShortcutChoosesInnermostListAndPreservesComments)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines =
        {"outer = {",
         "    'a': [",
         "']', // }",
         "/* [",
         " do not indent ]",
         "*/",
         "'x'",
         "]",
         "}"};
    *editor.cursorY = 2;
    *editor.cursorX = 0;
    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_T));
    EXPECT_EQ(
        editor.currentBuffer->lines,
        (std::vector<std::string>{
            "outer = {",
            "    'a': [",
            "        ']', // }",
            "        /* [",
            " do not indent ]",
            "*/",
            "        'x'",
            "    ]",
            "}"}));
}

TEST(
    RealModeTransitionsTest,
    CollectionShortcutLeavesUnmatchedAndSingleLineListsAlone)
{
    for(const std::vector<std::string> before :
        {std::vector<std::string>{"v = ['a', 'b'];"},
         std::vector<std::string>{"v = [", "'a'"}})
    {
        Editor editor = Editor::createForTests();
        editor.createNewBuffer();
        editor.currentBuffer->lines = before;
        auto sm = makeMachine(editor, NormalMode{});
        sm.dispatch(keyCode(control::ControlKey::CTRL_T));
        EXPECT_EQ(editor.currentBuffer->lines, before);
    }
}
