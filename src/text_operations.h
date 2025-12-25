#pragma once

#include "editor_context.h"
#include <string>

class TextOperations
{
public:
    explicit TextOperations(EditorContext& ctx);

    // Insert operations
    void insertChar(char c);
    void insertNewline();
    void insertTab();

    // Delete operations
    void deleteChar();        // Backspace
    void deleteCharForward(); // Delete/x
    void deleteLine();        // dd
    void deleteToLineEnd();   // D
    void deleteWord();        // dw

    // Yank operations
    void yankLine();
    void yankToLineEnd();
    void yankSelection();
    void yankRange(int startY, int startX, int endY, int endX);

    // Paste operations
    void pasteAfter();
    void pasteBefore();

    // System clipboard
    std::string getSystemClipboard();
    void setSystemClipboard(const std::string& text);
    void yankToSystemClipboard();
    void pasteFromSystemClipboard();

    // Range operations
    void deleteRange(int startY, int startX, int endY, int endX);
    void deleteSelection();

    // Indentation
    int getLineIndent(int line);
    void indentLine(int line, int spaces);
    void autoIndentLine(int line);
    void autoIndentRange(int startLine, int endLine);
    void shiftLineRight(int line, int spaces = 4);
    void shiftLineLeft(int line, int spaces = 4);

    // Join lines
    void joinLines(int count = 1);

    // Change case
    void toggleCase();
    void toUpperCase();
    void toLowerCase();

private:
    EditorContext& ctx;

    bool isCppFile() const;
};
