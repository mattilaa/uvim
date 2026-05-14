#pragma once

class Editor;

class EditorOperatorController
{
public:
    explicit EditorOperatorController(Editor& editor);

    void enterOperatorPending(char op);
    bool getTextObjectRange(char objChar, bool around, int& outStartY,
                            int& outStartX, int& outEndY, int& outEndX);
    void applyOperatorToRange(char op, int startY, int startX, int endY,
                              int endX);

private:
    Editor& editor;
};
