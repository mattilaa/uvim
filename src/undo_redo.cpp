#include "editor.h"
#include <algorithm>
#include <filesystem>
#include <fstream>

static bool write_lines_for_undo(const std::string& path,
                                 const std::vector<std::string>& lines)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if(!file)
        return false;
    for(size_t i = 0; i < lines.size(); ++i)
    {
        file << lines[i];
        if(i + 1 < lines.size())
            file << '\n';
    }
    return true;
}

static Buffer* find_buffer_for_undo(Editor& editor, const std::string& path)
{
    std::error_code targetEc;
    auto target = std::filesystem::weakly_canonical(path, targetEc);
    for(auto& buffer : editor.buffers)
    {
        if(!buffer)
            continue;
        std::error_code ec;
        auto candidate =
            std::filesystem::weakly_canonical(buffer->filename, ec);
        if((!targetEc && !ec && candidate == target) ||
           buffer->filename == path)
            return buffer.get();
    }
    return nullptr;
}

void Editor::clearRenameUndoSnapshot()
{
    renameUndoAvailable = false;
    renameUndoFiles.clear();
}

bool Editor::restoreRenameUndoSnapshot()
{
    if(!renameUndoAvailable)
        return false;

    int restored = 0;
    for(const auto& snapshot : renameUndoFiles)
    {
        if(Buffer* buffer = find_buffer_for_undo(*this, snapshot.path))
        {
            buffer->lines = snapshot.lines;
            buffer->dirty = snapshot.hadOpenBuffer ? snapshot.dirty : false;
            buffer->cursorX = snapshot.cursorX;
            buffer->cursorY = snapshot.cursorY;
            buffer->offsetX = snapshot.offsetX;
            buffer->offsetY = snapshot.offsetY;
            buffer->lspSyncNeeded = true;
            buffer->blameValid = false;
            ++restored;
            continue;
        }

        if(snapshot.fileExisted)
        {
            if(write_lines_for_undo(snapshot.path, snapshot.lines))
                ++restored;
        }
        else
        {
            std::error_code ec;
            if(std::filesystem::remove(snapshot.path, ec) || !ec)
                ++restored;
        }
    }

    if(currentBuffer)
    {
        lines = &currentBuffer->lines;
        cursorX = &currentBuffer->cursorX;
        cursorY = &currentBuffer->cursorY;
        offsetX = &currentBuffer->offsetX;
        offsetY = &currentBuffer->offsetY;
        dirty = &currentBuffer->dirty;
        currentBuffer->lspSyncNeeded = true;
        if(lines && !lines->empty())
        {
            *cursorY = std::clamp(*cursorY, 0, (int)lines->size() - 1);
            *cursorX = std::clamp(*cursorX, 0, (int)(*lines)[*cursorY].size());
        }
    }

    closeRenamePopup();
    clearRenameUndoSnapshot();
    adjustViewport();
    needsFullRedraw = true;
    setStatusMessage("rn: reverted rename in " + std::to_string(restored) +
                     " file(s)");
    return true;
}

void Editor::saveState()
{
    if(!currentBuffer)
        return;

    currentBuffer->invalidateSyntaxCache();
    clearRenameUndoSnapshot();

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
            currentBuffer->undoStack[currentBuffer->undoIndex].cursorX =
                state.cursorX;
            currentBuffer->undoStack[currentBuffer->undoIndex].cursorY =
                state.cursorY;

            if(dirty)
                currentBuffer->reconcileDirtyWithSavedContent();
            return; // Avoid duplicate undo steps with identical content.
        }
    }

    if(currentBuffer->undoIndex == 0 && currentBuffer->undoStack.size() == 1 &&
       currentBuffer->savedUndoIndex == 0)
    {
        currentBuffer->undoStack[0].cursorX = state.cursorX;
        currentBuffer->undoStack[0].cursorY = state.cursorY;
    }

    currentBuffer->undoStack.push_back(state);
    currentBuffer->undoIndex++;
    currentBuffer->reconcileDirtyWithSavedContent();
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

    if(restoreRenameUndoSnapshot())
        return;

    if(currentBuffer->undoIndex > 0)
    {
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
            *cursorY = std::clamp(state.cursorY, 0, (int)lines->size() - 1);
            *cursorX =
                std::clamp(state.cursorX, 0, (int)(*lines)[*cursorY].length());
        }

        adjustViewport();

        currentBuffer->reconcileDirtyWithSavedContent();
        currentBuffer->lspSyncNeeded = true;
        currentBuffer->invalidateSyntaxCache();

        needsFullRedraw = true;
    }
    else
    {
        currentBuffer->reconcileDirtyWithSavedContent();
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
            *cursorY = std::clamp(state.cursorY, 0, (int)lines->size() - 1);
            *cursorX =
                std::clamp(state.cursorX, 0, (int)(*lines)[*cursorY].length());
        }

        adjustViewport();

        currentBuffer->reconcileDirtyWithSavedContent();
        currentBuffer->lspSyncNeeded = true;
        currentBuffer->invalidateSyntaxCache();

        needsFullRedraw = true;
    }
    else
    {
        setStatusMessage("Already at newest change");
    }
}
