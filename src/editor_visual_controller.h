#pragma once

class Editor;

class EditorVisualController
{
public:
    explicit EditorVisualController(Editor& editor);

    void startVisualMode();
    void startVisualLineMode();
    void startVisualBlockMode();
    void updateVisualSelection();
    void updateVisualBlockSelection();
    bool isInSelection(int row, int col);
    bool isInVisualBlock(int row, int col);
    void getVisualBlockBounds(int& startY, int& startX, int& endY, int& endX);
    void getSelectionBounds(int& startY, int& startX, int& endY, int& endX);
    void setVisualRange();
    void swapVisualEnds();
    void swapVisualBlockCorner();
    void prepareBlockInsert(bool atEnd);
    void indentSelection();
    void dedentSelection();
    void autoIndentSelection();
    void lowercaseSelection();
    void uppercaseSelection();
    void toggleCaseSelection();
    void yankLineSelection();
    void deleteLineSelection();
    void indentLineSelection();
    void dedentLineSelection();
    void autoIndentLineSelection();

private:
    Editor& editor;
};
