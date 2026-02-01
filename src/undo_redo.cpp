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
    state.blameEntries = currentBuffer->blameEntries;
    state.blameStart = currentBuffer->blameStart;
    state.blameEnd = currentBuffer->blameEnd;
    state.blameValid = currentBuffer->blameValid;

    if(currentBuffer->undoIndex >= 0 &&
       currentBuffer->undoIndex < (int)currentBuffer->undoStack.size())
    {
        const Buffer::EditState& last =
            currentBuffer->undoStack[currentBuffer->undoIndex];
        if(last.lines == state.lines)
        {
            bool isSaved = false;
            if(currentBuffer->savedUndoIndex >= 0 &&
               currentBuffer->savedUndoIndex <
                   (int)currentBuffer->undoStack.size())
            {
                const auto& saved =
                    currentBuffer->undoStack[currentBuffer->savedUndoIndex];
                isSaved = (saved.lines == state.lines);
            }
            if(dirty)
                *dirty = !isSaved;
            return; // Avoid duplicate undo steps with identical content.
        }
    }

    currentBuffer->undoStack.push_back(state);
    currentBuffer->undoIndex++;
    currentBuffer->lspSyncNeeded = true;
    if(showGitBlame)
        currentBuffer->blameValid = false;

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
        currentBuffer->blameEntries = state.blameEntries;
        currentBuffer->blameStart = state.blameStart;
        currentBuffer->blameEnd = state.blameEnd;
        currentBuffer->blameValid = state.blameValid;

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

        bool isSaved = false;
        if(currentBuffer->savedUndoIndex >= 0 &&
           currentBuffer->savedUndoIndex <
               (int)currentBuffer->undoStack.size())
        {
            const auto& saved =
                currentBuffer->undoStack[currentBuffer->savedUndoIndex];
            isSaved = (saved.lines == *lines);
        }
        *dirty = !isSaved;
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
        currentBuffer->blameEntries = state.blameEntries;
        currentBuffer->blameStart = state.blameStart;
        currentBuffer->blameEnd = state.blameEnd;
        currentBuffer->blameValid = state.blameValid;

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

        bool isSaved = false;
        if(currentBuffer->savedUndoIndex >= 0 &&
           currentBuffer->savedUndoIndex <
               (int)currentBuffer->undoStack.size())
        {
            const auto& saved =
                currentBuffer->undoStack[currentBuffer->savedUndoIndex];
            isSaved = (saved.lines == *lines);
        }
        *dirty = !isSaved;
        currentBuffer->lspSyncNeeded = true;

        needsFullRedraw = true;
    }
    else
    {
        setStatusMessage("Already at newest change");
    }
}
