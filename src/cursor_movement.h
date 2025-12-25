#pragma once

#include "editor_context.h"

class CursorMovement
{
public:
    explicit CursorMovement(EditorContext& ctx);

    // Basic movement
    void moveLeft(int count = 1);
    void moveRight(int count = 1);
    void moveUp(int count = 1);
    void moveDown(int count = 1);

    // Word movement
    void moveWordForward();
    void moveWordBackward();
    void moveToEndOfWord();

    // Line movement
    void moveToLineStart();
    void moveToLineEnd();
    void moveToFirstNonBlank();

    // File movement
    void moveToFirstLine();
    void moveToLastLine();
    void moveToLine(int line);

    // Scrolling
    void scrollHalfPageDown(bool visual = false);
    void scrollHalfPageUp(bool visual = false);
    void centerScreen();

    // Bracket matching
    void moveToMatchingBracket();

    // Find char (f/F/t/T)
    void findCharForward(char c);
    void findCharBackward(char c);
    void findCharTillForward(char c);
    void findCharTillBackward(char c);
    void repeatFindChar();
    void repeatFindCharReverse();

    // Jump list
    void jumpForward();
    void jumpBack();
    void pushJumpLocation();
    void restoreJumpLocation(const JumpLocation& loc);

    // Viewport adjustment
    void adjustViewport();

private:
    EditorContext& ctx;
};
