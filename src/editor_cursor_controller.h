#pragma once

#include "editor.h"

class EditorCursorController
{
public:
    explicit EditorCursorController(Editor& editor);

    void moveLeft(int count = 1);
    void moveRight(int count = 1);
    void moveUp(int count = 1);
    void moveDown(int count = 1);
    void moveWordForward();
    void moveWordBackward();
    void moveToEndOfWord();
    void moveToLineStart();
    void moveToLineEnd();
    void moveToFirstLine();
    void moveToLastLine();
    void moveToLine(int line);
    void pushJumpLocation();
    void jumpForward();
    void jumpBack();
    void restoreJumpLocation(const JumpLocation& loc);
    void scrollHalfPageDown(bool visual);
    void scrollHalfPageUp(bool visual);
    void moveToMatchingBracket();
    void findCharForward(char c);
    void findCharBackward(char c);
    void moveToFirstNonBlank();
    void moveParagraphForward();
    void moveParagraphBackward();
    void moveWordForwardBig();
    void moveWordBackwardBig();
    void moveToEndOfWordBig();
    void findCharForwardBefore(char c);
    void findCharBackwardAfter(char c);
    void scrollToTop();
    void scrollToBottom();
    void scrollPageUp();
    void scrollPageDown();
    void moveToScreenTop();
    void moveToScreenMiddle();
    void moveToScreenBottom();
    void adjustViewport();
    void adjustViewportForPane(Editor::PaneState& pane, int rows, int cols);
    void centerScreen();

private:
    Editor& editor;
};
