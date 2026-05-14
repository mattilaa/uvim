#pragma once

class Editor;

class EditorEditingController
{
public:
    explicit EditorEditingController(Editor& editor);

    void insertTab();
    void toggleCase();
    void joinLines();
    void insertLineAbove();
    void insertLineBelow();
    void deleteCurrentLine();
    void deleteToLineStart();
    void deleteCharAtCursor();
    void deleteCharBeforeCursor();
    void deleteWordBackward();
    void deleteWord();
    void yankWord();
    void handleBackspace();
    void replaceCharAtCursor(char c);
    void beginChangeRecording(int count);
    void recordChangeKey(int key);
    void deferChangeRecordingCommit();
    void commitChangeRecording();
    void cancelChangeRecording();
    void finishChangeRecordingIfDeferred();
    bool isRecordingChange() const;
    bool isReplayingChange() const;
    int readKeyRecorded();
    void repeatLastChange(int times);
    void insertUtf8Char(int c);
    void indentCurrentLine();
    void dedentCurrentLine();
    void handleLinewiseOperator(char op, int count);

private:
    Editor& editor;
};
