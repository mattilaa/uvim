// Additional function implementations for Editor
// These functions are called by mode handlers

#include "editor_lsp_query.h"
#include "terminal.h"
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <sstream>

#include "text_utils.h"
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
// Mode Handlers
// ============================================================================

void Editor::handleFileBrowserMode(int c)
{
    if(c == Terminal::ESC || c == 'q')
    {
        setMode(NORMAL);
        return;
    }

    if(c == 'j' || c == Terminal::ARROW_DOWN)
    {
        fileBrowserDown();
    }
    else if(c == 'k' || c == Terminal::ARROW_UP)
    {
        fileBrowserUp();
    }
    else if(c == 'G')
    {
        fileBrowserEnd();
    }
    else if(c == 'g')
    {
        int next = Terminal::readKey();
        if(next == 'g')
            fileBrowserStart();
    }
    else if(c == Terminal::CTRL_D)
    {
        fileBrowserHalfPageDown();
    }
    else if(c == Terminal::CTRL_U)
    {
        fileBrowserHalfPageUp();
    }
    else if(c == Terminal::ENTER || c == 'l' || c == Terminal::ARROW_RIGHT)
    {
        if(selectFileBrowserEntry())
            setMode(NORMAL);
    }
    else if(c == 'h' || c == Terminal::ARROW_LEFT || c == '-')
    {
        fileBrowserParent();
    }
    else if(c == '.')
    {
        toggleHiddenFiles();
    }
    else if(c == 'r' || c == Terminal::CTRL_L)
    {
        refreshFileBrowser();
    }

    needsFullRedraw = true;
}

void Editor::handleFuzzyFindMode(int c)
{
    if(c == Terminal::ESC)
    {
        setMode(NORMAL);
        return;
    }

    if(c == Terminal::ENTER)
    {
        if(selectFuzzyFindEntry())
            setMode(NORMAL);
        return;
    }

    if(c == Terminal::CTRL_N || c == Terminal::ARROW_DOWN)
    {
        fuzzyFindDown();
    }
    else if(c == Terminal::CTRL_P || c == Terminal::ARROW_UP)
    {
        fuzzyFindUp();
    }
    else if(c == Terminal::CTRL_D)
    {
        fuzzyFindHalfPageDown();
    }
    else if(c == Terminal::CTRL_U)
    {
        fuzzyFindHalfPageUp();
    }
    else if(c == Terminal::BACKSPACE || c == 127 || c == 8)
    {
        fuzzyFindBackspace();
    }
    else if(c == Terminal::CTRL_W)
    {
        fuzzyFindDeleteWord();
    }
    else if(c >= 32 && c < 127)
    {
        fuzzyFindAddChar(static_cast<char>(c));
    }

    needsFullRedraw = true;
}

void Editor::handleBufferBrowserMode(int c)
{
    if(c == Terminal::ESC || c == 'q')
    {
        setMode(NORMAL);
        return;
    }

    if(c == Terminal::ENTER || c == 'l')
    {
        if(selectBufferBrowserEntry())
            setMode(NORMAL);
        return;
    }

    if(c == 'j' || c == Terminal::ARROW_DOWN || c == Terminal::CTRL_N)
    {
        bufferBrowserDown();
    }
    else if(c == 'k' || c == Terminal::ARROW_UP || c == Terminal::CTRL_P)
    {
        bufferBrowserUp();
    }
    else if(c == 'G')
    {
        bufferBrowserEnd();
    }
    else if(c == 'g')
    {
        int next = Terminal::readKey();
        if(next == 'g')
            bufferBrowserStart();
    }
    else if(c == 'd' || c == 'D')
    {
        deleteSelectedBuffer();
    }
    else if(c >= '1' && c <= '9')
    {
        int bufNum = c - '1';
        if(switchToBufferByNumber(bufNum))
            setMode(NORMAL);
    }

    needsFullRedraw = true;
}

void Editor::handleGrepSearchMode(int c)
{
    if(c == Terminal::ESC)
    {
        setMode(NORMAL);
        return;
    }

    if(c == Terminal::ENTER)
    {
        if(selectGrepResult())
            setMode(NORMAL);
        return;
    }

    if(c == Terminal::CTRL_N || c == Terminal::ARROW_DOWN)
    {
        grepResultDown();
    }
    else if(c == Terminal::CTRL_P || c == Terminal::ARROW_UP)
    {
        grepResultUp();
    }
    else if(c == Terminal::CTRL_D)
    {
        grepResultHalfPageDown();
    }
    else if(c == Terminal::CTRL_U)
    {
        grepResultHalfPageUp();
    }
    else if(c == Terminal::BACKSPACE || c == 127 || c == 8)
    {
        grepSearchBackspace();
    }
    else if(c == Terminal::CTRL_W)
    {
        grepSearchDeleteWord();
    }
    else if(c >= 32 && c < 127)
    {
        grepSearchAddChar(static_cast<char>(c));
    }

    needsFullRedraw = true;
}

// ============================================================================
// File Browser Functions
// ============================================================================

void Editor::openFileBrowser(const std::string& path)
{
    if(currentMode != FILE_BROWSER)
    {
        previousFile = *filename;
    }

    char resolvedPath[PATH_MAX];
    if(realpath(path.c_str(), resolvedPath))
    {
        currentDirectory = resolvedPath;
    }
    else
    {
        char cwd[PATH_MAX];
        if(getcwd(cwd, sizeof(cwd)))
        {
            currentDirectory = cwd;
        }
        else
        {
            currentDirectory = ".";
        }
    }

    loadDirectory(currentDirectory);

    if(fileList.empty())
    {
        setStatusMessage("Failed to load directory: " + currentDirectory);
        return;
    }

    setMode(FILE_BROWSER);
    browserCursor = 0;
    browserOffset = 0;
    needsFullRedraw = true;
}

void Editor::loadDirectory(const std::string& path)
{
    fileList.clear();

    DIR* dir = opendir(path.c_str());
    if(!dir)
    {
        dir = opendir(".");
        if(!dir)
        {
            setStatusMessage("Cannot open any directory!");
            return;
        }
        currentDirectory = ".";
    }

    std::vector<FileEntry> dirs;
    std::vector<FileEntry> files;

    struct dirent* entry;
    while((entry = readdir(dir)))
    {
        std::string name = entry->d_name;

        if(name == ".")
            continue;

        if(!showHidden && name != ".." && name[0] == '.')
            continue;

        std::string fullPath = path + "/" + name;

        struct stat st;
        if(stat(fullPath.c_str(), &st) != 0)
            continue;

        FileEntry fe;
        fe.name = name;
        fe.path = fullPath;
        fe.isDirectory = S_ISDIR(st.st_mode);
        fe.size = st.st_size;
        fe.modTime = st.st_mtime;

        if(fe.isDirectory)
        {
            dirs.push_back(fe);
        }
        else
        {
            files.push_back(fe);
        }
    }

    closedir(dir);

    // Sort
    auto sortByName = [](const FileEntry& a, const FileEntry& b)
    {
        if(a.name == "..")
            return true;
        if(b.name == "..")
            return false;
        return a.name < b.name;
    };

    std::sort(dirs.begin(), dirs.end(), sortByName);
    std::sort(files.begin(), files.end(), sortByName);

    fileList.reserve(dirs.size() + files.size());
    fileList.insert(fileList.end(), dirs.begin(), dirs.end());
    fileList.insert(fileList.end(), files.begin(), files.end());
}

void Editor::drawFileBrowser()
{
    std::string output;
    output.reserve(screenRows * screenCols * 2);

    // Header
    output += Terminal::ESC_REVERSE;
    output += " FILE BROWSER: " + currentDirectory;
    int padding = screenCols - static_cast<int>(currentDirectory.length()) - 16;
    if(padding > 0)
        output += std::string(padding, ' ');
    output += Terminal::ESC_RESET_ATTRS;
    output += "\r\n";

    int visibleRows = screenRows - 1;

    for(int row = 0; row < visibleRows; row++)
    {
        int entryIndex = browserOffset + row;

        output += "\x1b[K"; // Clear line

        if(entryIndex < static_cast<int>(fileList.size()))
        {
            const FileEntry& entry = fileList[entryIndex];

            if(entryIndex == browserCursor)
            {
                output += Terminal::ESC_REVERSE;
            }

            if(entry.isDirectory)
            {
                output += Terminal::FG_BLUE;
                output += "📁 ";
            }
            else
            {
                output += "   ";
            }

            output += entry.name;
            output += Terminal::ESC_RESET_ATTRS;
        }

        output += "\r\n";
    }

    Terminal::write(output);
}

void Editor::navigateTo(const FileEntry& entry)
{
    if(entry.isDirectory)
    {
        loadDirectory(entry.path);
        currentDirectory = entry.path;
        browserCursor = 0;
        browserOffset = 0;
    }
    else
    {
        openFile(entry.path);
        setMode(NORMAL);
    }
    needsFullRedraw = true;
}

// ============================================================================
// Fuzzy Finder Functions
// ============================================================================

void Editor::initializeFuzzyFind()
{
    fuzzyQuery.clear();
    fuzzyCursor = 0;
    fuzzyOffset = 0;

    // Collect files if not already done
    if(allProjectFiles.empty())
    {
        char cwd[PATH_MAX];
        if(getcwd(cwd, sizeof(cwd)))
        {
            collectProjectFiles(cwd, 0);
        }
    }

    updateFuzzyMatches();
    needsFullRedraw = true;
}

void Editor::updateFuzzyMatches()
{
    fuzzyMatches.clear();

    if(fuzzyQuery.empty())
    {
        for(const auto& file : allProjectFiles)
        {
            if(!file.isDirectory)
            {
                FuzzyMatch match;
                match.file = file;
                match.score = 0;
                fuzzyMatches.push_back(match);
            }
        }
    }
    else
    {
        for(const auto& file : allProjectFiles)
        {
            if(file.isDirectory)
                continue;

            std::vector<int> positions;
            int pathScore = fuzzyScore(fuzzyQuery, file.path, positions);

            std::vector<int> namePositions;
            int nameScore = fuzzyScore(fuzzyQuery, file.name, namePositions);

            int finalScore = std::max(pathScore, nameScore * 2);

            if(finalScore > 0)
            {
                FuzzyMatch match;
                match.file = file;
                match.score = finalScore;
                match.matchPositions =
                    (nameScore * 2 > pathScore) ? namePositions : positions;
                fuzzyMatches.push_back(match);
            }
        }

        std::sort(fuzzyMatches.begin(), fuzzyMatches.end(),
                  [](const FuzzyMatch& a, const FuzzyMatch& b)
                  { return a.score > b.score; });
    }

    if(fuzzyCursor >= static_cast<int>(fuzzyMatches.size()))
    {
        fuzzyCursor = 0;
        fuzzyOffset = 0;
    }
}

void Editor::drawFuzzyFind()
{
    std::string output;
    output.reserve(screenRows * screenCols * 2);

    // Query line
    output += "\x1b[1m> \x1b[0m";
    output += fuzzyQuery;
    output += "\x1b[7m \x1b[0m";
    output += "\x1b[K\r\n";

    int visibleRows = screenRows - 1;

    for(int row = 0; row < visibleRows; row++)
    {
        int matchIndex = fuzzyOffset + row;

        output += "\x1b[K";

        if(matchIndex < static_cast<int>(fuzzyMatches.size()))
        {
            const FuzzyMatch& match = fuzzyMatches[matchIndex];

            if(matchIndex == fuzzyCursor)
            {
                output += Terminal::ESC_REVERSE;
            }

            output += " " + match.file.path;
            output += Terminal::ESC_RESET_ATTRS;
        }

        output += "\r\n";
    }

    Terminal::write(output);
}

void Editor::selectFuzzyMatch()
{
    if(fuzzyMatches.empty() ||
       fuzzyCursor >= static_cast<int>(fuzzyMatches.size()))
        return;

    const FuzzyMatch& match = fuzzyMatches[fuzzyCursor];
    openFile(match.file.path);
    setMode(NORMAL);
}

// ============================================================================
// Buffer Browser Functions
// ============================================================================

void Editor::initializeBufferBrowser()
{
    bufferQuery.clear();
    bufferCursor = 0;
    bufferOffset = 0;
    updateBufferMatches();
    needsFullRedraw = true;
}

void Editor::updateBufferMatches()
{
    bufferMatches.clear();

    for(size_t i = 0; i < buffers.size(); i++)
    {
        BufferMatch match;
        match.bufferIndex = static_cast<int>(i);

        std::string name =
            buffers[i]->filename.empty() ? "[No Name]" : buffers[i]->filename;

        match.display = std::to_string(i + 1);
        if(static_cast<int>(i) == currentBufferIndex)
            match.display += " *";
        else
            match.display += "  ";

        if(buffers[i]->dirty)
            match.display += " [+] ";
        else
            match.display += "     ";

        match.display += name;
        match.score = 0;

        bufferMatches.push_back(match);
    }
}

void Editor::drawBufferBrowser()
{
    std::string output;
    output.reserve(screenRows * screenCols * 2);

    // Header
    output += Terminal::ESC_REVERSE;
    output += " BUFFERS ";
    int padding = screenCols - 9;
    if(padding > 0)
        output += std::string(padding, ' ');
    output += Terminal::ESC_RESET_ATTRS;
    output += "\r\n";

    int visibleRows = screenRows - 1;

    for(int row = 0; row < visibleRows; row++)
    {
        int matchIndex = bufferOffset + row;

        output += "\x1b[K";

        if(matchIndex < static_cast<int>(bufferMatches.size()))
        {
            const BufferMatch& match = bufferMatches[matchIndex];

            if(matchIndex == bufferCursor)
            {
                output += Terminal::ESC_REVERSE;
            }

            output += " " + match.display;
            output += Terminal::ESC_RESET_ATTRS;
        }

        output += "\r\n";
    }

    Terminal::write(output);
}

void Editor::selectBufferMatch()
{
    if(bufferMatches.empty() ||
       bufferCursor >= static_cast<int>(bufferMatches.size()))
        return;

    const BufferMatch& match = bufferMatches[bufferCursor];
    switchToBuffer(match.bufferIndex);
    setMode(NORMAL);
}

// ============================================================================
// Grep Search Functions
// ============================================================================

void Editor::initializeGrepSearch()
{
    grepQuery.clear();
    grepMatches.clear();
    grepCursor = 0;
    grepOffset = 0;
    needsFullRedraw = true;
}

void Editor::performGrepSearch()
{
    grepMatches.clear();

    if(grepQuery.length() < 2)
        return;

    // Escape query for shell
    std::string escapedQuery;
    for(char c : grepQuery)
    {
        if(c == '\'' || c == '\\' || c == '"')
            escapedQuery += '\\';
        escapedQuery += c;
    }

    std::string cmd = "rg --line-number --no-heading --color=never '";
    cmd += escapedQuery;
    cmd += "' 2>/dev/null || grep -rn '";
    cmd += escapedQuery;
    cmd += "' . 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe)
        return;

    char buffer[4096];
    while(fgets(buffer, sizeof(buffer), pipe) && grepMatches.size() < 1000)
    {
        std::string line = buffer;
        if(line.empty())
            continue;
        if(line.back() == '\n')
            line.pop_back();

        // Parse: filepath:line:content
        GrepMatch match;

        size_t firstColon = line.find(':');
        if(firstColon == std::string::npos)
            continue;

        match.filepath = line.substr(0, firstColon);

        // Extract filename from path
        size_t lastSlash = match.filepath.find_last_of("/\\");
        match.filename = (lastSlash != std::string::npos)
                             ? match.filepath.substr(lastSlash + 1)
                             : match.filepath;

        size_t secondColon = line.find(':', firstColon + 1);
        if(secondColon == std::string::npos)
            continue;

        try
        {
            match.lineNumber = std::stoi(
                line.substr(firstColon + 1, secondColon - firstColon - 1));
        }
        catch(...)
        {
            continue;
        }

        match.lineContent = line.substr(secondColon + 1);

        // Trim and limit content length
        size_t start = match.lineContent.find_first_not_of(" \t");
        if(start != std::string::npos)
        {
            match.lineContent = match.lineContent.substr(start);
        }
        if(match.lineContent.length() > 200)
        {
            match.lineContent = match.lineContent.substr(0, 197) + "...";
        }

        grepMatches.push_back(match);
    }

    pclose(pipe);
    grepCursor = 0;
    grepOffset = 0;
    needsFullRedraw = true;
}

void Editor::drawGrepSearch()
{
    std::string output;
    output.reserve(screenRows * screenCols * 2);

    // Query line
    output += "\x1b[1mgrep> \x1b[0m";
    output += grepQuery;
    output += "\x1b[7m \x1b[0m";
    output += "\x1b[K\r\n";

    int visibleRows = screenRows - 1;

    for(int row = 0; row < visibleRows; row++)
    {
        int matchIndex = grepOffset + row;

        output += "\x1b[K";

        if(matchIndex < static_cast<int>(grepMatches.size()))
        {
            const GrepMatch& match = grepMatches[matchIndex];

            if(matchIndex == grepCursor)
            {
                output += Terminal::ESC_REVERSE;
            }

            output += Terminal::FG_MAGENTA;
            output += match.filepath;
            output += Terminal::ESC_RESET_ATTRS;

            if(matchIndex == grepCursor)
                output += Terminal::ESC_REVERSE;

            output += Terminal::FG_GREEN;
            output += ":" + std::to_string(match.lineNumber);
            output += Terminal::ESC_RESET_ATTRS;

            if(matchIndex == grepCursor)
                output += Terminal::ESC_REVERSE;

            output += ": " + match.lineContent;
            output += Terminal::ESC_RESET_ATTRS;
        }

        output += "\r\n";
    }

    Terminal::write(output);
}

void Editor::selectGrepMatch()
{
    if(grepMatches.empty() ||
       grepCursor >= static_cast<int>(grepMatches.size()))
        return;

    const GrepMatch& match = grepMatches[grepCursor];
    openFile(match.filepath);
    *cursorY = match.lineNumber - 1;
    *cursorX = 0; // Go to beginning of line
    if(*cursorY < 0)
        *cursorY = 0;
    centerScreen();
    setMode(NORMAL);
}

// ============================================================================
// Project File Collection (for fuzzy finder)
// ============================================================================

void Editor::collectProjectFiles(const std::string& dir, int depth)
{
    if(depth > 5)
        return;

    DIR* d = opendir(dir.c_str());
    if(!d)
        return;

    struct dirent* entry;
    while((entry = readdir(d)))
    {
        std::string name = entry->d_name;

        if(name == "." || name == ".." || name[0] == '.')
            continue;

        if(name == "node_modules" || name == "build" || name == "dist" ||
           name == ".git" || name == "target" || name == "__pycache__")
            continue;

        std::string fullPath = dir + "/" + name;

        struct stat st;
        if(stat(fullPath.c_str(), &st) == 0)
        {
            FileEntry fileEntry;
            fileEntry.name = name;
            fileEntry.path = fullPath;
            fileEntry.isDirectory = S_ISDIR(st.st_mode);
            fileEntry.size = st.st_size;
            fileEntry.modTime = st.st_mtime;

            allProjectFiles.push_back(fileEntry);

            if(fileEntry.isDirectory)
            {
                collectProjectFiles(fullPath, depth + 1);
            }
        }
    }

    closedir(d);
}

int Editor::fuzzyScore(const std::string& needle, const std::string& haystack,
                       std::vector<int>& matchPositions)
{
    matchPositions.clear();

    if(needle.empty())
        return 0;
    if(needle.length() > haystack.length())
        return -1;

    int score = 0;
    int consecutiveBonus = 10;
    int separatorBonus = 30;
    int camelBonus = 30;
    int firstLetterBonus = 15;

    size_t needleIdx = 0;
    int prevMatchIdx = -1;

    for(size_t i = 0; i < haystack.length() && needleIdx < needle.length(); i++)
    {
        char needleChar = std::tolower(needle[needleIdx]);
        char haystackChar = std::tolower(haystack[i]);

        if(needleChar == haystackChar)
        {
            matchPositions.push_back(i);
            score += 100;

            if(prevMatchIdx >= 0 && static_cast<int>(i) == prevMatchIdx + 1)
            {
                score += consecutiveBonus;
            }

            if(i > 0)
            {
                char prevChar = haystack[i - 1];
                if(prevChar == '/' || prevChar == '-' || prevChar == '_' ||
                   prevChar == '.')
                {
                    score += separatorBonus;
                }
            }

            if(i > 0 && std::islower(haystack[i - 1]) &&
               std::isupper(haystack[i]))
            {
                score += camelBonus;
            }

            if(i == 0)
            {
                score += firstLetterBonus;
            }

            if(needle[needleIdx] == haystack[i])
            {
                score += 5;
            }

            prevMatchIdx = i;
            needleIdx++;
        }
        else
        {
            if(prevMatchIdx >= 0)
            {
                score -= (i - prevMatchIdx);
            }
        }
    }

    if(needleIdx != needle.length())
    {
        return -1;
    }

    score -= haystack.length();

    return score;
}

// ============================================================================
// Completion Functions
// ============================================================================

bool Editor::shouldTriggerCompletion()
{
    if(*cursorY >= static_cast<int>(lines->size()))
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

// ============================================================================
// Mode Handlers
// ============================================================================

void Editor::handleNormalMode(int c)
{
#ifdef UVIM_DEBUG_LOGGING
    // Debug: log every keypress
    {
        std::ofstream dbg("/tmp/uvim_debug.txt", std::ios::app);
        dbg << "handleNormalMode c=" << c << " ('" << (char)c
            << "') commandBuffer='" << commandBuffer << "'" << std::endl;
    }
#endif

    static bool pendingDelete = false;
    static bool pendingYank = false;
    static bool pendingIndent = false;
    static bool pendingShiftRight = false;
    static bool pendingShiftLeft = false;

    // ----- single-character replace (vim/neovim-style 'r{char}') -----
    if(c == 'r')
    {
        // Cancel any pending operators
        pendingDelete = pendingYank = pendingIndent = false;

        int rc = Terminal::readKey();

        // Only accept printable characters
        if(rc < 32 || rc == 127)
            return;

        if(!lines || lines->empty())
            return;

        if(*cursorY < 0 || *cursorY >= (int)lines->size())
            return;

        std::string& line = (*lines)[*cursorY];
        if(*cursorX < 0 || *cursorX >= (int)line.size())
            return;

        line[*cursorX] = (char)rc;

        saveState(); // your undo model saves *after* changes
        *dirty = true;
        needsFullRedraw =
            true; // IMPORTANT: otherwise NORMAL mode may not redraw text
        return;
    }

    if(c >= '1' && c <= '9' && repeatCount == 0 && commandBuffer.empty())
    {
        repeatCount = c - '0';
        return;
    }
    else if(c >= '0' && c <= '9' && repeatCount > 0)
    {
        repeatCount = repeatCount * 10 + (c - '0');
        return;
    }
    int count = std::max(1, repeatCount);

    // ----- Leader (space) prefixed commands (MUST be early) -----
    if(commandBuffer == " ")
    {
        if(c == 'h')
        {
            // Leader + h: jump to alternate file (header/source)
            jumpToAlternateFile();
            commandBuffer.clear();
            repeatCount = 0;
            return;
        }
        else if(c == 'b')
        {
            // Leader + b: start buffer command sequence
            commandBuffer = " b";
            setStatusMessage("Leader-b");
            repeatCount = 0;
            return;
        }
        else if(c == 'y')
        {
            // Leader + y: yank to system clipboard
            yankToSystemClipboard();
            commandBuffer.clear();
            repeatCount = 0;
            return;
        }
        else if(c == 'p')
        {
            // Leader + p: paste from system clipboard
            pasteFromSystemClipboard();
            commandBuffer.clear();
            repeatCount = 0;
            return;
        }
        else if(c == 'f')
        {
            // Leader + f: format file with clang-format
            commandBuffer.clear();
            repeatCount = 0;

#ifdef UVIM_DEBUG_LOGGING
            // Debug: log to file
            {
                std::ofstream dbg("/tmp/uvim_debug.txt", std::ios::app);
                dbg << "Leader-f pressed. filename='" << *filename
                    << "' isCppFile=" << isCppFile() << std::endl;
            }
#endif

            if(!isCppFile())
            {
                setStatusMessage("clang-format: not a C/C++ file (" +
                                 *filename + ")");
                return;
            }

            // Save current cursor position
            int savedY = *cursorY;
            int savedX = *cursorX;

            // Write buffer to temp file
            std::string tempPath =
                "/tmp/uvim_format_" + std::to_string(getpid()) + ".tmp";
            std::ofstream tempFile(tempPath);
            if(!tempFile.is_open())
            {
                setStatusMessage("clang-format: failed to create temp file");
                return;
            }

            // Write content with trailing newline
            for(size_t i = 0; i < lines->size(); ++i)
            {
                tempFile << (*lines)[i] << '\n';
            }
            tempFile.close();

            // Get the directory of the current file for clang-format to find
            // .clang-format
            std::string fileDir = ".";
            std::string absFilename = *filename;

            // Make filename absolute if it isn't
            if(!absFilename.empty() && absFilename[0] != '/')
            {
                char cwd[PATH_MAX];
                if(getcwd(cwd, sizeof(cwd)))
                {
                    absFilename = std::string(cwd) + "/" + *filename;
                }
            }

            // Run clang-format with stdin, using actual filename for style
            // lookup clang-format searches for .clang-format starting from the
            // file's directory
            std::string cmd = "cat \"" + tempPath +
                              "\" | /opt/homebrew/bin/clang-format -style=file"
                              " -assume-filename=\"" +
                              absFilename +
                              "\""
                              " 2>/tmp/uvim_clang_err.txt";

#ifdef UVIM_DEBUG_LOGGING
            // Debug log
            {
                std::ofstream dbg("/tmp/uvim_debug.txt", std::ios::app);
                dbg << "Temp file written: " << tempPath
                    << " lines=" << lines->size() << std::endl;
                dbg << "Running: " << cmd << std::endl;
            }
#endif

            FILE* pipe = popen(cmd.c_str(), "r");
            if(!pipe)
            {
                // Try without full path
                cmd = "cat \"" + tempPath +
                      "\" | clang-format -style=file"
                      " -assume-filename=\"" +
                      absFilename +
                      "\""
                      " 2>/tmp/uvim_clang_err.txt";
                pipe = popen(cmd.c_str(), "r");
            }

            if(!pipe)
            {
                unlink(tempPath.c_str());
                setStatusMessage("clang-format: failed to run");
                return;
            }

            // Read formatted output
            std::string formatted;
            char buffer[4096];
            while(fgets(buffer, sizeof(buffer), pipe))
            {
                formatted += buffer;
            }
            int status = pclose(pipe);
            unlink(tempPath.c_str());

#ifdef UVIM_DEBUG_LOGGING
            // Debug log
            {
                std::ofstream dbg("/tmp/uvim_debug.txt", std::ios::app);
                dbg << "pclose status=" << status
                    << " formatted.size()=" << formatted.size() << std::endl;
            }
#endif

            // Check for errors
            if(formatted.empty())
            {
                // Read error file
                std::ifstream errFile("/tmp/uvim_clang_err.txt");
                std::string errMsg;
                if(errFile.is_open())
                {
                    std::getline(errFile, errMsg);
                    errFile.close();
                }
                if(errMsg.empty())
                    errMsg = "no output (exit=" +
                             std::to_string(WEXITSTATUS(status)) + ")";
                setStatusMessage("clang-format: " + errMsg.substr(0, 50));
                return;
            }

            // Parse formatted output into lines
            std::vector<std::string> newLines;
            std::istringstream iss(formatted);
            std::string line;
            while(std::getline(iss, line))
            {
                // Remove \r if present
                if(!line.empty() && line.back() == '\r')
                    line.pop_back();
                newLines.push_back(line);
            }

            // Remove trailing empty line if present (clang-format adds one)
            if(!newLines.empty() && newLines.back().empty())
            {
                newLines.pop_back();
            }

            // Ensure at least one line
            if(newLines.empty())
            {
                newLines.push_back("");
            }

            // Check if anything changed
            if(newLines == *lines)
            {
                setStatusMessage("clang-format: no changes needed");
                return;
            }

            // Save state for undo
            saveState();

            // Replace buffer content
            *lines = newLines;
            *dirty = true;

            // Restore cursor position (clamped to valid range)
            if(lines->empty())
            {
                *cursorY = 0;
                *cursorX = 0;
            }
            else
            {
                *cursorY = savedY;
                if(*cursorY >= (int)lines->size())
                    *cursorY = (int)lines->size() - 1;
                if(*cursorY < 0)
                    *cursorY = 0;

                *cursorX = savedX;
                int lineLen = (int)(*lines)[*cursorY].length();
                if(*cursorX > lineLen)
                    *cursorX = lineLen > 0 ? lineLen - 1 : 0;
                if(*cursorX < 0)
                    *cursorX = 0;
            }

            adjustViewport();
            needsFullRedraw = true;
            setStatusMessage("clang-format: formatted " +
                             std::to_string(lines->size()) + " lines");
            return;
        }
        else if(c == ' ')
        {
            // Double space cancels
            commandBuffer.clear();
            setStatusMessage("");
            repeatCount = 0;
            return;
        }
        else
        {
            // Unknown leader command - cancel
            commandBuffer.clear();
            setStatusMessage("");
        }
    }

    // ----- Leader-b (buffer) commands -----
    if(commandBuffer == " b")
    {
        if(c == 'd')
        {
            // Leader + bd: close current buffer
            commandBuffer.clear();
            closeCurrentBuffer();
            repeatCount = 0;
            return;
        }
        else
        {
            // Unknown buffer command - cancel
            commandBuffer.clear();
            setStatusMessage("");
        }
    }

    // ----- g-prefixed commands (MUST be first) -----
    if(commandBuffer == "g")
    {
        if(c == 'd')
        {
            goToDefinition();
            repeatCount = 0;
            return;
        }
        else if(c == 'g')
        {
            moveToFirstLine();
            commandBuffer.clear();
            repeatCount = 0;
            return;
        }
        else
        {
            // Unknown g-command → cancel
            commandBuffer.clear();
        }
    }
    else if(c == 'd')
    {
        if(pendingDelete)
        {
            // dd detected
            for(int i = 0; i < count; i++)
            {
                deleteLine();
            }
            saveState();
            setStatusMessage(std::to_string(count) + " line(s) deleted");
            pendingDelete = false;
            repeatCount = 0;
            return;
        }
        else
        {
            // first 'd'
            pendingDelete = true;
            pendingYank = false;   // Cancel any pending yank
            pendingIndent = false; // Cancel any pending indent
            return;
        }
    }
    else if(c == 'y')
    {
        if(pendingYank)
        {
            // yy detected - yank multiple lines
            yankBuffer.clear();
            int startLine = *cursorY;
            int endLine =
                std::min(startLine + count - 1, (int)lines->size() - 1);

            for(int i = startLine; i <= endLine; i++)
            {
                yankBuffer += (*lines)[i] + "\n";
            }

            int linesYanked = endLine - startLine + 1;
            setStatusMessage(std::to_string(linesYanked) + " line" +
                             (linesYanked > 1 ? "s" : "") + " yanked");
            pendingYank = false;
            repeatCount = 0;
            return;
        }
        else
        {
            // first 'y'
            pendingYank = true;
            pendingDelete = false; // Cancel any pending delete
            pendingIndent = false; // Cancel any pending indent
            return;
        }
    }
    else if(c == '=')
    {
        if(pendingIndent)
        {
            // == detected - indent current line(s)
            int startLine = *cursorY;
            int endLine =
                std::min(startLine + count - 1, (int)lines->size() - 1);

            autoIndentRange(startLine, endLine);

            int linesIndented = endLine - startLine + 1;
            setStatusMessage(std::to_string(linesIndented) + " line" +
                             (linesIndented > 1 ? "s" : "") + " indented");
            pendingIndent = false;
            repeatCount = 0;
            saveState();
            return;
        }
        else
        {
            // first '='
            pendingIndent = true;
            pendingDelete = false; // Cancel any pending delete
            pendingYank = false;   // Cancel any pending yank
            return;
        }
    }
    else if(c == '>')
    {
        if(pendingShiftRight)
        {
            // >> detected - shift right (increase indent)
            int startLine = *cursorY;
            int endLine =
                std::min(startLine + count - 1, (int)lines->size() - 1);

            for(int i = startLine; i <= endLine; i++)
            {
                int currentIndent = getLineIndent(i);
                indentLine(i, currentIndent + 4); // Add 4 spaces
            }

            int linesIndented = endLine - startLine + 1;
            setStatusMessage(std::to_string(linesIndented) + " line" +
                             (linesIndented > 1 ? "s" : "") + " >>");
            pendingShiftRight = false;
            repeatCount = 0;
            saveState();
            needsFullRedraw = true;
            return;
        }
        else
        {
            // first '>'
            pendingShiftRight = true;
            pendingShiftLeft = false;
            pendingDelete = false;
            pendingYank = false;
            pendingIndent = false;
            return;
        }
    }
    else if(c == '<')
    {
        if(pendingShiftLeft)
        {
            // << detected - shift left (decrease indent)
            int startLine = *cursorY;
            int endLine =
                std::min(startLine + count - 1, (int)lines->size() - 1);

            for(int i = startLine; i <= endLine; i++)
            {
                int currentIndent = getLineIndent(i);
                indentLine(i,
                           std::max(0, currentIndent - 4)); // Remove 4 spaces
            }

            int linesIndented = endLine - startLine + 1;
            setStatusMessage(std::to_string(linesIndented) + " line" +
                             (linesIndented > 1 ? "s" : "") + " <<");
            pendingShiftLeft = false;
            repeatCount = 0;
            saveState();
            needsFullRedraw = true;
            return;
        }
        else
        {
            // first '<'
            pendingShiftLeft = true;
            pendingShiftRight = false;
            pendingDelete = false;
            pendingYank = false;
            pendingIndent = false;
            return;
        }
    }
    else if(pendingYank && c != 'y')
    {
        // 'y' followed by motion command - enter operator-pending mode
        pendingYank = false;
        enterOperatorPending('y');
        pendingCount = count;
        // Process the motion command immediately
        handleOperatorPendingMode(c);
        repeatCount = 0;
        return;
    }
    else if(pendingDelete && c != 'd')
    {
        // 'd' followed by motion command - enter operator-pending mode
        pendingDelete = false;
        enterOperatorPending('d');
        pendingCount = count;
        // Process the motion command immediately
        handleOperatorPendingMode(c);
        repeatCount = 0;
        return;
    }
    else if(pendingIndent && c != '=')
    {
        // '=' followed by motion command - enter operator-pending mode
        pendingIndent = false;
        enterOperatorPending('=');
        pendingCount = count;
        // Process the motion command immediately
        handleOperatorPendingMode(c);
        repeatCount = 0;
        return;
    }
    else if(!pendingDelete && !pendingYank && !pendingIndent &&
            !pendingShiftRight && !pendingShiftLeft)
    {
        // Only reset if we're not in the middle of processing pending
        // operations
    }
    switch(c)
    {
    case Terminal::ESC:
    {
        // Handle double ESC to clear search highlights
        auto now = std::chrono::steady_clock::now();
        auto timeSinceLastEsc =
            std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                                  lastEscTime)
                .count();

        if(timeSinceLastEsc <= DOUBLE_ESC_TIMEOUT_MS &&
           (!searchMatches.empty() || !searchQuery.empty()))
        {
            // Double ESC detected - clear search highlights
            clearSearch();
            setStatusMessage("Search cleared");
            needsFullRedraw = true; // Force full redraw to clear highlights
            lastEscTime = std::chrono::steady_clock::time_point(); // Reset
        }
        else
        {
            // First ESC or timeout exceeded
            lastEscTime = now;
            // Clear any pending operations
            pendingDelete = false;
            pendingYank = false;
            pendingIndent = false;
            pendingShiftRight = false;
            pendingShiftLeft = false;
            repeatCount = 0;
            commandBuffer.clear();
        }
    }
    break;
    case 'i':
        saveState();
        setMode(INSERT);
        break;
    case 'I':
        saveState();
        moveToLineStart();
        setMode(INSERT);
        break;
    case 'a':
        saveState();
        if(*cursorX < (*lines)[*cursorY].length())
            (*cursorX)++;
        setMode(INSERT);
        break;
    case 'A':
        saveState();
        moveToLineEnd();
        if(*cursorX < (*lines)[*cursorY].length())
            (*cursorX)++;
        setMode(INSERT);
        break;
    case 'o':
    {
        // Get indentation from current line
        const std::string& currentLine = (*lines)[*cursorY];
        size_t indent = 0;
        while(indent < currentLine.length() &&
              (currentLine[indent] == ' ' || currentLine[indent] == '\t'))
        {
            indent++;
        }
        std::string indentStr = currentLine.substr(0, indent);

        // Check if current line ends with { (add extra indent)
        bool addExtraIndent = false;
        if(isCppFile())
        {
            size_t lastNonSpace = currentLine.find_last_not_of(" \t");
            if(lastNonSpace != std::string::npos &&
               currentLine[lastNonSpace] == '{')
            {
                addExtraIndent = true;
            }
        }

        // Insert new line below with proper indentation
        std::string newLine = indentStr;
        if(addExtraIndent)
        {
            newLine += "    ";
        }
        lines->insert(lines->begin() + *cursorY + 1, newLine);
        (*cursorY)++;
        *cursorX = newLine.length();
        *dirty = true;
        needsFullRedraw = true;
        setMode(INSERT);
        saveState();
        break;
    }
    case 'O':
    {
        // Get indentation from current line
        const std::string& currentLine = (*lines)[*cursorY];
        size_t indent = 0;
        while(indent < currentLine.length() &&
              (currentLine[indent] == ' ' || currentLine[indent] == '\t'))
        {
            indent++;
        }
        std::string indentStr = currentLine.substr(0, indent);

        // Insert new line above with same indentation
        lines->insert(lines->begin() + *cursorY, indentStr);
        *cursorX = indentStr.length();
        *dirty = true;
        needsFullRedraw = true;
        setMode(INSERT);
        saveState();
        break;
    }
    case 'v':
        startVisualMode();
        break;
    case 'V':
        startVisualLineMode();
        break;
    case 22: // Ctrl-V (ASCII 22)
        startVisualBlockMode();
        break;
    case ':':
        setMode(COMMAND);
        break;
    case '/':
        startSearchForward();
        break;
    case '?':
        startSearchBackward();
        break;
    case 'n':
        searchNext();
        break;
    case 'N':
        searchPrevious();
        break;
    case '#':
    {
        // Vim-style: search backward for the word under the cursor.
        // Anchor at the start of the current word so we don't match the same
        // occurrence when the cursor is inside the word.
        std::string sym = getSymbolUnderCursor();
        if(sym.empty())
        {
            setStatusMessage("#: no word under cursor");
            break;
        }

        // Move cursor to the start of the current identifier.
        if(*cursorY >= 0 && *cursorY < (int)lines->size())
        {
            const std::string& line = (*lines)[*cursorY];
            int x = *cursorX;
            if(x >= (int)line.size())
                x = (int)line.size() - 1;

            while(x > 0 && isIdent(line[x - 1]))
                --x;
            *cursorX = x;
        }

        searchQuery = sym;
        searchForward = false;
        performSearch();
        needsFullRedraw = true;
        *wantedX = *cursorX;
        break;
    }
    case 30: // Ctrl+^ (Ctrl+6)
        if(buffers.size() > 1)
        {
            previousBuffer();
        }
        break;
    case Terminal::CTRL_P:
        setMode(FUZZY_FIND);
        break;
    case Terminal::CTRL_W: // Ctrl+W for buffer browser
        setMode(BUFFER_BROWSER);
        break;
    case Terminal::CTRL_S: // Ctrl+S for grep search (find in files)
        setMode(GREP_SEARCH);
        break;
    case Terminal::CTRL_O:
        jumpBack();
        break;
    case Terminal::CTRL_I:
        jumpForward();
        break;
    case 'h':
        moveLeft(count);
        break;
    case Terminal::ARROW_LEFT:
        moveLeft(count);
        break;
    case 'l':
    case Terminal::ARROW_RIGHT:
        moveRight(count);
        break;
    case 'j':
    case Terminal::ARROW_DOWN:
        moveDown(count);
        break;
    case 'k':
    case Terminal::ARROW_UP:
        moveUp(count);
        break;
    case Terminal::CTRL_D:
        scrollHalfPageDown(false);
        break;
    case Terminal::CTRL_U:
        scrollHalfPageUp(false);
        break;
    case 'w':
        while(count-- > 0)
            moveWordForward();
        break;
    case 'b':
        while(count-- > 0)
            moveWordBackward();
        break;
    case 'e':
        while(count-- > 0)
            moveToEndOfWord();
        break;
    case '0':
        if(repeatCount == 0)
            moveToLineStart();
        break;
    case '$':
        moveToLineEnd();
        break;
    case 'g':
        commandBuffer = "g";
        return;
    case 'G':
        if(repeatCount > 0)
        {
            moveToLine(repeatCount - 1);
        }
        else
        {
            moveToLastLine();
        }
        break;
    case ' ': // Leader key (space)
        if(commandBuffer == " ")
        {
            commandBuffer.clear(); // Double space cancels
        }
        else
        {
            commandBuffer = " ";
            setStatusMessage("Leader");
        }
        break;

    case 'x':
        while(count-- > 0)
        {
            deleteCharForward();
        }
        saveState();
        break;
    case 's':
        // Substitute: delete char(s) under cursor and enter insert mode
        while(count-- > 0)
        {
            deleteCharForward();
        }
        saveState();
        setMode(INSERT);
        break;
    case 'D':
        deleteToLineEnd();
        saveState();
        needsFullRedraw = true;
        break;
    case 'Y':
        yankToLineEnd();
        break;

    case 'J':
    {
        // Join lines: current line with next line(s)
        // count specifies how many lines to join (default 2 = current + next)
        int linesToJoin = (repeatCount > 0) ? repeatCount : 2;
        int joinCount = 0;

        for(int i = 0; i < linesToJoin - 1; ++i)
        {
            if(*cursorY >= (int)lines->size() - 1)
                break; // No more lines to join

            std::string& currentLine = (*lines)[*cursorY];
            std::string& nextLine = (*lines)[*cursorY + 1];

            // Remove trailing whitespace from current line
            size_t endPos = currentLine.find_last_not_of(" \t");
            if(endPos != std::string::npos)
                currentLine = currentLine.substr(0, endPos + 1);

            // Remove leading whitespace from next line
            size_t startPos = nextLine.find_first_not_of(" \t");
            std::string trimmedNext = (startPos != std::string::npos)
                                          ? nextLine.substr(startPos)
                                          : "";

            // Join with a single space (unless current line is empty)
            if(!currentLine.empty() && !trimmedNext.empty())
            {
                *cursorX =
                    currentLine.length(); // Position cursor at join point
                currentLine += " " + trimmedNext;
            }
            else if(currentLine.empty())
            {
                currentLine = trimmedNext;
                *cursorX = 0;
            }
            else
            {
                *cursorX = currentLine.length();
            }

            // Delete the next line
            lines->erase(lines->begin() + *cursorY + 1);
            joinCount++;
        }

        if(joinCount > 0)
        {
            *dirty = true;
            saveState();
            needsFullRedraw = true;
            if(joinCount > 1)
                setStatusMessage(std::to_string(joinCount + 1) +
                                 " lines joined");
        }
        break;
    }

    case 'c':
        // change operator: enter operator pending (support e.g. cw, ci(, etc.)
        enterOperatorPending('c');
        break;
    case 'p':
        pasteAfter();
        break;
    case 'P':
        pasteBefore();
        break;
    case 'u':
        undo();
        break;
    case Terminal::CTRL_R:
        redo();
        break;
    case '%':
        moveToMatchingBracket();
        adjustViewport();
        break;
    default:
        if(c != 'g' && c != 'd' && c != 'y')
        {
            commandBuffer.clear();
        }
        break;
    }

    // Handle g-prefixed commands at end (for 'gg' which needs switch case for
    // first 'g')
    if(commandBuffer == "g")
    {
        if(c == 'd')
        {
            commandBuffer.clear();
            goToDefinition();
            repeatCount = 0;
            return;
        }

        // Unknown g-command → cancel
        if(c != 'g')
        {
            commandBuffer.clear();
        }
    }

    repeatCount = 0;
}
void Editor::handleInsertMode(int c)
{
    if(c == Terminal::ESC || c == Terminal::CTRL_C)
    {
        if(*cursorX > 0)
            (*cursorX)--;
        saveState();
        setMode(NORMAL);
        return;
    }

    if(c == Terminal::BACKSPACE || c == 127 || c == 8)
    {
        if(*cursorX > 0)
        {
            (*cursorX)--;
            if(*cursorY < static_cast<int>(lines->size()))
            {
                (*lines)[*cursorY].erase(*cursorX, 1);
            }
            *dirty = true;
        }
        else if(*cursorY > 0)
        {
            int prevLen = (*lines)[*cursorY - 1].length();
            (*lines)[*cursorY - 1] += (*lines)[*cursorY];
            lines->erase(lines->begin() + *cursorY);
            (*cursorY)--;
            *cursorX = prevLen;
            *dirty = true;
        }
        return;
    }

    if(c == Terminal::ENTER)
    {
        insertNewline();
        return;
    }

    if(c == Terminal::TAB)
    {
        insertTab();
        return;
    }

    if(c == Terminal::ARROW_LEFT)
    {
        if(*cursorX > 0)
            (*cursorX)--;
        return;
    }
    if(c == Terminal::ARROW_RIGHT)
    {
        if(*cursorY < static_cast<int>(lines->size()) &&
           *cursorX < static_cast<int>((*lines)[*cursorY].length()))
            (*cursorX)++;
        return;
    }
    if(c == Terminal::ARROW_UP)
    {
        if(*cursorY > 0)
        {
            (*cursorY)--;
            if(*cursorY < static_cast<int>(lines->size()) &&
               *cursorX > static_cast<int>((*lines)[*cursorY].length()))
                *cursorX = (*lines)[*cursorY].length();
        }
        return;
    }
    if(c == Terminal::ARROW_DOWN)
    {
        if(*cursorY < static_cast<int>(lines->size()) - 1)
        {
            (*cursorY)++;
            if(*cursorX > static_cast<int>((*lines)[*cursorY].length()))
                *cursorX = (*lines)[*cursorY].length();
        }
        return;
    }

    if(c == Terminal::CTRL_W)
    {
        deleteWordBackward();
        return;
    }

    if(c == Terminal::CTRL_U)
    {
        deleteToLineStart();
        return;
    }

    if(c >= 32 && c < 127)
    {
        insertChar(static_cast<char>(c));
    }
    else if(c >= 128)
    {
        insertUtf8Char(c);
    }
}

void Editor::handleVisualMode(int c)
{
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

    // Movement keys
    switch(c)
    {
    case 'h':
    case Terminal::ARROW_LEFT:
        moveLeft();
        break;
    case 'l':
    case Terminal::ARROW_RIGHT:
        moveRight();
        break;
    case 'j':
    case Terminal::ARROW_DOWN:
        moveDown();
        break;
    case 'k':
    case Terminal::ARROW_UP:
        moveUp();
        break;
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
    case 'G':
        moveToLastLine();
        break;
    case 'g':
        // Wait for second 'g'
        {
            int next = Terminal::readKey();
            if(next == 'g')
                moveToFirstLine();
        }
        break;
    case 'd':
    case 'x':
        yankSelection();
        deleteSelection();
        setMode(NORMAL);
        break;
    case 'y':
        yankSelection();
        setMode(NORMAL);
        break;
    case 'c':
        yankSelection();
        deleteSelection();
        setMode(INSERT);
        break;
    case 'o':
        swapVisualEnds();
        break;
    }

    setVisualRange();
}

void Editor::handleVisualBlockMode(int c)
{
    if(c == Terminal::ESC || c == Terminal::CTRL_C)
    {
        setMode(NORMAL);
        return;
    }

    // Movement
    switch(c)
    {
    case 'h':
    case Terminal::ARROW_LEFT:
        moveLeft();
        break;
    case 'l':
    case Terminal::ARROW_RIGHT:
        moveRight();
        break;
    case 'j':
    case Terminal::ARROW_DOWN:
        moveDown();
        break;
    case 'k':
    case Terminal::ARROW_UP:
        moveUp();
        break;
    case '0':
        moveToLineStart();
        break;
    case '$':
        moveToLineEnd();
        break;
    case 'd':
    case 'x':
        yankVisualBlock();
        deleteVisualBlock();
        setMode(NORMAL);
        break;
    case 'y':
        yankVisualBlock();
        setMode(NORMAL);
        break;
    case 'I':
        prepareBlockInsert(false);
        setMode(INSERT);
        break;
    case 'A':
        prepareBlockInsert(true);
        setMode(INSERT);
        break;
    case 'o':
    case 'O':
        swapVisualBlockCorner();
        break;
    }
}

void Editor::handleCommandMode(int c)
{
    if(c == Terminal::ESC)
    {
        commandBuffer.clear();
        setMode(NORMAL);
        return;
    }

    if(c == Terminal::ENTER)
    {
        executeCommand(commandBuffer);
        commandBuffer.clear();
        setMode(NORMAL);
        return;
    }

    if(c == Terminal::BACKSPACE || c == 127 || c == 8)
    {
        if(!commandBuffer.empty())
        {
            commandBuffer.pop_back();
        }
        else
        {
            setMode(NORMAL);
        }
        return;
    }

    if(c == Terminal::ARROW_UP)
    {
        commandHistoryUp();
        return;
    }

    if(c == Terminal::ARROW_DOWN)
    {
        commandHistoryDown();
        return;
    }

    if(c >= 32 && c < 127)
    {
        commandBuffer += static_cast<char>(c);
    }
}

void Editor::handleSearchMode(int c)
{
    if(c == Terminal::ESC)
    {
        searchQuery.clear();
        setMode(NORMAL);
        return;
    }

    if(c == Terminal::ENTER)
    {
        if(!searchQuery.empty())
        {
            addSearchToHistory(searchQuery);
            performSearch(searchQuery, searchForward);
        }
        setMode(NORMAL);
        return;
    }

    if(c == Terminal::BACKSPACE || c == 127 || c == 8)
    {
        if(!searchQuery.empty())
        {
            searchQuery.pop_back();
            performIncrementalSearch(searchQuery, searchForward);
        }
        else
        {
            setMode(NORMAL);
        }
        return;
    }

    if(c >= 32 && c < 127)
    {
        searchQuery += static_cast<char>(c);
        performIncrementalSearch(searchQuery, searchForward);
    }
}
