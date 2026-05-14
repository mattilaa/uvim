#pragma once

#include "mode.h"

#include <string>

class Editor;

class EditorBufferController
{
public:
    explicit EditorBufferController(Editor& editor);

    void createNewBuffer();
    void updateCurrentBufferPointers();
    void clearCurrentBufferPointers();
    bool hasBuffer() const;
    void ensureBufferForMode(Mode mode);
    void switchToBuffer(int index);
    void nextBuffer();
    void previousBuffer();
    void moveBufferLeft();
    void moveBufferRight();
    void closeCurrentBuffer();
    void listBuffers();
    int findBufferByFilename(const std::string& filename);
    void saveBufferState();
    void restoreBufferState();

private:
    Editor& editor;
};
