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
    void handleOperatorPendingMode(int c);
    void handleNormalMode(int c);
    void handleInsertMode(int c);
    void handleVisualMode(int c);
    void handleVisualBlockMode(int c);
    void handleCommandMode(int c);
    void handleSearchMode(int c);

private:
    Editor& editor;
};
