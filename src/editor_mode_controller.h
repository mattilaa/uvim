#pragma once

class Editor;

class EditorModeController
{
public:
    explicit EditorModeController(Editor& editor);

    bool dispatchModeKey(int c);
    void syncModeFromStateMachine();
    void handleKeypress(int c);
    bool handleEmojiPopupKey(int c);

private:
    Editor& editor;
};
