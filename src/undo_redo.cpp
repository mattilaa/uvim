#include "editor_lsp_query.h"

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
    currentBuffer->undoStack.push_back(state);
    currentBuffer->undoIndex++;

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
        currentBuffer->undoIndex--;
        const Buffer::EditState& state =
            currentBuffer->undoStack[currentBuffer->undoIndex];
        *lines = state.lines;

        if(currentBuffer->undoIndex > 0)
        {
            *cursorX = state.cursorX;
            *cursorY = state.cursorY;
        }

        if(*cursorY >= lines->size())
            *cursorY = lines->size() - 1;
        if(*cursorY < 0)
            *cursorY = 0;
        if(*cursorX > (*lines)[*cursorY].length())
            *cursorX = (*lines)[*cursorY].length();
        if(*cursorX < 0)
            *cursorX = 0;

        if(currentBuffer->undoIndex == currentBuffer->savedUndoIndex)
        {
            *dirty = false;
        }
        else
        {
            *dirty = true;
        }

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
        currentBuffer->undoIndex++;
        const Buffer::EditState& state =
            currentBuffer->undoStack[currentBuffer->undoIndex];
        *lines = state.lines;
        *cursorX = state.cursorX;
        *cursorY = state.cursorY;

        if(currentBuffer->undoIndex == currentBuffer->savedUndoIndex)
        {
            *dirty = false;
        }
        else
        {
            *dirty = true;
        }

        needsFullRedraw = true;
    }
    else
    {
        setStatusMessage("Already at newest change");
    }
}
