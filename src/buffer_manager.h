#pragma once

#include "editor_context.h"
#include <string>

class BufferManager
{
public:
    explicit BufferManager(EditorContext& ctx);

    void createNewBuffer();
    void switchToBuffer(int index);
    void nextBuffer();
    void previousBuffer();
    void closeCurrentBuffer();
    void listBuffers();
    int findBufferByFilename(const std::string& fname);

    void saveBufferState();
    void restoreBufferState();
private:
    EditorContext& ctx;
};
