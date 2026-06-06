#pragma once

#include "mode.h"

#include <memory>
#include <string>

class Editor;
class EditorBufferView;
class EditorMessageBarView;
class EditorStatusBarView;

class EditorDrawingController
{
public:
    explicit EditorDrawingController(Editor& editor);
    ~EditorDrawingController();

    void draw();
    void drawBufferView();
    void drawStatusBar();
    void drawStatusBarQuick();
    void drawMessageBar();
    void drawMessageBarQuick();
    void appendStatusBar(std::string& output);
    void appendMessageBar(std::string& output, bool includePopups);
    void refreshScreen();
    void updateCursorPosition(bool flushNow = true);
    void forceFullRedraw();

private:
    Editor& editor;
    std::unique_ptr<EditorBufferView> bufferView;
    std::unique_ptr<EditorStatusBarView> statusBarView;
    std::unique_ptr<EditorMessageBarView> messageBarView;

    int lastFrameOffsetY = -1;
    int lastFrameOffsetX = -1;
    int lastFrameCursorY = -1;
    int lastFrameVisualStartY = -1;
    int lastFrameVisualEndY = -1;
    Mode lastFrameMode = NORMAL;
    bool lastFrameCommandPopupActive = false;
    bool lastFrameCommandHistoryPopupActive = false;
};
