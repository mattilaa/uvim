#include "buffer_manager.h"

BufferManager::BufferManager(EditorContext& ctx) : ctx(ctx) {}

void BufferManager::createNewBuffer()
{
    ctx.buffers.push_back(std::make_unique<Buffer>());
    ctx.currentBufferIndex = ctx.buffers.size() - 1;
    ctx.currentBuffer = ctx.buffers[ctx.currentBufferIndex].get();
    ctx.updateCurrentBufferPointers();
}

void BufferManager::switchToBuffer(int index)
{
    if(index < 0 || index >= (int)ctx.buffers.size())
        return;

    // Save current buffer state
    if(ctx.currentBuffer)
    {
        saveBufferState();
    }

    ctx.currentBufferIndex = index;
    ctx.currentBuffer = ctx.buffers[ctx.currentBufferIndex].get();
    ctx.updateCurrentBufferPointers();
    restoreBufferState();

    ctx.needsFullRedraw = true;
    ctx.statusMessage = "Switched to buffer " + std::to_string(index + 1);
}

void BufferManager::nextBuffer()
{
    if(ctx.buffers.size() <= 1)
    {
        ctx.statusMessage = "No other buffers";
        return;
    }

    int nextIndex = (ctx.currentBufferIndex + 1) % ctx.buffers.size();
    switchToBuffer(nextIndex);
}

void BufferManager::previousBuffer()
{
    if(ctx.buffers.size() <= 1)
    {
        ctx.statusMessage = "No other buffers";
        return;
    }

    int prevIndex =
        (ctx.currentBufferIndex - 1 + ctx.buffers.size()) % ctx.buffers.size();
    switchToBuffer(prevIndex);
}

void BufferManager::closeCurrentBuffer()
{
    if(ctx.buffers.size() <= 1)
    {
        // Last buffer - just clear it
        ctx.currentBuffer->lines.clear();
        ctx.currentBuffer->lines.push_back("");
        ctx.currentBuffer->filename.clear();
        ctx.currentBuffer->dirty = false;
        ctx.currentBuffer->cursorX = 0;
        ctx.currentBuffer->cursorY = 0;
        ctx.currentBuffer->offsetX = 0;
        ctx.currentBuffer->offsetY = 0;
        ctx.needsFullRedraw = true;
        ctx.statusMessage = "Buffer cleared";
        return;
    }

    // Remove current buffer
    ctx.buffers.erase(ctx.buffers.begin() + ctx.currentBufferIndex);

    // Adjust index
    if(ctx.currentBufferIndex >= (int)ctx.buffers.size())
    {
        ctx.currentBufferIndex = ctx.buffers.size() - 1;
    }

    ctx.currentBuffer = ctx.buffers[ctx.currentBufferIndex].get();
    ctx.updateCurrentBufferPointers();
    ctx.needsFullRedraw = true;
    ctx.statusMessage = "Buffer closed. " + std::to_string(ctx.buffers.size()) +
                        " buffer(s) remaining";
}

void BufferManager::listBuffers()
{
    std::string msg = "Buffers: ";
    for(size_t i = 0; i < ctx.buffers.size(); i++)
    {
        if(i > 0)
            msg += ", ";
        if((int)i == ctx.currentBufferIndex)
            msg += "[";
        msg += std::to_string(i + 1) + ":";
        if(ctx.buffers[i]->filename.empty())
        {
            msg += "[No Name]";
        }
        else
        {
            // Extract just filename
            size_t lastSlash = ctx.buffers[i]->filename.find_last_of('/');
            if(lastSlash != std::string::npos)
            {
                msg += ctx.buffers[i]->filename.substr(lastSlash + 1);
            }
            else
            {
                msg += ctx.buffers[i]->filename;
            }
        }
        if(ctx.buffers[i]->dirty)
            msg += "+";
        if((int)i == ctx.currentBufferIndex)
            msg += "]";
    }
    ctx.statusMessage = msg;
}

int BufferManager::findBufferByFilename(const std::string& fname)
{
    for(size_t i = 0; i < ctx.buffers.size(); i++)
    {
        if(ctx.buffers[i]->filename == fname)
            return i;
    }
    return -1;
}

void BufferManager::saveBufferState()
{
    // Buffer state is maintained through pointers
}

void BufferManager::restoreBufferState()
{
    // Restore search state
    ctx.searchQuery = ctx.currentBuffer->lastSearchQuery;
    ctx.searchForward = ctx.currentBuffer->lastSearchForward;
}
