#pragma once

#include <string>

class Editor;

class EditorIndentController
{
public:
    explicit EditorIndentController(Editor& editor);

    std::string toLowerCase(const std::string& str);
    int getLineIndent(int line);
    void indentLine(int line, int spaces);
    void autoIndentLine(int line);
    void autoIndentRange(int startLine, int endLine);
    void updateClangFormatIndentWidth();
    int indentWidthForBraces() const;
    bool braceNewLineForAutoBraces() const;
    void commentLines(int startY, int endY);
    void commentBlock(int startY, int endY);
    void commentBlockRange(int startY, int startX, int endY, int endX);
    bool insertTodoLineComment(int row);
    bool insertTodoBlockComment(int row);

private:
    Editor& editor;
};
