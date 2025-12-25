#pragma once

#include "editor_context.h"

class UndoManager
{
public:
    explicit UndoManager(EditorContext& ctx);

    void saveState();
    void undo();
    void redo();

private:
    EditorContext& ctx;
    static constexpr int MAX_UNDO_STACK_SIZE = 1000;
};
