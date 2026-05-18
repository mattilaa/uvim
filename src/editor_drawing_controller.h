#pragma once

#include "mode.h"

class Editor;

class EditorDrawingController
{
public:
    explicit EditorDrawingController(Editor& editor);

    void draw();
    void drawBufferView();
    void refreshScreen();
    void updateCursorPosition(bool flushNow = true);
    void forceFullRedraw();

private:
    Editor& editor;

    int lastBufferOffsetY = -1;
    int lastBufferOffsetX = -1;
    int lastBufferCursorY = -1;
    int lastBufferVisualStartY = -1;
    int lastBufferVisualEndY = -1;
    Mode lastBufferMode = NORMAL;
    int lastFrameOffsetY = -1;
    int lastFrameOffsetX = -1;
    int lastFrameCursorY = -1;
    int lastFrameVisualStartY = -1;
    int lastFrameVisualEndY = -1;
    Mode lastFrameMode = NORMAL;
    bool lastFrameCommandPopupActive = false;
    bool lastFrameCommandHistoryPopupActive = false;
};
