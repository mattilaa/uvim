#include "undo_manager.h"
#include <algorithm>

UndoManager::UndoManager(EditorContext& ctx) : ctx(ctx) {}

void UndoManager::saveState()
{
    if(!ctx.currentBuffer)
        return;

    Buffer::EditState state;
    state.lines = *ctx.lines;
    state.cursorX = *ctx.cursorX;
    state.cursorY = *ctx.cursorY;

    // Remove any states after current position (branching)
    if(ctx.currentBuffer->undoIndex >= 0 &&
       ctx.currentBuffer->undoIndex <
           (int)ctx.currentBuffer->undoStack.size() - 1)
    {
        ctx.currentBuffer->undoStack.erase(
            ctx.currentBuffer->undoStack.begin() +
                ctx.currentBuffer->undoIndex + 1,
            ctx.currentBuffer->undoStack.end());
    }

    ctx.currentBuffer->undoStack.push_back(state);
    ctx.currentBuffer->undoIndex = ctx.currentBuffer->undoStack.size() - 1;

    // Limit undo stack size
    if(ctx.currentBuffer->undoStack.size() > MAX_UNDO_STACK_SIZE)
    {
        ctx.currentBuffer->undoStack.erase(
            ctx.currentBuffer->undoStack.begin());
        ctx.currentBuffer->undoIndex--;
        if(ctx.currentBuffer->savedUndoIndex > 0)
        {
            ctx.currentBuffer->savedUndoIndex--;
        }
    }
}

void UndoManager::undo()
{
    if(!ctx.currentBuffer || ctx.currentBuffer->undoIndex <= 0)
    {
        ctx.statusMessage = "Already at oldest change";
        return;
    }

    ctx.currentBuffer->undoIndex--;
    const Buffer::EditState& state =
        ctx.currentBuffer->undoStack[ctx.currentBuffer->undoIndex];
    *ctx.lines = state.lines;
    *ctx.cursorX = state.cursorX;
    *ctx.cursorY = state.cursorY;

    // Check if back to saved state
    if(ctx.currentBuffer->undoIndex == ctx.currentBuffer->savedUndoIndex)
    {
        *ctx.dirty = false;
    }
    else
    {
        *ctx.dirty = true;
    }

    // Ensure cursor is valid
    if(*ctx.cursorY >= (int)ctx.lines->size())
    {
        *ctx.cursorY = ctx.lines->size() - 1;
    }
    if(*ctx.cursorY >= 0 &&
       *ctx.cursorX > (int)(*ctx.lines)[*ctx.cursorY].length())
    {
        *ctx.cursorX = (*ctx.lines)[*ctx.cursorY].length();
    }

    *ctx.wantedX = *ctx.cursorX;
    ctx.needsFullRedraw = true;
    ctx.statusMessage = "Undo";
}

void UndoManager::redo()
{
    if(!ctx.currentBuffer || ctx.currentBuffer->undoIndex >=
                                 (int)ctx.currentBuffer->undoStack.size() - 1)
    {
        ctx.statusMessage = "Already at newest change";
        return;
    }

    ctx.currentBuffer->undoIndex++;
    const Buffer::EditState& state =
        ctx.currentBuffer->undoStack[ctx.currentBuffer->undoIndex];
    *ctx.lines = state.lines;
    *ctx.cursorX = state.cursorX;
    *ctx.cursorY = state.cursorY;

    // Check if back to saved state
    if(ctx.currentBuffer->undoIndex == ctx.currentBuffer->savedUndoIndex)
    {
        *ctx.dirty = false;
    }
    else
    {
        *ctx.dirty = true;
    }

    // Ensure cursor is valid
    if(*ctx.cursorY >= (int)ctx.lines->size())
    {
        *ctx.cursorY = ctx.lines->size() - 1;
    }
    if(*ctx.cursorY >= 0 &&
       *ctx.cursorX > (int)(*ctx.lines)[*ctx.cursorY].length())
    {
        *ctx.cursorX = (*ctx.lines)[*ctx.cursorY].length();
    }

    *ctx.wantedX = *ctx.cursorX;
    ctx.needsFullRedraw = true;
    ctx.statusMessage = "Redo";
}
