#include "real_mode_test_utils.h"

using namespace uvim_test;

TEST(RealModeTransitionsTest, LeaderCiTogglesCppLineCommentOnCurrentLine)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int value = 1;"};
    set_buffer_filename(editor, "main.cpp");
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "// int value = 1;");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, LeaderCiAppliesWithoutEnter)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int value = 1;"};
    set_buffer_filename(editor, "main.cpp");
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "// int value = 1;");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, LeaderCiUndoRestoresCursorRow)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int first = 1;", "int second = 2;",
                                   "int third = 3;"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorY = 2;
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));

    EXPECT_EQ(editor.currentBuffer->lines[2], "// int third = 3;");

    editor.undo();

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[2], "int third = 3;");
    EXPECT_EQ(*editor.cursorY, 2);
    EXPECT_EQ(*editor.cursorX, 4);
}

TEST(RealModeTransitionsTest, LeaderCiiWrapsCppCurrentLineWithBlockRows)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    int value = 1;"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "    /*");
    EXPECT_EQ(editor.currentBuffer->lines[1], "    int value = 1;");
    EXPECT_EQ(editor.currentBuffer->lines[2], "    */");
    EXPECT_EQ(*editor.cursorY, 1);

    editor.undo();

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "    int value = 1;");
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_EQ(*editor.cursorX, 4);
}

TEST(RealModeTransitionsTest, LeaderCiiAppliesWithoutEnter)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    int value = 1;"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "    /*");
    EXPECT_EQ(editor.currentBuffer->lines[1], "    int value = 1;");
    EXPECT_EQ(editor.currentBuffer->lines[2], "    */");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, LeaderCiiUndoRestoresCursorRow)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int first = 1;", "int second = 2;",
                                   "    int third = 3;"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorY = 2;
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 5u);

    editor.undo();

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[2], "    int third = 3;");
    EXPECT_EQ(*editor.cursorY, 2);
    EXPECT_EQ(*editor.cursorX, 4);
}

TEST(RealModeTransitionsTest, LeaderCitInsertsTodoLineComment)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    int value = 1;"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_T));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 2u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "    // TODO: ");
    EXPECT_EQ(editor.currentBuffer->lines[1], "    int value = 1;");
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_EQ(*editor.cursorX, 13);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, LeaderCiitInsertsTodoBlockComment)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    int value = 1;"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_T));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "    /** TODO: ");
    EXPECT_EQ(editor.currentBuffer->lines[1], "     */");
    EXPECT_EQ(editor.currentBuffer->lines[2], "    int value = 1;");
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_EQ(*editor.cursorX, 14);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, LeaderCiitTypedCharsEnterInsertMode)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    int value = 1;"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(' ');
    sm.dispatch('c');
    sm.dispatch('i');
    sm.dispatch('i');
    sm.dispatch('t');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "    /** TODO: ");
    EXPECT_EQ(editor.currentBuffer->lines[1], "     */");
    EXPECT_EQ(editor.currentBuffer->lines[2], "    int value = 1;");
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_EQ(*editor.cursorX, 14);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, LeaderCiiWaitsForTodoSuffix)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    int value = 1;"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "    /*");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");

    sm.dispatch(keyCode(typed::TypedKey::KEY_T));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "    /** TODO: ");
    EXPECT_EQ(editor.currentBuffer->lines[1], "     */");
    EXPECT_EQ(editor.currentBuffer->lines[2], "    int value = 1;");
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_EQ(*editor.cursorX, 14);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, LeaderCiitUndoRestoresCursorRow)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int first = 1;", "int second = 2;",
                                   "    int third = 3;"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorY = 2;
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_T));

    EXPECT_STREQ(sm.currentStateName(), "INSERT");

    editor.undo();

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[2], "    int third = 3;");
    EXPECT_EQ(*editor.cursorY, 2);
    EXPECT_EQ(*editor.cursorX, 4);
}

TEST(RealModeTransitionsTest, LeaderCommentPendingEscReturnsToNormalAfterCi)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int value = 1;"};
    set_buffer_filename(editor, "main.cpp");
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(control::ControlKey::ESC));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "// int value = 1;");
    EXPECT_TRUE(editor.commandBuffer.empty());
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, VisualLineLeaderCommentPendingEscStaysVisualLine)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int one = 1;", "int two = 2;"};
    set_buffer_filename(editor, "main.cpp");
    auto sm = makeMachine(editor, VisualLineMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_J));
    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(control::ControlKey::ESC));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 2u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "int one = 1;");
    EXPECT_EQ(editor.currentBuffer->lines[1], "int two = 2;");
    EXPECT_STREQ(sm.currentStateName(), "VISUAL LINE");
}

TEST(RealModeTransitionsTest, VisualLineLeaderCitInsertsTodoLineComment)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    int one = 1;", "    int two = 2;"};
    set_buffer_filename(editor, "main.cpp");
    auto sm = makeMachine(editor, VisualLineMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_J));
    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_T));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 3u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "    // TODO: ");
    EXPECT_EQ(editor.currentBuffer->lines[1], "    int one = 1;");
    EXPECT_EQ(editor.currentBuffer->lines[2], "    int two = 2;");
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_EQ(*editor.cursorX, 13);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, VisualLineLeaderCiiWrapsCppRangeWithBlockRows)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    int one = 1;", "    int two = 2;"};
    set_buffer_filename(editor, "main.cpp");
    auto sm = makeMachine(editor, VisualLineMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_J));
    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 4u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "    /*");
    EXPECT_EQ(editor.currentBuffer->lines[1], "    int one = 1;");
    EXPECT_EQ(editor.currentBuffer->lines[2], "    int two = 2;");
    EXPECT_EQ(editor.currentBuffer->lines[3], "    */");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, VisualLineLeaderCiitInsertsTodoBlockComment)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    int one = 1;", "    int two = 2;"};
    set_buffer_filename(editor, "main.cpp");
    auto sm = makeMachine(editor, VisualLineMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_J));
    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_T));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 4u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "    /** TODO: ");
    EXPECT_EQ(editor.currentBuffer->lines[1], "    int one = 1;");
    EXPECT_EQ(editor.currentBuffer->lines[2], "    int two = 2;");
    EXPECT_EQ(editor.currentBuffer->lines[3], "     */");
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_EQ(*editor.cursorX, 14);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");
}

TEST(RealModeTransitionsTest, VisualLeaderCiiWrapsOnlySelectedText)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"foo(one, two);"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, VisualMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_L));
    sm.dispatch(keyCode(typed::TypedKey::KEY_L));
    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(typed::TypedKey::KEY_I));
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "foo(/* one */, two);");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}
