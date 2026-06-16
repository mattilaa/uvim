#include "real_mode_test_utils.h"

using namespace uvim_test;

TEST(RealModeTransitionsTest, VisualInnerParenSelectsTextObject)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"foo(one, two);"};
    *editor.cursorX = 5;
    auto sm = makeMachine(editor, VisualMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(command::CommandKey::KEY_RIGHT_PAREN));

    EXPECT_STREQ(sm.currentStateName(), "VISUAL");
    EXPECT_EQ(editor.currentBuffer->visualStartX, 4);
    EXPECT_EQ(editor.currentBuffer->visualEndX, 11);
    EXPECT_EQ(*editor.cursorX, 11);
}

TEST(RealModeTransitionsTest, VisualInnerQuoteSelectsTextObject)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"auto s = \"hello\";"};
    *editor.cursorX = 11;
    auto sm = makeMachine(editor, VisualMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(command::CommandKey::KEY_DOUBLE_QUOTE));

    EXPECT_STREQ(sm.currentStateName(), "VISUAL");
    EXPECT_EQ(editor.currentBuffer->visualStartX, 10);
    EXPECT_EQ(editor.currentBuffer->visualEndX, 14);
    EXPECT_EQ(*editor.cursorX, 14);
}

TEST(RealModeTransitionsTest, VisualInnerSingleQuoteSelectsTextObject)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"auto c = 'x';"};
    *editor.cursorX = 10;
    auto sm = makeMachine(editor, VisualMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(command::CommandKey::KEY_APOSTROPHE));

    EXPECT_STREQ(sm.currentStateName(), "VISUAL");
    EXPECT_EQ(editor.currentBuffer->visualStartX, 10);
    EXPECT_EQ(editor.currentBuffer->visualEndX, 10);
    EXPECT_EQ(*editor.cursorX, 10);
}

TEST(RealModeTransitionsTest, VisualInnerLeftBracketSelectsTextObject)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"items[index + 1];"};
    *editor.cursorX = 8;
    auto sm = makeMachine(editor, VisualMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(command::CommandKey::KEY_LEFT_BRACKET));

    EXPECT_STREQ(sm.currentStateName(), "VISUAL");
    EXPECT_EQ(editor.currentBuffer->visualStartX, 6);
    EXPECT_EQ(editor.currentBuffer->visualEndX, 14);
    EXPECT_EQ(*editor.cursorX, 14);
}

TEST(RealModeTransitionsTest, VisualInnerRightBracketSelectsTextObject)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"items[index + 1];"};
    *editor.cursorX = 8;
    auto sm = makeMachine(editor, VisualMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(command::CommandKey::KEY_RIGHT_BRACKET));

    EXPECT_STREQ(sm.currentStateName(), "VISUAL");
    EXPECT_EQ(editor.currentBuffer->visualStartX, 6);
    EXPECT_EQ(editor.currentBuffer->visualEndX, 14);
    EXPECT_EQ(*editor.cursorX, 14);
}

TEST(RealModeTransitionsTest, VisualInnerLeftBraceSelectsMultilineTextObject)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {
        "void f() {",
        "    int value = 1;",
        "    value++;",
        "}",
    };
    *editor.cursorY = 1;
    *editor.cursorX = 8;
    auto sm = makeMachine(editor, VisualMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(command::CommandKey::KEY_LEFT_BRACE));

    EXPECT_STREQ(sm.currentStateName(), "VISUAL");
    EXPECT_EQ(editor.currentBuffer->visualStartY, 1);
    EXPECT_EQ(editor.currentBuffer->visualStartX, 0);
    EXPECT_EQ(editor.currentBuffer->visualEndY, 2);
    EXPECT_EQ(editor.currentBuffer->visualEndX, 11);
    EXPECT_EQ(*editor.cursorY, 2);
    EXPECT_EQ(*editor.cursorX, 11);
}

TEST(RealModeTransitionsTest, VisualInnerRightBraceSelectsMultilineTextObject)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {
        "void f() {",
        "    int value = 1;",
        "    value++;",
        "}",
    };
    *editor.cursorY = 1;
    *editor.cursorX = 8;
    auto sm = makeMachine(editor, VisualMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(command::CommandKey::KEY_RIGHT_BRACE));

    EXPECT_STREQ(sm.currentStateName(), "VISUAL");
    EXPECT_EQ(editor.currentBuffer->visualStartY, 1);
    EXPECT_EQ(editor.currentBuffer->visualStartX, 0);
    EXPECT_EQ(editor.currentBuffer->visualEndY, 2);
    EXPECT_EQ(editor.currentBuffer->visualEndX, 11);
    EXPECT_EQ(*editor.cursorY, 2);
    EXPECT_EQ(*editor.cursorX, 11);
}

TEST(RealModeTransitionsTest, NormalVisualInnerParenSelectsTextObject)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"foo(one, two);"};
    *editor.cursorX = 5;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_V));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(command::CommandKey::KEY_RIGHT_PAREN));

    EXPECT_STREQ(sm.currentStateName(), "VISUAL");
    EXPECT_EQ(editor.currentBuffer->visualStartX, 4);
    EXPECT_EQ(editor.currentBuffer->visualEndX, 11);
    EXPECT_EQ(*editor.cursorX, 11);
}

TEST(RealModeTransitionsTest, NormalVisualInnerParenEscCancelsPending)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"foo(one, two);"};
    *editor.cursorX = 5;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_V));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(control::ControlKey::ESC));

    EXPECT_STREQ(sm.currentStateName(), "VISUAL");
    EXPECT_TRUE(editor.commandBuffer.empty());
    EXPECT_EQ(editor.currentBuffer->visualStartX, 5);
    EXPECT_EQ(editor.currentBuffer->visualEndX, 5);
    EXPECT_EQ(*editor.cursorX, 5);
}
