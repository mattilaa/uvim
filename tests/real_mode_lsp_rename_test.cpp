#include "real_mode_test_utils.h"

using namespace uvim_test;

TEST(RealModeTransitionsTest, NormalRnRequestsClangRename)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int value = 1;"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorX = 4;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    Terminal::unreadKey('n');
    sm.dispatch('r');

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    EXPECT_FALSE(editor.renamePopupActive);
    EXPECT_EQ(editor.statusMessage, "rn: clangd rename unavailable");
}

TEST(RealModeTransitionsTest, VisualRnRequestsClangRenameAndReturnsNormal)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int value = 1;"};
    set_buffer_filename(editor, "main.cpp");
    *editor.cursorX = 4;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, VisualMode{});
    Terminal::unreadKey('n');
    sm.dispatch('r');

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    EXPECT_FALSE(editor.renamePopupActive);
    EXPECT_EQ(editor.statusMessage, "rn: clangd rename unavailable");
}

TEST(RealModeTransitionsTest, UndoRestoresAllFilesTouchedByRename)
{
    auto root = make_temp_dir("uvim_rename_undo_");
    auto current = root / "main.cpp";
    auto other = root / "other.cpp";
    write_file(current, "int value = 1;\n");
    write_file(other, "int value = 2;\n");

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int value = 1;"};
    set_buffer_filename(editor, current.string());
    editor.currentBuffer->dirty = false;

    Editor::RenameUndoFileSnapshot currentSnapshot;
    currentSnapshot.path = current.string();
    currentSnapshot.lines = {"int value = 1;"};
    currentSnapshot.hadOpenBuffer = true;
    currentSnapshot.fileExisted = true;
    currentSnapshot.dirty = false;

    Editor::RenameUndoFileSnapshot otherSnapshot;
    otherSnapshot.path = other.string();
    otherSnapshot.lines = {"int value = 2;"};
    otherSnapshot.fileExisted = true;

    editor.renameUndoAvailable = true;
    editor.renameUndoFiles = {currentSnapshot, otherSnapshot};

    editor.currentBuffer->lines = {"int renamed = 1;"};
    editor.currentBuffer->dirty = true;
    write_file(other, "int renamed = 2;\n");

    editor.undo();

    EXPECT_EQ(editor.currentBuffer->lines,
              std::vector<std::string>({"int value = 1;"}));
    EXPECT_FALSE(editor.currentBuffer->dirty);
    EXPECT_EQ(read_file(other), "int value = 2;");
    EXPECT_FALSE(editor.renameUndoAvailable);
    EXPECT_EQ(editor.statusMessage, "rn: reverted rename in 2 file(s)");
}
