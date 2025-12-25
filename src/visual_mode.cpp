#include "visual_mode.h"
#include "text_operations.h"
#include <algorithm>

VisualMode::VisualMode(EditorContext& ctx, TextOperations& textOps)
    : ctx(ctx), textOps(textOps)
{
}

void VisualMode::startVisualMode()
{
    ctx.currentMode = Mode::VISUAL;
    ctx.currentBuffer->visualStartX = *ctx.cursorX;
    ctx.currentBuffer->visualStartY = *ctx.cursorY;
    ctx.currentBuffer->visualEndX = *ctx.cursorX;
    ctx.currentBuffer->visualEndY = *ctx.cursorY;
}

void VisualMode::startVisualLineMode()
{
    ctx.currentMode = Mode::VISUAL_LINE;
    ctx.currentBuffer->visualStartX = 0;
    ctx.currentBuffer->visualStartY = *ctx.cursorY;
    ctx.currentBuffer->visualEndX = (*ctx.lines)[*ctx.cursorY].length();
    ctx.currentBuffer->visualEndY = *ctx.cursorY;
}

void VisualMode::startVisualBlockMode()
{
    ctx.currentMode = Mode::VISUAL_BLOCK;
    ctx.currentBuffer->visualStartX = *ctx.cursorX;
    ctx.currentBuffer->visualStartY = *ctx.cursorY;
    ctx.currentBuffer->visualEndX = *ctx.cursorX;
    ctx.currentBuffer->visualEndY = *ctx.cursorY;
    ctx.visualBlockInsertText.clear();
    ctx.visualBlockInsertStartX = *ctx.cursorX;
}

void VisualMode::updateVisualSelection()
{
    ctx.currentBuffer->visualEndX = *ctx.cursorX;
    ctx.currentBuffer->visualEndY = *ctx.cursorY;
}

void VisualMode::updateVisualBlockSelection()
{
    ctx.currentBuffer->visualEndX = *ctx.cursorX;
    ctx.currentBuffer->visualEndY = *ctx.cursorY;
}

bool VisualMode::isInSelection(int row, int col) const
{
    if(ctx.currentMode != Mode::VISUAL && ctx.currentMode != Mode::VISUAL_LINE)
        return false;

    int startY, startX, endY, endX;
    getSelectionBounds(startY, startX, endY, endX);

    if(ctx.currentMode == Mode::VISUAL_LINE)
    {
        return row >= startY && row <= endY;
    }

    // Character-wise visual mode
    if(row < startY || row > endY)
        return false;

    if(startY == endY)
    {
        return col >= startX && col <= endX;
    }

    if(row == startY)
        return col >= startX;
    if(row == endY)
        return col <= endX;

    return true;
}

bool VisualMode::isInVisualBlock(int row, int col) const
{
    if(ctx.currentMode != Mode::VISUAL_BLOCK)
        return false;

    int startY, startX, endY, endX;
    getVisualBlockBounds(startY, startX, endY, endX);

    return row >= startY && row <= endY && col >= startX && col <= endX;
}

void VisualMode::getSelectionBounds(int& startY, int& startX, int& endY,
                                    int& endX) const
{
    startY = ctx.currentBuffer->visualStartY;
    startX = ctx.currentBuffer->visualStartX;
    endY = ctx.currentBuffer->visualEndY;
    endX = ctx.currentBuffer->visualEndX;

    // Normalize so start <= end
    if(startY > endY || (startY == endY && startX > endX))
    {
        std::swap(startY, endY);
        std::swap(startX, endX);
    }
}

void VisualMode::getVisualBlockBounds(int& startY, int& startX, int& endY,
                                      int& endX) const
{
    startY = std::min(ctx.currentBuffer->visualStartY,
                      ctx.currentBuffer->visualEndY);
    endY = std::max(ctx.currentBuffer->visualStartY,
                    ctx.currentBuffer->visualEndY);
    startX = std::min(ctx.currentBuffer->visualStartX,
                      ctx.currentBuffer->visualEndX);
    endX = std::max(ctx.currentBuffer->visualStartX,
                    ctx.currentBuffer->visualEndX);
}

void VisualMode::deleteVisualBlock()
{
    int startY, startX, endY, endX;
    getVisualBlockBounds(startY, startX, endY, endX);

    ctx.yankBuffer.clear();

    for(int y = startY; y <= endY && y < (int)ctx.lines->size(); y++)
    {
        std::string& line = (*ctx.lines)[y];
        if(startX < (int)line.length())
        {
            int deleteEnd = std::min(endX + 1, (int)line.length());
            ctx.yankBuffer += line.substr(startX, deleteEnd - startX);
            line.erase(startX, deleteEnd - startX);
        }
        ctx.yankBuffer += "\n";
    }

    *ctx.cursorX = startX;
    *ctx.cursorY = startY;
    *ctx.dirty = true;
    ctx.needsFullRedraw = true;
}

void VisualMode::yankVisualBlock()
{
    int startY, startX, endY, endX;
    getVisualBlockBounds(startY, startX, endY, endX);

    ctx.yankBuffer.clear();

    for(int y = startY; y <= endY && y < (int)ctx.lines->size(); y++)
    {
        const std::string& line = (*ctx.lines)[y];
        if(startX < (int)line.length())
        {
            int copyEnd = std::min(endX + 1, (int)line.length());
            ctx.yankBuffer += line.substr(startX, copyEnd - startX);
        }
        ctx.yankBuffer += "\n";
    }

    int lineCount = endY - startY + 1;
    ctx.statusMessage =
        "Block of " + std::to_string(lineCount) + " lines yanked";
}

void VisualMode::changeVisualBlock()
{
    int startY, startX, endY, endX;
    getVisualBlockBounds(startY, startX, endY, endX);

    // Store for block insert
    ctx.visualBlockInsertStartX = startX;
    ctx.visualBlockInsertText.clear();

    // Delete the block
    deleteVisualBlock();

    // Enter insert mode
    ctx.currentMode = Mode::INSERT;
    *ctx.cursorX = startX;
    *ctx.cursorY = startY;
}

void VisualMode::applyVisualBlockInsert()
{
    if(ctx.visualBlockInsertText.empty())
        return;

    int startY = std::min(ctx.currentBuffer->visualStartY,
                          ctx.currentBuffer->visualEndY);
    int endY = std::max(ctx.currentBuffer->visualStartY,
                        ctx.currentBuffer->visualEndY);
    int insertX = ctx.visualBlockInsertStartX;

    for(int y = startY; y <= endY && y < (int)ctx.lines->size(); y++)
    {
        std::string& line = (*ctx.lines)[y];

        // Extend line with spaces if needed
        while((int)line.length() < insertX)
        {
            line += ' ';
        }

        line.insert(insertX, ctx.visualBlockInsertText);
    }

    *ctx.dirty = true;
    ctx.needsFullRedraw = true;
}
