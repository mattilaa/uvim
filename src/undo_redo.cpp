#include "editor.h"
#include <algorithm>

void Editor::saveState()
{
    if(!currentBuffer)
        return;

    if(currentBuffer->undoIndex < currentBuffer->undoStack.size() - 1)
    {
        if(currentBuffer->savedUndoIndex > currentBuffer->undoIndex)
        {
            currentBuffer->savedUndoIndex = -1;
        }

        currentBuffer->undoStack.erase(currentBuffer->undoStack.begin() +
                                           currentBuffer->undoIndex + 1,
                                       currentBuffer->undoStack.end());
    }

    Buffer::EditState state;
    state.lines = *lines;
    state.cursorX = *cursorX;
    state.cursorY = *cursorY;

    if(currentBuffer->undoIndex >= 0 &&
       currentBuffer->undoIndex < (int)currentBuffer->undoStack.size())
    {
        const Buffer::EditState& last =
            currentBuffer->undoStack[currentBuffer->undoIndex];
        if(last.lines == state.lines)
        {
            return; // Avoid duplicate undo steps with identical content.
        }
    }

    currentBuffer->undoStack.push_back(state);
    currentBuffer->undoIndex++;
    currentBuffer->lspSyncNeeded = true;

    if(currentBuffer->undoStack.size() > 100)
    {
        currentBuffer->undoStack.erase(currentBuffer->undoStack.begin());
        currentBuffer->undoIndex--;

        if(currentBuffer->savedUndoIndex >= 0)
        {
            currentBuffer->savedUndoIndex--;
            if(currentBuffer->savedUndoIndex < 0)
            {
                currentBuffer->savedUndoIndex = -1;
            }
        }
    }
}

void Editor::undo()
{
    if(!currentBuffer)
        return;

    if(currentBuffer->undoIndex > 0)
    {
        int prevCursorX = *cursorX;
        int prevCursorY = *cursorY;

        currentBuffer->undoIndex--;
        const Buffer::EditState& state =
            currentBuffer->undoStack[currentBuffer->undoIndex];
        *lines = state.lines;

        if(lines->empty())
        {
            *cursorY = 0;
            *cursorX = 0;
        }
        else
        {
            *cursorY = std::clamp(prevCursorY, 0, (int)lines->size() - 1);
            *cursorX =
                std::clamp(prevCursorX, 0, (int)(*lines)[*cursorY].length());
        }

        adjustViewport();

        if(currentBuffer->undoIndex == currentBuffer->savedUndoIndex)
        {
            *dirty = false;
        }
        else
        {
            *dirty = true;
        }
        currentBuffer->lspSyncNeeded = true;

        needsFullRedraw = true;
    }
    else
    {
        setStatusMessage("Already at oldest change");
    }
}

void Editor::redo()
{
    if(!currentBuffer)
        return;

    if(currentBuffer->undoIndex < currentBuffer->undoStack.size() - 1)
    {
        int prevCursorX = *cursorX;
        int prevCursorY = *cursorY;

        currentBuffer->undoIndex++;
        const Buffer::EditState& state =
            currentBuffer->undoStack[currentBuffer->undoIndex];
        *lines = state.lines;

        // Clamp cursor to valid range for current buffer
        if(lines->empty())
        {
            *cursorY = 0;
            *cursorX = 0;
        }
        else
        {
            *cursorY = std::clamp(prevCursorY, 0, (int)lines->size() - 1);
            *cursorX =
                std::clamp(prevCursorX, 0, (int)(*lines)[*cursorY].length());
        }

        adjustViewport();

        if(currentBuffer->undoIndex == currentBuffer->savedUndoIndex)
        {
            *dirty = false;
        }
        else
        {
            *dirty = true;
        }
        currentBuffer->lspSyncNeeded = true;

        needsFullRedraw = true;
    }
    else
    {
        setStatusMessage("Already at newest change");
    }
}
