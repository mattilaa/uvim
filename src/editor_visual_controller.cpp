#include "editor_visual_controller.h"
#include "editor.h"
#include <algorithm>
#include <cctype>
#include <string>

EditorVisualController::EditorVisualController(Editor& editor) : editor(editor)
{
}

void EditorVisualController::startVisualMode()
{
    editor.startVisualModeImpl();
}
void EditorVisualController::startVisualLineMode()
{
    editor.startVisualLineModeImpl();
}
void EditorVisualController::startVisualBlockMode()
{
    editor.startVisualBlockModeImpl();
}
void EditorVisualController::updateVisualSelection()
{
    editor.updateVisualSelectionImpl();
}
void EditorVisualController::updateVisualBlockSelection()
{
    editor.updateVisualBlockSelectionImpl();
}
bool EditorVisualController::isInSelection(int row, int col)
{
    return editor.isInSelectionImpl(row, col);
}
bool EditorVisualController::isInVisualBlock(int row, int col)
{
    return editor.isInVisualBlockImpl(row, col);
}
void EditorVisualController::getVisualBlockBounds(int& startY, int& startX,
                                                  int& endY, int& endX)
{
    editor.getVisualBlockBoundsImpl(startY, startX, endY, endX);
}
void EditorVisualController::getSelectionBounds(int& startY, int& startX,
                                                int& endY, int& endX)
{
    editor.getSelectionBoundsImpl(startY, startX, endY, endX);
}
void EditorVisualController::setVisualRange()
{
    editor.setVisualRangeImpl();
}
void EditorVisualController::swapVisualEnds()
{
    editor.swapVisualEndsImpl();
}
void EditorVisualController::swapVisualBlockCorner()
{
    editor.swapVisualBlockCornerImpl();
}
void EditorVisualController::prepareBlockInsert(bool atEnd)
{
    editor.prepareBlockInsertImpl(atEnd);
}
void EditorVisualController::indentSelection()
{
    editor.indentSelectionImpl();
}
void EditorVisualController::dedentSelection()
{
    editor.dedentSelectionImpl();
}
void EditorVisualController::autoIndentSelection()
{
    editor.autoIndentSelectionImpl();
}
void EditorVisualController::lowercaseSelection()
{
    editor.lowercaseSelectionImpl();
}
void EditorVisualController::uppercaseSelection()
{
    editor.uppercaseSelectionImpl();
}
void EditorVisualController::toggleCaseSelection()
{
    editor.toggleCaseSelectionImpl();
}
void EditorVisualController::yankLineSelection()
{
    editor.yankLineSelectionImpl();
}
void EditorVisualController::deleteLineSelection()
{
    editor.deleteLineSelectionImpl();
}
void EditorVisualController::indentLineSelection()
{
    editor.indentLineSelectionImpl();
}
void EditorVisualController::dedentLineSelection()
{
    editor.dedentLineSelectionImpl();
}
void EditorVisualController::autoIndentLineSelection()
{
    editor.autoIndentLineSelectionImpl();
}

void Editor::startVisualModeImpl()
{
    setMode(VISUAL);
}

void Editor::startVisualLineModeImpl()
{
    setMode(VISUAL_LINE);
}

void Editor::startVisualBlockModeImpl()
{
    setMode(VISUAL_BLOCK);
    currentBuffer->visualBlockStartX = *cursorX;
    currentBuffer->visualBlockStartY = *cursorY;
    currentBuffer->visualBlockEndX = *cursorX;
    currentBuffer->visualBlockEndY = *cursorY;
    currentBuffer->visualBlockInsertText.clear();
}

void Editor::updateVisualSelectionImpl()
{
    currentBuffer->visualEndX = *cursorX;
    currentBuffer->visualEndY = *cursorY;
}

void Editor::updateVisualBlockSelectionImpl()
{
    currentBuffer->visualBlockEndX = *cursorX;
    currentBuffer->visualBlockEndY = *cursorY;
}

bool Editor::isInSelectionImpl(int row, int col)
{
    if(currentMode != VISUAL && currentMode != VISUAL_LINE)
        return false;

    if(currentMode == VISUAL_LINE)
    {
        int startY =
            std::min(currentBuffer->visualStartY, currentBuffer->visualEndY);
        int endY =
            std::max(currentBuffer->visualStartY, currentBuffer->visualEndY);
        return row >= startY && row <= endY;
    }

    int startY, startX, endY, endX;
    getSelectionBounds(startY, startX, endY, endX);

    if(row < startY || row > endY)
        return false;
    if(row == startY && row == endY)
        return col >= startX && col <= endX;
    if(row == startY)
        return col >= startX;
    if(row == endY)
        return col <= endX;
    return true;
}

bool Editor::isInVisualBlockImpl(int row, int col)
{
    if(currentMode != VISUAL_BLOCK)
        return false;

    int startY, startX, endY, endX;
    getVisualBlockBounds(startY, startX, endY, endX);

    return row >= startY && row <= endY && col >= startX && col <= endX;
}

void Editor::getVisualBlockBoundsImpl(int& startY, int& startX, int& endY,
                                      int& endX)
{
    startY = std::min(currentBuffer->visualBlockStartY,
                      currentBuffer->visualBlockEndY);
    endY = std::max(currentBuffer->visualBlockStartY,
                    currentBuffer->visualBlockEndY);
    startX = std::min(currentBuffer->visualBlockStartX,
                      currentBuffer->visualBlockEndX);
    endX = std::max(currentBuffer->visualBlockStartX,
                    currentBuffer->visualBlockEndX);
}

void Editor::getSelectionBoundsImpl(int& startY, int& startX, int& endY,
                                    int& endX)
{
    if(currentBuffer->visualStartY < currentBuffer->visualEndY ||
       (currentBuffer->visualStartY == currentBuffer->visualEndY &&
        currentBuffer->visualStartX <= currentBuffer->visualEndX))
    {
        startY = currentBuffer->visualStartY;
        startX = currentBuffer->visualStartX;
        endY = currentBuffer->visualEndY;
        endX = currentBuffer->visualEndX;
    }
    else
    {
        startY = currentBuffer->visualEndY;
        startX = currentBuffer->visualEndX;
        endY = currentBuffer->visualStartY;
        endX = currentBuffer->visualStartX;
    }
}

void Editor::setVisualRangeImpl()
{
    currentBuffer->visualEndX = *cursorX;
    currentBuffer->visualEndY = *cursorY;
}

void Editor::swapVisualEndsImpl()
{
    std::swap(*cursorX, currentBuffer->visualStartX);
    std::swap(*cursorY, currentBuffer->visualStartY);
    currentBuffer->visualEndX = *cursorX;
    currentBuffer->visualEndY = *cursorY;
    adjustViewport();
}

void Editor::swapVisualBlockCornerImpl()
{
    std::swap(*cursorX, currentBuffer->visualBlockStartX);
    std::swap(*cursorY, currentBuffer->visualBlockStartY);
    currentBuffer->visualBlockEndX = *cursorX;
    currentBuffer->visualBlockEndY = *cursorY;
    adjustViewport();
}

void Editor::prepareBlockInsertImpl(bool atEnd)
{
    int startY, startX, endY, endX;
    getVisualBlockBounds(startY, startX, endY, endX);

    currentBuffer->visualBlockStartY = startY;
    currentBuffer->visualBlockEndY = endY;
    currentBuffer->visualBlockStartX = atEnd ? endX + 1 : startX;
    currentBuffer->visualBlockInsertText.clear();

    *cursorY = startY;
    *cursorX = atEnd ? endX + 1 : startX;

    if(*cursorY < (int)lines->size())
    {
        std::string& line = (*lines)[*cursorY];
        while((int)line.length() < *cursorX)
        {
            line += ' ';
        }
    }
}

void Editor::indentSelectionImpl()
{
    int startY, startX, endY, endX;
    getSelectionBounds(startY, startX, endY, endX);

    for(int y = startY; y <= endY && y < (int)lines->size(); y++)
    {
        (*lines)[y] = "    " + (*lines)[y];
    }
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

void Editor::dedentSelectionImpl()
{
    int startY, startX, endY, endX;
    getSelectionBounds(startY, startX, endY, endX);

    for(int y = startY; y <= endY && y < (int)lines->size(); y++)
    {
        std::string& line = (*lines)[y];
        int remove = 0;
        while(remove < 4 && remove < (int)line.length() &&
              (line[remove] == ' ' || line[remove] == '\t'))
        {
            remove++;
        }
        if(remove > 0)
            line.erase(0, remove);
    }
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

void Editor::autoIndentSelectionImpl()
{
    int startY, startX, endY, endX;
    getSelectionBounds(startY, startX, endY, endX);

    for(int y = startY; y <= endY && y < (int)lines->size(); y++)
    {
        autoIndentLine(y);
    }
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

void Editor::lowercaseSelectionImpl()
{
    int startY, startX, endY, endX;
    getSelectionBounds(startY, startX, endY, endX);

    for(int y = startY; y <= endY && y < (int)lines->size(); y++)
    {
        std::string& line = (*lines)[y];
        int start = (y == startY) ? startX : 0;
        int end = (y == endY) ? std::min(endX + 1, (int)line.length())
                              : line.length();

        for(int x = start; x < end; x++)
        {
            line[x] = std::tolower(line[x]);
        }
    }
    *dirty = true;
    saveState();
    setMode(NORMAL);
    needsFullRedraw = true;
}

void Editor::uppercaseSelectionImpl()
{
    int startY, startX, endY, endX;
    getSelectionBounds(startY, startX, endY, endX);

    for(int y = startY; y <= endY && y < (int)lines->size(); y++)
    {
        std::string& line = (*lines)[y];
        int start = (y == startY) ? startX : 0;
        int end = (y == endY) ? std::min(endX + 1, (int)line.length())
                              : line.length();

        for(int x = start; x < end; x++)
        {
            line[x] = std::toupper(line[x]);
        }
    }
    *dirty = true;
    saveState();
    setMode(NORMAL);
    needsFullRedraw = true;
}

void Editor::toggleCaseSelectionImpl()
{
    int startY, startX, endY, endX;
    getSelectionBounds(startY, startX, endY, endX);

    for(int y = startY; y <= endY && y < (int)lines->size(); y++)
    {
        std::string& line = (*lines)[y];
        int start = (y == startY) ? startX : 0;
        int end = (y == endY) ? std::min(endX + 1, (int)line.length())
                              : line.length();

        for(int x = start; x < end; x++)
        {
            if(std::isupper(line[x]))
                line[x] = std::tolower(line[x]);
            else if(std::islower(line[x]))
                line[x] = std::toupper(line[x]);
        }
    }
    *dirty = true;
    saveState();
    setMode(NORMAL);
    needsFullRedraw = true;
}

void Editor::yankLineSelectionImpl()
{
    int startY =
        std::min(currentBuffer->visualStartY, currentBuffer->visualEndY);
    int endY = std::max(currentBuffer->visualStartY, currentBuffer->visualEndY);

    yankBuffer.clear();
    for(int y = startY; y <= endY && y < (int)lines->size(); y++)
    {
        yankBuffer += (*lines)[y] + "\n";
    }

    if(useSystemClipboard && !yankBuffer.empty())
    {
        setSystemClipboard(yankBuffer);
    }
    setStatusMessage(std::to_string(endY - startY + 1) + " lines yanked");
}

void Editor::deleteLineSelectionImpl()
{
    int startY =
        std::min(currentBuffer->visualStartY, currentBuffer->visualEndY);
    int endY = std::max(currentBuffer->visualStartY, currentBuffer->visualEndY);

    yankLineSelection();

    for(int y = endY; y >= startY; y--)
    {
        if(y < (int)lines->size())
        {
            lines->erase(lines->begin() + y);
            if(currentBuffer && currentBuffer->blameValid &&
               y < (int)currentBuffer->blameEntries.size())
            {
                currentBuffer->blameEntries.erase(
                    currentBuffer->blameEntries.begin() + y);
            }
        }
    }

    if(lines->empty())
        lines->push_back("");

    *cursorY = std::min(startY, (int)lines->size() - 1);
    *cursorX = 0;
    *dirty = true;
    if(currentBuffer && currentBuffer->blameValid)
    {
        currentBuffer->blameStart = 0;
        currentBuffer->blameEnd = (int)currentBuffer->blameEntries.size() - 1;
    }
    saveState();
    setMode(NORMAL);
    needsFullRedraw = true;
}

void Editor::indentLineSelectionImpl()
{
    int startY =
        std::min(currentBuffer->visualStartY, currentBuffer->visualEndY);
    int endY = std::max(currentBuffer->visualStartY, currentBuffer->visualEndY);

    for(int y = startY; y <= endY && y < (int)lines->size(); y++)
    {
        (*lines)[y] = "    " + (*lines)[y];
    }
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

void Editor::dedentLineSelectionImpl()
{
    int startY =
        std::min(currentBuffer->visualStartY, currentBuffer->visualEndY);
    int endY = std::max(currentBuffer->visualStartY, currentBuffer->visualEndY);

    for(int y = startY; y <= endY && y < (int)lines->size(); y++)
    {
        std::string& line = (*lines)[y];
        int remove = 0;
        while(remove < 4 && remove < (int)line.length() &&
              (line[remove] == ' ' || line[remove] == '\t'))
        {
            remove++;
        }
        if(remove > 0)
            line.erase(0, remove);
    }
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

void Editor::autoIndentLineSelectionImpl()
{
    int startY =
        std::min(currentBuffer->visualStartY, currentBuffer->visualEndY);
    int endY = std::max(currentBuffer->visualStartY, currentBuffer->visualEndY);

    for(int y = startY; y <= endY && y < (int)lines->size(); y++)
    {
        autoIndentLine(y);
    }
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

// ============================================================================
// Marks
// ============================================================================
