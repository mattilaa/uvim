#include "real_mode_test_utils.h"
#include "editor_mode_controller.h"

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

TEST(RealModeTransitionsTest, ChangeInnerQuoteAcceptsUppercaseTInInsertMode)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"auto s = \"return to something\";"};
    *editor.cursorX = 14;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(command::CommandKey::KEY_DOUBLE_QUOTE));

    ASSERT_STREQ(sm.currentStateName(), "INSERT");

    sm.dispatch(keyCode(typed::TypedKey::KEY_CAP_T));
    sm.dispatch(keyCode(control::ControlKey::BACKSPACE));

    EXPECT_STREQ(sm.currentStateName(), "INSERT");
    EXPECT_EQ(editor.currentBuffer->lines[0], "auto s = \"\";");
    EXPECT_EQ(*editor.cursorX, 10);
}

TEST(RealModeTransitionsTest, EditorHandleKeypressChangeInnerQuoteAcceptsUppercaseT)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"auto s = \"return to something\";"};
    *editor.cursorX = 14;
    EditorModeController controller(editor);

    controller.handleKeypress(keyCode(typed::TypedKey::KEY_C));
    controller.handleKeypress(keyCode(typed::TypedKey::KEY_I));
    controller.handleKeypress(keyCode(command::CommandKey::KEY_DOUBLE_QUOTE));

    ASSERT_EQ(editor.currentMode, INSERT);

    controller.handleKeypress(keyCode(typed::TypedKey::KEY_CAP_T));
    controller.handleKeypress(127);

    EXPECT_EQ(editor.currentMode, INSERT);
    EXPECT_EQ(editor.currentBuffer->lines[0], "auto s = \"\";");
    EXPECT_EQ(*editor.cursorX, 10);
}

TEST(RealModeTransitionsTest,
     ChangeInnerQuoteCancelsCompletionBeforeUppercaseTAndBackspace)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"println!(\"return to something\");"};
    *editor.cursorX = 12;
    editor.completionActive = true;
    editor.completionFromLsp = true;
    editor.completionAnchorX = 12;
    editor.completionAnchorY = 0;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(command::CommandKey::KEY_DOUBLE_QUOTE));

    ASSERT_STREQ(sm.currentStateName(), "INSERT");
    EXPECT_FALSE(editor.completionActive);

    sm.dispatch(keyCode(typed::TypedKey::KEY_CAP_T));
    sm.dispatch(keyCode(control::ControlKey::BACKSPACE));

    EXPECT_STREQ(sm.currentStateName(), "INSERT");
    EXPECT_FALSE(editor.completionActive);
    EXPECT_EQ(editor.currentBuffer->lines[0], "println!(\"\");");
    EXPECT_EQ(*editor.cursorX, 10);
}

TEST(RealModeTransitionsTest, OperatorPendingCapitalTWaitsWithoutBlocking)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"abc def"};
    *editor.cursorX = 6;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_CAP_T));

    ASSERT_STREQ(sm.currentStateName(), "OP_PENDING");

    sm.dispatch(keyCode(typed::TypedKey::KEY_D));
    ASSERT_STREQ(sm.currentStateName(), "INSERT");

    sm.dispatch(keyCode(typed::TypedKey::KEY_CAP_X));

    EXPECT_EQ(editor.currentBuffer->lines[0], "abc dX");
    EXPECT_EQ(*editor.cursorX, 6);
}
