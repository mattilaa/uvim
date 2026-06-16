#include "real_mode_test_utils.h"

using namespace uvim_test;

TEST(RealModeTransitionsTest, UndoToOldestChangeKeepsFirstEditedLine)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"one", "two", "three", "four"};
    set_buffer_filename(editor, "/tmp/uvim_undo_oldest_cursor_test.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 0;
    editor.saveState();
    editor.currentBuffer->savedUndoIndex = editor.currentBuffer->undoIndex;

    *editor.cursorY = 2;
    *editor.cursorX = 1;
    editor.currentBuffer->lines[2] = "changed";
    editor.saveState();

    editor.undo();
    EXPECT_EQ(editor.currentBuffer->lines,
              (std::vector<std::string>{"one", "two", "three", "four"}));
    EXPECT_EQ(*editor.cursorY, 2);
    EXPECT_EQ(*editor.cursorX, 1);
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

TEST(RealModeTransitionsTest, UndoBackToSavedClearsDirtyWithHash)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"one"};
    *editor.cursorX = 0;
    *editor.cursorY = 0;
    editor.saveState();
    editor.currentBuffer->savedUndoIndex = -1;
    auto hash_lines = [](const std::vector<std::string>& src) -> size_t
    {
        size_t h = 1469598103934665603ull;
        for(const auto& line : src)
        {
            for(unsigned char c : line)
            {
                h ^= c;
                h *= 1099511628211ull;
            }
            h ^= '\n';
            h *= 1099511628211ull;
        }
        return h;
    };
    editor.currentBuffer->savedContentHashValid = true;
    editor.currentBuffer->savedContentHash =
        hash_lines(editor.currentBuffer->lines);
    *editor.dirty = false;

    editor.currentBuffer->lines[0] = "two";
    *editor.dirty = true;
    editor.saveState();

    editor.undo();

    EXPECT_FALSE(*editor.dirty);
    EXPECT_EQ(editor.currentBuffer->lines[0], "one");
}
