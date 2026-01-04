// Additional function implementations for Editor
// These functions are called by mode handlers

#include "editor.h"
#include "terminal.h"
#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <sys/stat.h>

// ============================================================================
// Extended Movement Commands
// ============================================================================

void Editor::moveToFirstNonBlank()
{
    if(*cursorY >= (int)lines->size())
        return;
    const std::string& line = (*lines)[*cursorY];
    *cursorX = 0;
    while(*cursorX < (int)line.length() &&
          (line[*cursorX] == ' ' || line[*cursorX] == '\t'))
    {
        (*cursorX)++;
    }
    *wantedX = *cursorX;
}

void Editor::moveParagraphForward()
{
    if(lines->empty())
        return;

    // Skip current non-empty lines
    while(*cursorY < (int)lines->size() - 1 && !(*lines)[*cursorY].empty())
    {
        (*cursorY)++;
    }
    // Skip empty lines
    while(*cursorY < (int)lines->size() - 1 && (*lines)[*cursorY].empty())
    {
        (*cursorY)++;
    }
    *cursorX = 0;
    adjustViewport();
}

void Editor::moveParagraphBackward()
{
    if(lines->empty())
        return;

    // Skip current empty lines
    while(*cursorY > 0 && (*lines)[*cursorY].empty())
    {
        (*cursorY)--;
    }
    // Skip non-empty lines
    while(*cursorY > 0 && !(*lines)[*cursorY].empty())
    {
        (*cursorY)--;
    }
    *cursorX = 0;
    adjustViewport();
}

void Editor::moveWordForwardBig()
{
    if(lines->empty() || *cursorY >= (int)lines->size())
        return;

    const std::string& line = (*lines)[*cursorY];

    // Skip current non-whitespace
    while(*cursorX < (int)line.length() && !std::isspace(line[*cursorX]))
    {
        (*cursorX)++;
    }
    // Skip whitespace
    while(*cursorX < (int)line.length() && std::isspace(line[*cursorX]))
    {
        (*cursorX)++;
    }
    // If at end of line, move to next line
    if(*cursorX >= (int)line.length() && *cursorY < (int)lines->size() - 1)
    {
        (*cursorY)++;
        *cursorX = 0;
        const std::string& nextLine = (*lines)[*cursorY];
        while(*cursorX < (int)nextLine.length() &&
              std::isspace(nextLine[*cursorX]))
        {
            (*cursorX)++;
        }
    }
    *wantedX = *cursorX;
}

void Editor::moveWordBackwardBig()
{
    if(lines->empty() || *cursorY >= (int)lines->size())
        return;

    // Move back one if not at start
    if(*cursorX > 0)
        (*cursorX)--;
    else if(*cursorY > 0)
    {
        (*cursorY)--;
        *cursorX = (*lines)[*cursorY].length();
        if(*cursorX > 0)
            (*cursorX)--;
    }

    const std::string& line = (*lines)[*cursorY];

    // Skip whitespace backward
    while(*cursorX > 0 && std::isspace(line[*cursorX]))
    {
        (*cursorX)--;
    }
    // Skip non-whitespace backward
    while(*cursorX > 0 && !std::isspace(line[*cursorX - 1]))
    {
        (*cursorX)--;
    }
    *wantedX = *cursorX;
}

void Editor::moveToEndOfWordBig()
{
    if(lines->empty() || *cursorY >= (int)lines->size())
        return;

    const std::string& line = (*lines)[*cursorY];

    // Move forward one
    if(*cursorX < (int)line.length())
        (*cursorX)++;

    // Skip whitespace
    while(*cursorX < (int)line.length() && std::isspace(line[*cursorX]))
    {
        (*cursorX)++;
    }
    // Move to end of word
    while(*cursorX < (int)line.length() - 1 &&
          !std::isspace(line[*cursorX + 1]))
    {
        (*cursorX)++;
    }
    *wantedX = *cursorX;
}

void Editor::findCharForwardBefore(char c)
{
    if(*cursorY >= (int)lines->size())
        return;
    const std::string& line = (*lines)[*cursorY];

    for(int i = *cursorX + 1; i < (int)line.length(); i++)
    {
        if(line[i] == c)
        {
            *cursorX = i - 1;
            if(*cursorX < 0)
                *cursorX = 0;
            *wantedX = *cursorX;
            return;
        }
    }
}

void Editor::findCharBackwardAfter(char c)
{
    if(*cursorY >= (int)lines->size())
        return;
    const std::string& line = (*lines)[*cursorY];

    for(int i = *cursorX - 1; i >= 0; i--)
    {
        if(line[i] == c)
        {
            *cursorX = i + 1;
            if(*cursorX >= (int)line.length())
                *cursorX = line.length() - 1;
            *wantedX = *cursorX;
            return;
        }
    }
}

// ============================================================================
// Character finding (f/F motions) - base implementations
// These may already exist in another file, but are needed by operator_pending
// ============================================================================

void Editor::findCharForward(char c)
{
    if(*cursorY >= (int)lines->size())
        return;
    const std::string& line = (*lines)[*cursorY];

    for(int i = *cursorX + 1; i < (int)line.length(); i++)
    {
        if(line[i] == c)
        {
            *cursorX = i;
            *wantedX = *cursorX;
            lastFindChar = c;
            lastFindForward = true;
            return;
        }
    }
}

void Editor::findCharBackward(char c)
{
    if(*cursorY >= (int)lines->size())
        return;
    const std::string& line = (*lines)[*cursorY];

    for(int i = *cursorX - 1; i >= 0; i--)
    {
        if(line[i] == c)
        {
            *cursorX = i;
            *wantedX = *cursorX;
            lastFindChar = c;
            lastFindForward = false;
            return;
        }
    }
}

// ============================================================================
// Scrolling Commands
// ============================================================================

void Editor::scrollToTop()
{
    *offsetY = 0;
    *cursorY = 0;
    *cursorX = 0;
    needsFullRedraw = true;
}

void Editor::scrollToBottom()
{
    if(lines->empty())
        return;
    *cursorY = lines->size() - 1;
    *cursorX = 0;
    adjustViewport();
    needsFullRedraw = true;
}

void Editor::scrollPageUp()
{
    int pageSize = screenRows - 2;
    *cursorY -= pageSize;
    if(*cursorY < 0)
        *cursorY = 0;
    *offsetY -= pageSize;
    if(*offsetY < 0)
        *offsetY = 0;
    if(*cursorY < (int)lines->size())
        *cursorX = std::min(*wantedX, (int)(*lines)[*cursorY].length());
    needsFullRedraw = true;
}

void Editor::scrollPageDown()
{
    int pageSize = screenRows - 2;
    *cursorY += pageSize;
    if(*cursorY >= (int)lines->size())
        *cursorY = lines->size() - 1;
    *offsetY += pageSize;
    int maxOffset = std::max(0, (int)lines->size() - screenRows + 2);
    if(*offsetY > maxOffset)
        *offsetY = maxOffset;
    if(*cursorY >= 0 && *cursorY < (int)lines->size())
        *cursorX = std::min(*wantedX, (int)(*lines)[*cursorY].length());
    needsFullRedraw = true;
}

void Editor::moveToScreenTop()
{
    *cursorY = *offsetY;
    if(*cursorY >= (int)lines->size())
        *cursorY = lines->size() - 1;
    moveToFirstNonBlank();
}

void Editor::moveToScreenMiddle()
{
    *cursorY = *offsetY + (screenRows - 2) / 2;
    if(*cursorY >= (int)lines->size())
        *cursorY = lines->size() - 1;
    moveToFirstNonBlank();
}

void Editor::moveToScreenBottom()
{
    *cursorY = *offsetY + screenRows - 3;
    if(*cursorY >= (int)lines->size())
        *cursorY = lines->size() - 1;
    moveToFirstNonBlank();
}

// ============================================================================
// Extended Search Commands
// ============================================================================

void Editor::performSearch(const std::string& query, bool forward)
{
    searchQuery = query;
    searchForward = forward;
    performSearch();
}

void Editor::performIncrementalSearch(const std::string& query, bool forward)
{
    searchQuery = query;
    searchForward = forward;
    findAllMatches();

    if(!searchMatches.empty())
    {
        // Find closest match
        int bestIndex = 0;
        for(int i = 0; i < (int)searchMatches.size(); i++)
        {
            const auto& match = searchMatches[i];
            if(forward)
            {
                if(match.row > savedCursorY ||
                   (match.row == savedCursorY && match.col >= savedCursorX))
                {
                    bestIndex = i;
                    break;
                }
            }
            else
            {
                if(match.row < savedCursorY ||
                   (match.row == savedCursorY && match.col <= savedCursorX))
                {
                    bestIndex = i;
                }
            }
        }
        jumpToMatch(bestIndex);
    }
    needsFullRedraw = true;
}

void Editor::searchWordUnderCursor(bool forward)
{
    std::string word = getSymbolUnderCursor();
    if(!word.empty())
    {
        searchQuery = word;
        searchForward = forward;
        savedCursorX = *cursorX;
        savedCursorY = *cursorY;
        performSearch();
    }
}

void Editor::addSearchToHistory(const std::string& query)
{
    if(query.empty())
        return;

    auto it = std::find(searchHistory.begin(), searchHistory.end(), query);
    if(it != searchHistory.end())
    {
        searchHistory.erase(it);
    }

    searchHistory.push_back(query);
    searchHistoryIndex = -1;

    while(searchHistory.size() > 100)
    {
        searchHistory.erase(searchHistory.begin());
    }
}

std::string Editor::getPreviousSearch()
{
    if(searchHistory.empty())
        return "";

    if(searchHistoryIndex < 0)
    {
        searchHistoryIndex = searchHistory.size() - 1;
    }
    else if(searchHistoryIndex > 0)
    {
        searchHistoryIndex--;
    }
    return searchHistory[searchHistoryIndex];
}

std::string Editor::getNextSearch()
{
    if(searchHistory.empty() || searchHistoryIndex < 0)
        return "";

    if(searchHistoryIndex < (int)searchHistory.size() - 1)
    {
        searchHistoryIndex++;
        return searchHistory[searchHistoryIndex];
    }
    return "";
}

// ============================================================================
// Extended Editing Commands
// ============================================================================

void Editor::insertTab()
{
    for(int i = 0; i < 4; i++)
    {
        insertChar(' ');
    }
}

void Editor::toggleCase()
{
    if(*cursorY >= (int)lines->size())
        return;
    std::string& line = (*lines)[*cursorY];
    if(*cursorX >= (int)line.length())
        return;

    char c = line[*cursorX];
    if(std::isupper(c))
        line[*cursorX] = std::tolower(c);
    else if(std::islower(c))
        line[*cursorX] = std::toupper(c);

    if(*cursorX < (int)line.length() - 1)
        (*cursorX)++;
    *dirty = true;
    saveState();
}

void Editor::joinLines()
{
    if(*cursorY >= (int)lines->size() - 1)
        return;

    std::string& currentLine = (*lines)[*cursorY];
    const std::string& nextLine = (*lines)[*cursorY + 1];

    while(!currentLine.empty() && std::isspace(currentLine.back()))
    {
        currentLine.pop_back();
    }

    int joinPos = currentLine.length();

    if(!currentLine.empty() && !nextLine.empty())
    {
        currentLine += ' ';
        joinPos++;
    }

    size_t start = 0;
    while(start < nextLine.length() && std::isspace(nextLine[start]))
    {
        start++;
    }

    currentLine += nextLine.substr(start);
    lines->erase(lines->begin() + *cursorY + 1);

    *cursorX = joinPos;
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

void Editor::insertLineAbove()
{
    lines->insert(lines->begin() + *cursorY, "");
    *cursorX = 0;
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

void Editor::insertLineBelow()
{
    if(*cursorY >= (int)lines->size())
    {
        lines->push_back("");
    }
    else
    {
        lines->insert(lines->begin() + *cursorY + 1, "");
    }
    (*cursorY)++;
    *cursorX = 0;
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

void Editor::deleteCurrentLine()
{
    if(lines->empty())
        return;

    yankLine();
    lines->erase(lines->begin() + *cursorY);

    if(lines->empty())
    {
        lines->push_back("");
    }
    if(*cursorY >= (int)lines->size())
    {
        *cursorY = lines->size() - 1;
    }
    *cursorX = 0;
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

void Editor::deleteToLineStart()
{
    if(*cursorY >= (int)lines->size())
        return;

    std::string& line = (*lines)[*cursorY];
    if(*cursorX > 0 && *cursorX <= (int)line.length())
    {
        line.erase(0, *cursorX);
        *cursorX = 0;
        *dirty = true;
    }
}

void Editor::deleteCharAtCursor()
{
    deleteCharForward();
    saveState();
}

void Editor::deleteCharBeforeCursor()
{
    if(*cursorX > 0)
    {
        deleteChar();
        saveState();
    }
}

void Editor::deleteWordBackward()
{
    if(*cursorY >= (int)lines->size())
        return;
    std::string& line = (*lines)[*cursorY];

    if(*cursorX == 0)
        return;

    int start = *cursorX;

    while(*cursorX > 0 && std::isspace(line[*cursorX - 1]))
    {
        (*cursorX)--;
    }
    while(*cursorX > 0 && !std::isspace(line[*cursorX - 1]))
    {
        (*cursorX)--;
    }

    line.erase(*cursorX, start - *cursorX);
    *dirty = true;
}

void Editor::deleteWord()
{
    if(*cursorY >= (int)lines->size())
        return;
    std::string& line = (*lines)[*cursorY];

    if(line.empty())
        return;

    int start = *cursorX;
    int end = *cursorX;

    if(end >= (int)line.length())
        return;

    // Helper lambda to check if char is a word character (alphanumeric or
    // underscore)
    auto isWordChar = [](char c)
    { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };

    char startChar = line[end];

    if(std::isspace(static_cast<unsigned char>(startChar)))
    {
        // On whitespace: delete whitespace, then the next word/punctuation
        while(end < (int)line.length() &&
              std::isspace(static_cast<unsigned char>(line[end])))
            end++;

        // Now delete the word or punctuation sequence
        if(end < (int)line.length())
        {
            if(isWordChar(line[end]))
            {
                while(end < (int)line.length() && isWordChar(line[end]))
                    end++;
            }
            else
            {
                // Punctuation sequence
                while(end < (int)line.length() && !isWordChar(line[end]) &&
                      !std::isspace(static_cast<unsigned char>(line[end])))
                    end++;
            }
        }
    }
    else if(isWordChar(startChar))
    {
        // On a word character: delete word + trailing whitespace
        while(end < (int)line.length() && isWordChar(line[end]))
            end++;

        // Include trailing whitespace
        while(end < (int)line.length() &&
              std::isspace(static_cast<unsigned char>(line[end])))
            end++;
    }
    else
    {
        // On punctuation: delete punctuation sequence + trailing whitespace
        while(end < (int)line.length() && !isWordChar(line[end]) &&
              !std::isspace(static_cast<unsigned char>(line[end])))
            end++;

        // Include trailing whitespace
        while(end < (int)line.length() &&
              std::isspace(static_cast<unsigned char>(line[end])))
            end++;
    }

    if(end > start)
    {
        // Yank before deleting
        yankBuffer = line.substr(start, end - start);
        line.erase(start, end - start);

        // Adjust cursor if past end of line
        if(*cursorX >= (int)line.length() && !line.empty())
        {
            *cursorX = line.length() - 1;
        }
        *dirty = true;
    }
}

void Editor::yankWord()
{
    if(*cursorY >= (int)lines->size())
        return;
    const std::string& line = (*lines)[*cursorY];

    if(*cursorX >= (int)line.length())
        return;

    int start = *cursorX;
    int end = *cursorX;

    // Get current word characters
    while(end < (int)line.length() && !std::isspace(line[end]))
    {
        end++;
    }
    // Include trailing whitespace
    while(end < (int)line.length() && std::isspace(line[end]))
    {
        end++;
    }

    yankBuffer = line.substr(start, end - start);
    setStatusMessage("Yanked: " + std::to_string(end - start) + " chars");
}

void Editor::handleBackspace()
{
    deleteChar();
}

void Editor::replaceCharAtCursor(char c)
{
    if(*cursorY >= (int)lines->size())
        return;
    std::string& line = (*lines)[*cursorY];
    if(*cursorX >= (int)line.length())
        return;

    line[*cursorX] = c;
    *dirty = true;
    saveState();
}

void Editor::repeatLastChange()
{
    setStatusMessage("Repeat not yet implemented");
}

void Editor::insertUtf8Char(int c)
{
    if(c < 128)
    {
        insertChar((char)c);
    }
    else
    {
        char buf[5] = {0};
        if(c < 0x800)
        {
            buf[0] = 0xC0 | (c >> 6);
            buf[1] = 0x80 | (c & 0x3F);
        }
        else if(c < 0x10000)
        {
            buf[0] = 0xE0 | (c >> 12);
            buf[1] = 0x80 | ((c >> 6) & 0x3F);
            buf[2] = 0x80 | (c & 0x3F);
        }
        else
        {
            buf[0] = 0xF0 | (c >> 18);
            buf[1] = 0x80 | ((c >> 12) & 0x3F);
            buf[2] = 0x80 | ((c >> 6) & 0x3F);
            buf[3] = 0x80 | (c & 0x3F);
        }
        for(int i = 0; buf[i]; i++)
        {
            insertChar(buf[i]);
        }
    }
}

void Editor::indentCurrentLine()
{
    if(*cursorY >= (int)lines->size())
        return;
    (*lines)[*cursorY] = "    " + (*lines)[*cursorY];
    *cursorX += 4;
    *dirty = true;
}

void Editor::dedentCurrentLine()
{
    if(*cursorY >= (int)lines->size())
        return;
    std::string& line = (*lines)[*cursorY];

    int remove = 0;
    while(remove < 4 && remove < (int)line.length() &&
          (line[remove] == ' ' || line[remove] == '\t'))
    {
        remove++;
    }

    if(remove > 0)
    {
        line.erase(0, remove);
        *cursorX = std::max(0, *cursorX - remove);
        *dirty = true;
    }
}

void Editor::handleLinewiseOperator(char op, int count)
{
    switch(op)
    {
    case 'd':
        for(int i = 0; i < count && !lines->empty(); i++)
        {
            deleteCurrentLine();
        }
        break;
    case 'y':
    {
        yankBuffer.clear();
        int endLine = std::min(*cursorY + count, (int)lines->size());
        for(int y = *cursorY; y < endLine; y++)
        {
            yankBuffer += (*lines)[y] + "\n";
        }
        setStatusMessage(std::to_string(count) + " lines yanked");
    }
    break;
    case '>':
        for(int i = 0; i < count && *cursorY + i < (int)lines->size(); i++)
        {
            (*lines)[*cursorY + i] = "    " + (*lines)[*cursorY + i];
        }
        *dirty = true;
        saveState();
        needsFullRedraw = true;
        break;
    case '<':
        for(int i = 0; i < count && *cursorY + i < (int)lines->size(); i++)
        {
            std::string& line = (*lines)[*cursorY + i];
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
        break;
    case '=':
        for(int i = 0; i < count && *cursorY + i < (int)lines->size(); i++)
        {
            autoIndentLine(*cursorY + i);
        }
        *dirty = true;
        saveState();
        needsFullRedraw = true;
        break;
    case 'c':
        for(int i = 0; i < count && !lines->empty(); i++)
        {
            deleteCurrentLine();
        }
        insertLineAbove();
        setMode(INSERT);
        break;
    }
}

// ============================================================================
// Extended Visual Mode Commands
// ============================================================================

void Editor::setVisualRange()
{
    currentBuffer->visualEndX = *cursorX;
    currentBuffer->visualEndY = *cursorY;
}

void Editor::swapVisualEnds()
{
    std::swap(*cursorX, currentBuffer->visualStartX);
    std::swap(*cursorY, currentBuffer->visualStartY);
    currentBuffer->visualEndX = *cursorX;
    currentBuffer->visualEndY = *cursorY;
    adjustViewport();
}

void Editor::swapVisualBlockCorner()
{
    std::swap(*cursorX, currentBuffer->visualBlockStartX);
    std::swap(*cursorY, currentBuffer->visualBlockStartY);
    currentBuffer->visualBlockEndX = *cursorX;
    currentBuffer->visualBlockEndY = *cursorY;
    adjustViewport();
}

void Editor::prepareBlockInsert(bool atEnd)
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

void Editor::indentSelection()
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

void Editor::dedentSelection()
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

void Editor::autoIndentSelection()
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

void Editor::lowercaseSelection()
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

void Editor::uppercaseSelection()
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

void Editor::toggleCaseSelection()
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

void Editor::yankLineSelection()
{
    int startY =
        std::min(currentBuffer->visualStartY, currentBuffer->visualEndY);
    int endY = std::max(currentBuffer->visualStartY, currentBuffer->visualEndY);

    yankBuffer.clear();
    for(int y = startY; y <= endY && y < (int)lines->size(); y++)
    {
        yankBuffer += (*lines)[y] + "\n";
    }
    setStatusMessage(std::to_string(endY - startY + 1) + " lines yanked");
}

void Editor::deleteLineSelection()
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
        }
    }

    if(lines->empty())
        lines->push_back("");

    *cursorY = std::min(startY, (int)lines->size() - 1);
    *cursorX = 0;
    *dirty = true;
    saveState();
    setMode(NORMAL);
    needsFullRedraw = true;
}

void Editor::indentLineSelection()
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

void Editor::dedentLineSelection()
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

void Editor::autoIndentLineSelection()
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

void Editor::setMark(char mark)
{
    if(mark >= 'a' && mark <= 'z')
    {
        MarkLocation loc;
        loc.filename = *filename;
        loc.line = *cursorY;
        loc.col = *cursorX;
        marks[mark] = loc;
        setStatusMessage(std::string("Mark '") + mark + "' set");
    }
}

void Editor::jumpToMark(char mark)
{
    if(mark >= 'a' && mark <= 'z')
    {
        auto it = marks.find(mark);
        if(it != marks.end())
        {
            pushJumpLocation();
            *cursorY = it->second.line;
            *cursorX = it->second.col;
            if(*cursorY >= (int)lines->size())
                *cursorY = lines->size() - 1;
            if(*cursorY >= 0 && *cursorX > (int)(*lines)[*cursorY].length())
                *cursorX = (*lines)[*cursorY].length();
            adjustViewport();
        }
        else
        {
            setStatusMessage(std::string("Mark '") + mark + "' not set");
        }
    }
}

// ============================================================================
// Misc Utilities
// ============================================================================

void Editor::goToFile()
{
    std::string word = getSymbolUnderCursor();
    if(!word.empty())
    {
        if(fileExists(word))
        {
            openFile(word);
        }
        else
        {
            setStatusMessage("File not found: " + word);
        }
    }
}

void Editor::showFileInfo()
{
    std::string info =
        "\"" + (filename->empty() ? "[No Name]" : *filename) + "\"";
    info += " " + std::to_string(lines->size()) + " lines";
    if(*dirty)
        info += " [Modified]";
    info += " -- " + std::to_string(*cursorY + 1) + "/" +
            std::to_string(lines->size());
    info +=
        " -- " +
        std::to_string((*cursorY + 1) * 100 / std::max(1, (int)lines->size())) +
        "%";
    setStatusMessage(info);
}

void Editor::forceFullRedraw()
{
    needsFullRedraw = true;
}

void Editor::executeOneNormalCommand(int key)
{
    switch(key)
    {
    case 'w':
        moveWordForward();
        break;
    case 'b':
        moveWordBackward();
        break;
    case 'e':
        moveToEndOfWord();
        break;
    case '0':
        moveToLineStart();
        break;
    case '$':
        moveToLineEnd();
        break;
    case 'h':
        moveLeft();
        break;
    case 'l':
        moveRight();
        break;
    case 'j':
        moveDown();
        adjustViewport();
        break;
    case 'k':
        moveUp();
        adjustViewport();
        break;
    default:
        break;
    }
}

// ============================================================================
// Command History
// ============================================================================

void Editor::commandHistoryUp()
{
    if(commandHistory.empty())
        return;
    if(commandHistoryIndex < 0)
    {
        commandHistoryIndex = commandHistory.size() - 1;
    }
    else if(commandHistoryIndex > 0)
    {
        commandHistoryIndex--;
    }
    commandInput = commandHistory[commandHistoryIndex];
}

void Editor::commandHistoryDown()
{
    if(commandHistory.empty() || commandHistoryIndex < 0)
        return;
    if(commandHistoryIndex < (int)commandHistory.size() - 1)
    {
        commandHistoryIndex++;
        commandInput = commandHistory[commandHistoryIndex];
    }
    else
    {
        commandHistoryIndex = -1;
        commandInput.clear();
    }
}

std::vector<std::string>
Editor::getCommandCompletions(const std::string& prefix)
{
    std::vector<std::string> commands = {
        "w",    "write",    "q",   "quit",    "wq",   "x",
        "e",    "edit",     "new", "vnew",    "bn",   "bnext",
        "bp",   "bprev",    "bd",  "bdelete", "ls",   "buffers",
        "sp",   "split",    "vs",  "vsplit",  "only", "tabnew",
        "tabc", "tabclose", "set", "syntax",  "noh",  "nohlsearch"};

    std::vector<std::string> matches;
    for(const auto& cmd : commands)
    {
        if(cmd.find(prefix) == 0)
        {
            matches.push_back(cmd);
        }
    }
    return matches;
}

std::vector<std::string> Editor::getPathCompletions(const std::string& path)
{
    std::vector<std::string> completions;

    std::string dir = path;
    std::string prefix;

    size_t lastSlash = path.find_last_of('/');
    if(lastSlash != std::string::npos)
    {
        dir = path.substr(0, lastSlash + 1);
        prefix = path.substr(lastSlash + 1);
    }
    else
    {
        dir = ".";
        prefix = path;
    }

    DIR* d = opendir(dir.c_str());
    if(d)
    {
        struct dirent* entry;
        while((entry = readdir(d)) != nullptr)
        {
            std::string name = entry->d_name;
            if(name[0] == '.' && prefix.empty())
                continue;
            if(name.find(prefix) == 0)
            {
                std::string fullPath = (dir == ".") ? name : dir + name;
                if(entry->d_type == DT_DIR)
                {
                    fullPath += "/";
                }
                completions.push_back(fullPath);
            }
        }
        closedir(d);
    }

    std::sort(completions.begin(), completions.end());
    return completions;
}

// ============================================================================
// File Browser Helpers
// ============================================================================

void Editor::fileBrowserUp()
{
    if(browserCursor > 0)
    {
        browserCursor--;
        if(browserCursor < browserOffset)
            browserOffset = browserCursor;
    }
}

void Editor::fileBrowserDown()
{
    if(browserCursor < (int)fileList.size() - 1)
    {
        browserCursor++;
        int visible = screenRows - 4;
        if(browserCursor >= browserOffset + visible)
            browserOffset = browserCursor - visible + 1;
    }
}

void Editor::fileBrowserStart()
{
    browserCursor = 0;
    browserOffset = 0;
}

void Editor::fileBrowserEnd()
{
    browserCursor = fileList.size() - 1;
    int visible = screenRows - 4;
    if(browserCursor >= visible)
        browserOffset = browserCursor - visible + 1;
}

void Editor::fileBrowserHalfPageUp()
{
    int half = (screenRows - 4) / 2;
    browserCursor -= half;
    if(browserCursor < 0)
        browserCursor = 0;
    if(browserCursor < browserOffset)
        browserOffset = browserCursor;
}

void Editor::fileBrowserHalfPageDown()
{
    int half = (screenRows - 4) / 2;
    browserCursor += half;
    if(browserCursor >= (int)fileList.size())
        browserCursor = fileList.size() - 1;
    int visible = screenRows - 4;
    if(browserCursor >= browserOffset + visible)
        browserOffset = browserCursor - visible + 1;
}

void Editor::fileBrowserParent()
{
    size_t lastSlash = currentDirectory.find_last_of('/');
    if(lastSlash != std::string::npos && lastSlash > 0)
    {
        std::string parent = currentDirectory.substr(0, lastSlash);
        loadDirectory(parent);
        browserCursor = 0;
        browserOffset = 0;
    }
}

bool Editor::selectFileBrowserEntry()
{
    if(browserCursor >= 0 && browserCursor < (int)fileList.size())
    {
        navigateTo(fileList[browserCursor]);
        return true;
    }
    return false;
}

void Editor::toggleHiddenFiles()
{
    showHidden = !showHidden;
    loadDirectory(currentDirectory);
    browserCursor = 0;
    browserOffset = 0;
}

void Editor::refreshFileBrowser()
{
    loadDirectory(currentDirectory);
}

void Editor::deleteFilePrompt()
{
    setStatusMessage("File deletion not yet implemented");
}

void Editor::renameFilePrompt()
{
    setStatusMessage("File rename not yet implemented");
}

void Editor::createNewFilePrompt()
{
    setStatusMessage("New file creation not yet implemented");
}

void Editor::createNewDirectoryPrompt()
{
    setStatusMessage("New directory creation not yet implemented");
}

// ============================================================================
// Fuzzy Finder Helpers
// ============================================================================

void Editor::fuzzyFindUp()
{
    if(fuzzyCursor > 0)
    {
        fuzzyCursor--;
        if(fuzzyCursor < fuzzyOffset)
            fuzzyOffset = fuzzyCursor;
    }
}

void Editor::fuzzyFindDown()
{
    if(fuzzyCursor < (int)fuzzyMatches.size() - 1)
    {
        fuzzyCursor++;
        int visible = screenRows - 4;
        if(fuzzyCursor >= fuzzyOffset + visible)
            fuzzyOffset = fuzzyCursor - visible + 1;
    }
}

void Editor::fuzzyFindHalfPageUp()
{
    int half = (screenRows - 4) / 2;
    fuzzyCursor -= half;
    if(fuzzyCursor < 0)
        fuzzyCursor = 0;
    if(fuzzyCursor < fuzzyOffset)
        fuzzyOffset = fuzzyCursor;
}

void Editor::fuzzyFindHalfPageDown()
{
    int half = (screenRows - 4) / 2;
    fuzzyCursor += half;
    if(fuzzyCursor >= (int)fuzzyMatches.size())
        fuzzyCursor = fuzzyMatches.size() - 1;
    int visible = screenRows - 4;
    if(fuzzyCursor >= fuzzyOffset + visible)
        fuzzyOffset = fuzzyCursor - visible + 1;
}

void Editor::fuzzyFindAddChar(char c)
{
    fuzzyQuery += c;
    updateFuzzyMatches();
    fuzzyCursor = 0;
    fuzzyOffset = 0;
}

void Editor::fuzzyFindBackspace()
{
    if(!fuzzyQuery.empty())
    {
        fuzzyQuery.pop_back();
        updateFuzzyMatches();
        fuzzyCursor = 0;
        fuzzyOffset = 0;
    }
}

void Editor::fuzzyFindDeleteWord()
{
    while(!fuzzyQuery.empty() && fuzzyQuery.back() == ' ')
        fuzzyQuery.pop_back();
    while(!fuzzyQuery.empty() && fuzzyQuery.back() != ' ')
        fuzzyQuery.pop_back();
    updateFuzzyMatches();
    fuzzyCursor = 0;
    fuzzyOffset = 0;
}

void Editor::fuzzyFindClear()
{
    fuzzyQuery.clear();
    updateFuzzyMatches();
    fuzzyCursor = 0;
    fuzzyOffset = 0;
}

bool Editor::selectFuzzyFindEntry()
{
    selectFuzzyMatch();
    return true;
}

void Editor::toggleFuzzyPreview()
{
    // TODO: Implement fuzzy preview toggle
}

// ============================================================================
// Buffer Browser Helpers
// ============================================================================

void Editor::bufferBrowserUp()
{
    if(bufferCursor > 0)
    {
        bufferCursor--;
        if(bufferCursor < bufferOffset)
            bufferOffset = bufferCursor;
    }
}

void Editor::bufferBrowserDown()
{
    if(bufferCursor < (int)bufferMatches.size() - 1)
    {
        bufferCursor++;
        int visible = screenRows - 4;
        if(bufferCursor >= bufferOffset + visible)
            bufferOffset = bufferCursor - visible + 1;
    }
}

void Editor::bufferBrowserStart()
{
    bufferCursor = 0;
    bufferOffset = 0;
}

void Editor::bufferBrowserEnd()
{
    bufferCursor = bufferMatches.size() - 1;
    int visible = screenRows - 4;
    if(bufferCursor >= visible)
        bufferOffset = bufferCursor - visible + 1;
}

bool Editor::selectBufferBrowserEntry()
{
    selectBufferMatch();
    return true;
}

void Editor::deleteSelectedBuffer()
{
    if(bufferCursor >= 0 && bufferCursor < (int)bufferMatches.size())
    {
        int idx = bufferMatches[bufferCursor].bufferIndex;
        if(idx != currentBufferIndex && idx >= 0 && idx < (int)buffers.size())
        {
            buffers.erase(buffers.begin() + idx);
            if(currentBufferIndex > idx)
                currentBufferIndex--;
            updateCurrentBufferPointers();
            updateBufferMatches();
        }
    }
}

bool Editor::switchToBufferByNumber(int num)
{
    if(num >= 1 && num <= (int)buffers.size())
    {
        switchToBuffer(num - 1);
        setMode(NORMAL);
        return true;
    }
    return false;
}

// ============================================================================
// Grep Search Helpers
// ============================================================================

void Editor::grepResultUp()
{
    if(grepCursor > 0)
    {
        grepCursor--;
        if(grepCursor < grepOffset)
            grepOffset = grepCursor;
    }
}

void Editor::grepResultDown()
{
    if(grepCursor < (int)grepMatches.size() - 1)
    {
        grepCursor++;
        int visible = screenRows - 4;
        if(grepCursor >= grepOffset + visible)
            grepOffset = grepCursor - visible + 1;
    }
}

void Editor::grepResultHalfPageUp()
{
    int half = (screenRows - 4) / 2;
    grepCursor -= half;
    if(grepCursor < 0)
        grepCursor = 0;
    if(grepCursor < grepOffset)
        grepOffset = grepCursor;
}

void Editor::grepResultHalfPageDown()
{
    int half = (screenRows - 4) / 2;
    grepCursor += half;
    if(grepCursor >= (int)grepMatches.size())
        grepCursor = grepMatches.size() - 1;
    int visible = screenRows - 4;
    if(grepCursor >= grepOffset + visible)
        grepOffset = grepCursor - visible + 1;
}

void Editor::grepSearchAddChar(char c)
{
    grepQuery += c;
    performGrepSearch();
}

void Editor::grepSearchBackspace()
{
    if(!grepQuery.empty())
    {
        grepQuery.pop_back();
        performGrepSearch();
    }
}

void Editor::grepSearchDeleteWord()
{
    while(!grepQuery.empty() && grepQuery.back() == ' ')
        grepQuery.pop_back();
    while(!grepQuery.empty() && grepQuery.back() != ' ')
        grepQuery.pop_back();
    performGrepSearch();
}

void Editor::grepSearchClear()
{
    grepQuery.clear();
    grepMatches.clear();
    grepCursor = 0;
    grepOffset = 0;
}

bool Editor::selectGrepResult()
{
    selectGrepMatch();
    return true;
}

void Editor::toggleGrepPreview()
{
    // TODO: Implement grep preview toggle
}

// ============================================================================
// Mode Handlers (legacy - called by handleKeypress)
// ============================================================================

void Editor::handleInsertMode(int c)
{
    // Handle completion navigation first if active
    if(completionActive)
    {
        if(c == 14 || c == Terminal::ARROW_DOWN) // 14 is Ctrl-N
        {
            nextCompletion();
            return;
        }
        if(c == 16 || c == Terminal::ARROW_UP) // 16 is Ctrl-P
        {
            previousCompletion();
            return;
        }
        if(c == Terminal::TAB || c == Terminal::ENTER)
        {
            acceptCompletion();
            return;
        }
        if(c == Terminal::ESC || c == Terminal::CTRL_C)
        {
            cancelCompletion();
            setMode(NORMAL);
            if(*cursorX > 0)
                (*cursorX)--;
            saveState();
            return;
        }
    }

    if(c == Terminal::ESC || c == Terminal::CTRL_C)
    {
        // Exit insert mode
        if(*cursorX > 0)
            (*cursorX)--;
        saveState();
        setMode(NORMAL);
        return;
    }

    // Trigger completion with Ctrl-N
    if(c == 14) // 14 is Ctrl-N
    {
        if(!completionActive)
        {
            triggerCompletion();
        }
        else
        {
            nextCompletion();
        }
        return;
    }

    // Previous completion with Ctrl-P
    if(c == 16) // 16 is Ctrl-P
    {
        if(completionActive)
        {
            previousCompletion();
        }
        return;
    }

    if(c == Terminal::BACKSPACE || c == 127 || c == 8)
    {
        if(*cursorX > 0)
        {
            (*cursorX)--;
            if(*cursorY < (int)lines->size())
            {
                (*lines)[*cursorY].erase(*cursorX, 1);
            }
            *dirty = true;

            // Update completion filter if active
            if(completionActive)
            {
                rebuildCompletionFilter();
            }
        }
        else if(*cursorY > 0)
        {
            // Join with previous line
            int prevLen = (*lines)[*cursorY - 1].length();
            (*lines)[*cursorY - 1] += (*lines)[*cursorY];
            lines->erase(lines->begin() + *cursorY);
            (*cursorY)--;
            *cursorX = prevLen;
            *dirty = true;

            // Cancel completion if joining lines
            if(completionActive)
            {
                cancelCompletion();
            }
        }
        return;
    }

    if(c == Terminal::ENTER)
    {
        // If completion is active, accept it
        if(completionActive)
        {
            acceptCompletion();
        }
        else
        {
            insertNewline();
        }
        return;
    }

    if(c == Terminal::TAB)
    {
        // If completion is active, accept it
        if(completionActive)
        {
            acceptCompletion();
        }
        else
        {
            insertTab();
        }
        return;
    }

    if(c == Terminal::ARROW_LEFT)
    {
        if(*cursorX > 0)
            (*cursorX)--;

        // Cancel completion if moving cursor
        if(completionActive)
        {
            cancelCompletion();
        }
        return;
    }
    if(c == Terminal::ARROW_RIGHT)
    {
        if(*cursorY < (int)lines->size() &&
           *cursorX < (int)(*lines)[*cursorY].length())
            (*cursorX)++;

        // Cancel completion if moving cursor
        if(completionActive)
        {
            cancelCompletion();
        }
        return;
    }
    if(c == Terminal::ARROW_UP)
    {
        // If completion is active, use for navigation
        if(completionActive)
        {
            previousCompletion();
        }
        else if(*cursorY > 0)
        {
            (*cursorY)--;
            if(*cursorY < (int)lines->size() &&
               *cursorX > (int)(*lines)[*cursorY].length())
                *cursorX = (*lines)[*cursorY].length();
        }
        return;
    }
    if(c == Terminal::ARROW_DOWN)
    {
        // If completion is active, use for navigation
        if(completionActive)
        {
            nextCompletion();
        }
        else if(*cursorY < (int)lines->size() - 1)
        {
            (*cursorY)++;
            if(*cursorX > (int)(*lines)[*cursorY].length())
                *cursorX = (*lines)[*cursorY].length();
        }
        return;
    }

    if(c == Terminal::CTRL_W)
    {
        deleteWordBackward();

        // Update or cancel completion
        if(completionActive)
        {
            rebuildCompletionFilter();
        }
        return;
    }

    if(c == Terminal::CTRL_U)
    {
        deleteToLineStart();

        // Cancel completion if deleting to line start
        if(completionActive)
        {
            cancelCompletion();
        }
        return;
    }

    // Regular character
    if(c >= 32 && c < 127)
    {
        insertChar((char)c);

        // Update completion filter if active
        if(completionActive)
        {
            rebuildCompletionFilter();
        }
        // Auto-trigger completion after typing '.' or '::' or '->'
        else if(c == '.')
        {
            // Automatically trigger completion after dot
            triggerCompletion();
        }
        else if(c == ':' && *cursorX >= 2 &&
                (*lines)[*cursorY][*cursorX - 2] == ':')
        {
            // Trigger after ::
            triggerCompletion();
        }
        else if(c == '>' && *cursorX >= 2 &&
                (*lines)[*cursorY][*cursorX - 2] == '-')
        {
            // Trigger after ->
            triggerCompletion();
        }
    }
    else if(c >= 128)
    {
        insertUtf8Char(c);

        // Update completion filter if active
        if(completionActive)
        {
            rebuildCompletionFilter();
        }
    }
}

void Editor::handleVisualMode(int c)
{
    // ----- Leader (space) in visual modes -----
    if(commandBuffer == " ")
    {
        if(c == 'f')
        {
            commandBuffer.clear();
            repeatCount = 0;
            clangFormatVisualSelection();
            setMode(NORMAL);
            return;
        }
        if(c == ' ')
        {
            commandBuffer.clear();
            setStatusMessage("");
            repeatCount = 0;
            return;
        }

        // Unknown leader command: cancel leader and continue handling `c`.
        commandBuffer.clear();
        setStatusMessage("");
        repeatCount = 0;
    }

    if(c == ' ')
    {
        commandBuffer = " ";
        setStatusMessage("Leader");
        repeatCount = 0;
        return;
    }
    if(c == Terminal::ESC || c == 'v')
    {
        setMode(NORMAL);
        return;
    }

    if(c == 'V')
    {
        setMode(VISUAL_LINE);
        return;
    }

    if(c == Terminal::CTRL_V)
    {
        setMode(VISUAL_BLOCK);
        return;
    }

    // Movement updates selection
    switch(c)
    {
    case 'h':
    case Terminal::ARROW_LEFT:
        moveLeft(1);
        break;
    case 'j':
    case Terminal::ARROW_DOWN:
        moveDown(1);
        break;
    case 'k':
    case Terminal::ARROW_UP:
        moveUp(1);
        break;
    case 'l':
    case Terminal::ARROW_RIGHT:
        moveRight(1);
        break;

    // Word movements
    case 'w':
        moveWordForward();
        break;
    case 'W':
        moveWordForwardBig();
        break;
    case 'b':
        moveWordBackward();
        break;
    case 'B':
        moveWordBackwardBig();
        break;
    case 'e':
        moveToEndOfWord();
        break;
    case 'E':
        moveToEndOfWordBig();
        break;

    // Line movements
    case '0':
        moveToLineStart();
        break;
    case '^':
        moveToFirstNonBlank();
        break;
    case '$':
        moveToLineEnd();
        break;

    // Paragraph movements
    case '{':
        moveParagraphBackward();
        break;
    case '}':
        moveParagraphForward();
        break;

    // File movements
    case 'G':
        moveToLastLine();
        break;
    case 'g':
    {
        int nextChar = Terminal::readKey();
        if(nextChar == 'g')
            moveToFirstLine();
    }
    break;

    // Matching bracket
    case '%':
        moveToMatchingBracket();
        break;

    // Screen movements
    case 'H':
        moveToScreenTop();
        break;
    case 'M':
        moveToScreenMiddle();
        break;
    case 'L':
        moveToScreenBottom();
        break;

    // Scrolling
    case Terminal::CTRL_D:
        scrollHalfPageDown(false);
        break;
    case Terminal::CTRL_U:
        scrollHalfPageUp(false);
        break;
    case Terminal::CTRL_F:
    case Terminal::PAGE_DOWN:
        scrollPageDown();
        break;
    case Terminal::CTRL_B:
    case Terminal::PAGE_UP:
        scrollPageUp();
        break;

    // Operations on selection
    case 'd':
    case 'x':
        yankSelection();
        deleteSelection();
        saveState();
        setMode(NORMAL);
        return;
    case 'y':
        yankSelection();
        setMode(NORMAL);
        return;
    case 'c':
        yankSelection();
        deleteSelection();
        saveState();
        setMode(INSERT);
        return;
    case '>':
        indentSelection();
        saveState();
        setMode(NORMAL);
        return;
    case '<':
        dedentSelection();
        saveState();
        setMode(NORMAL);
        return;
    case '~':
        toggleCaseSelection();
        saveState();
        setMode(NORMAL);
        return;
    case 'u':
        lowercaseSelection();
        saveState();
        setMode(NORMAL);
        return;
    case 'U':
        uppercaseSelection();
        saveState();
        setMode(NORMAL);
        return;
    case 'J':
        // Join selected lines
        {
            int startLine = std::min(currentBuffer->visualStartY,
                                     currentBuffer->visualEndY);
            int endLine = std::max(currentBuffer->visualStartY,
                                   currentBuffer->visualEndY);
            *cursorY = startLine;
            for(int i = startLine;
                i < endLine && *cursorY < (int)lines->size() - 1; i++)
            {
                joinLines();
            }
            saveState();
            setMode(NORMAL);
        }
        return;

    // Swap selection ends
    case 'o':
    {
        std::swap(*cursorX, currentBuffer->visualStartX);
        std::swap(*cursorY, currentBuffer->visualStartY);
    }
    break;
    }

    // Update visual end
    currentBuffer->visualEndX = *cursorX;
    currentBuffer->visualEndY = *cursorY;
    needsFullRedraw = true;
}

void Editor::handleVisualBlockMode(int c)
{
    if(c == Terminal::ESC || c == Terminal::CTRL_V)
    {
        setMode(NORMAL);
        return;
    }

    // ----- Leader (space) in visual block mode -----
    if(commandBuffer == " ")
    {
        if(c == 'f')
        {
            commandBuffer.clear();
            repeatCount = 0;
            clangFormatVisualBlockSelection();
            setMode(NORMAL);
            return;
        }
        if(c == ' ')
        {
            commandBuffer.clear();
            setStatusMessage("");
            repeatCount = 0;
            return;
        }

        commandBuffer.clear();
        setStatusMessage("");
        repeatCount = 0;
    }

    if(c == ' ')
    {
        commandBuffer = " ";
        setStatusMessage("Leader");
        repeatCount = 0;
        return;
    }

    // Movement
    switch(c)
    {
    case 'h':
    case Terminal::ARROW_LEFT:
        moveLeft(1);
        break;
    case 'j':
    case Terminal::ARROW_DOWN:
        moveDown(1);
        break;
    case 'k':
    case Terminal::ARROW_UP:
        moveUp(1);
        break;
    case 'l':
    case Terminal::ARROW_RIGHT:
        moveRight(1);
        break;
    case 'd':
    case 'x':
        deleteVisualBlock();
        saveState();
        setMode(NORMAL);
        return;
    case 'y':
        yankVisualBlock();
        setMode(NORMAL);
        return;
    case 'c':
        changeVisualBlock();
        setMode(INSERT);
        return;
    }

    // Update block end
    currentBuffer->visualBlockEndX = *cursorX;
    currentBuffer->visualBlockEndY = *cursorY;
    needsFullRedraw = true;
}

void Editor::handleSearchMode(int c)
{
    if(c == Terminal::ESC)
    {
        // Cancel search, restore cursor
        *cursorX = savedCursorX;
        *cursorY = savedCursorY;
        searchQuery.clear();
        setMode(NORMAL);
        return;
    }

    if(c == Terminal::ENTER)
    {
        if(!searchQuery.empty())
        {
            addSearchToHistory(searchQuery);
            performSearch();
        }
        setMode(NORMAL);
        return;
    }

    if(c == Terminal::BACKSPACE || c == 127 || c == 8)
    {
        if(!searchQuery.empty())
        {
            searchQuery.pop_back();
            commandBuffer = (searchForward ? "/" : "?") + searchQuery;

            // Restore and re-search
            *cursorX = savedCursorX;
            *cursorY = savedCursorY;
            if(!searchQuery.empty())
            {
                findAllMatches();
            }
        }
        else
        {
            *cursorX = savedCursorX;
            *cursorY = savedCursorY;
            setMode(NORMAL);
        }
        return;
    }

    if(c >= 32 && c < 127)
    {
        searchQuery += (char)c;
        commandBuffer = (searchForward ? "/" : "?") + searchQuery;

        // Incremental search
        findAllMatches();
        if(!searchMatches.empty())
        {
            jumpToMatch(0);
        }
    }

    needsFullRedraw = true;
}

// ============================================================================
// Completion Helpers (for insert mode)
// ============================================================================

bool Editor::shouldTriggerCompletion()
{
    // Check if we should auto-trigger completion
    // Typically after typing an identifier character
    if(*cursorY >= (int)lines->size())
        return false;
    const std::string& line = (*lines)[*cursorY];
    if(*cursorX == 0)
        return false;

    char prevChar = line[*cursorX - 1];
    return std::isalnum(prevChar) || prevChar == '_' || prevChar == '.';
}

void Editor::triggerCompletion()
{
    requestCompletion();
}

void Editor::nextCompletion()
{
    completionNext();
}

void Editor::previousCompletion()
{
    completionPrev();
}

// ============================================================================
// Compatibility Aliases
// ============================================================================

void Editor::deleteToEndOfLine()
{
    deleteToLineEnd();
}

void Editor::switchToAlternateFile()
{
    jumpToAlternateFile();
}
