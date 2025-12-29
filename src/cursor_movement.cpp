#include "editor.h"
#include <algorithm>
#include <cctype>

void Editor::moveLeft(int count)
{
    while(count-- > 0)
    {
        if(*cursorX > 0)
        {
            (*cursorX)--;
        }
        else if(*cursorY > 0)
        {
            (*cursorY)--;
            *cursorX = (*lines)[*cursorY].length();
        }
    }
    *wantedX = *cursorX;
}

void Editor::moveRight(int count)
{
    while(count-- > 0)
    {
        if(*cursorY < lines->size())
        {
            if(*cursorX < (*lines)[*cursorY].length())
            {
                (*cursorX)++;
            }
            else if(*cursorY < lines->size() - 1)
            {
                (*cursorY)++;
                *cursorX = 0;
            }
        }
    }
    *wantedX = *cursorX;
}

void Editor::moveUp(int count)
{
    while(count-- > 0 && *cursorY > 0)
    {
        (*cursorY)--;
    }
    *cursorX = std::min(*wantedX, (int)(*lines)[*cursorY].length());
}

void Editor::moveDown(int count)
{
    while(count-- > 0 && *cursorY < lines->size() - 1)
    {
        (*cursorY)++;
    }
    *cursorX = std::min(*wantedX, (int)(*lines)[*cursorY].length());
}

void Editor::moveWordForward()
{
    int y = *cursorY;
    int x = *cursorX;

    while(true)
    {
        const std::string& line = (*lines)[y];

        if(x >= (int)line.length())
        {
            if(y + 1 >= (int)lines->size())
                break;

            y++;
            x = 0;

            while(x < (int)(*lines)[y].length() &&
                  std::isspace((unsigned char)(*lines)[y][x]))
            {
                x++;
            }

            break;
        }

        char c = line[x];

        if(std::isspace((unsigned char)c))
        {
            while(x < (int)line.length() &&
                  std::isspace((unsigned char)line[x]))
            {
                x++;
            }
            break;
        }

        bool isAlphaWord = (std::isalnum((unsigned char)c) || c == '_');
        x++;

        while(x < (int)line.length())
        {
            char d = line[x];
            bool dAlpha = (std::isalnum((unsigned char)d) || d == '_');

            if(std::isspace((unsigned char)d))
                break;
            if(isAlphaWord != dAlpha)
                break;

            x++;
        }

        break;
    }

    *cursorX = x;
    *cursorY = y;
    *wantedX = x;
}

void Editor::moveWordBackward()
{
    int y = *cursorY;
    int x = *cursorX;

    if(y == 0 && x == 0)
    {
        return;
    }

    if(x == 0)
    {
        y--;
        x = (*lines)[y].length();
    }

    const std::string& line = (*lines)[y];

    if(x > 0)
    {
        x--;
    }

    while(x > 0 && std::isspace((unsigned char)line[x]))
    {
        x--;
    }

    if(x >= 0 && !std::isspace((unsigned char)line[x]))
    {
        bool isAlphaWord =
            (std::isalnum((unsigned char)line[x]) || line[x] == '_');

        while(x > 0)
        {
            char prevChar = line[x - 1];
            bool prevAlpha =
                (std::isalnum((unsigned char)prevChar) || prevChar == '_');

            if(std::isspace((unsigned char)prevChar))
                break;
            if(isAlphaWord != prevAlpha)
                break;

            x--;
        }
    }

    *cursorX = x;
    *cursorY = y;
    *wantedX = x;
}

void Editor::moveToEndOfWord()
{
    int y = *cursorY;
    int x = *cursorX;

    const std::string& line = (*lines)[y];

    if(x >= (int)line.length() - 1)
    {
        if(y + 1 < (int)lines->size())
        {
            y++;
            x = 0;
            const std::string& nextLine = (*lines)[y];

            while(x < (int)nextLine.length() &&
                  std::isspace((unsigned char)nextLine[x]))
            {
                x++;
            }

            if(x < (int)nextLine.length())
            {
                bool isAlphaWord = (std::isalnum((unsigned char)nextLine[x]) ||
                                    nextLine[x] == '_');

                while(x < (int)nextLine.length() - 1)
                {
                    char nextChar = nextLine[x + 1];
                    bool nextAlpha = (std::isalnum((unsigned char)nextChar) ||
                                      nextChar == '_');

                    if(std::isspace((unsigned char)nextChar))
                        break;
                    if(isAlphaWord != nextAlpha)
                        break;

                    x++;
                }
            }
        }
    }
    else
    {
        x++;

        while(x < (int)line.length() && std::isspace((unsigned char)line[x]))
        {
            x++;
        }

        if(x < (int)line.length())
        {
            bool isAlphaWord =
                (std::isalnum((unsigned char)line[x]) || line[x] == '_');

            while(x < (int)line.length() - 1)
            {
                char nextChar = line[x + 1];
                bool nextAlpha =
                    (std::isalnum((unsigned char)nextChar) || nextChar == '_');

                if(std::isspace((unsigned char)nextChar))
                    break;
                if(isAlphaWord != nextAlpha)
                    break;

                x++;
            }
        }
    }

    *cursorX = x;
    *cursorY = y;
    *wantedX = x;
}

void Editor::moveToLineStart()
{
    *cursorX = 0;
    *wantedX = *cursorX;
}

void Editor::moveToLineEnd()
{
    if(*cursorY < lines->size())
    {
        *cursorX = (*lines)[*cursorY].length();
        if(*cursorX > 0 && currentMode == NORMAL)
        {
            (*cursorX)--;
        }
    }
    *wantedX = *cursorX;
}

void Editor::moveToFirstLine()
{
    *cursorY = 0;
    *cursorX = 0;
    *wantedX = *cursorX;
}

void Editor::moveToLastLine()
{
    *cursorY = lines->size() - 1;
    *cursorX = 0;
    *wantedX = *cursorX;
}

void Editor::moveToLine(int line)
{
    *cursorY = std::max(0, std::min(line, (int)lines->size() - 1));
    *cursorX = 0;
}

void Editor::jumpForward()
{
    if(jumpForwardStack.empty())
    {
        setStatusMessage("Jump stack empty");
        return;
    }

    JumpLocation current;
    current.bufferIndex = currentBufferIndex;
    current.cursorX = *cursorX;
    current.cursorY = *cursorY;
    current.offsetX = *offsetX;
    current.offsetY = *offsetY;

    jumpBackStack.push_back(current);

    JumpLocation target = jumpForwardStack.back();
    jumpForwardStack.pop_back();

    restoreJumpLocation(target);
}

void Editor::jumpBack()
{
    if(jumpBackStack.empty())
    {
        setStatusMessage("Jump stack empty");
        return;
    }

    JumpLocation current;
    current.bufferIndex = currentBufferIndex;
    current.cursorX = *cursorX;
    current.cursorY = *cursorY;
    current.offsetX = *offsetX;
    current.offsetY = *offsetY;

    jumpForwardStack.push_back(current);

    JumpLocation target = jumpBackStack.back();
    jumpBackStack.pop_back();

    restoreJumpLocation(target);
}

void Editor::pushJumpLocation()
{
    if(!currentBuffer)
        return;

    JumpLocation loc;
    loc.bufferIndex = currentBufferIndex;
    loc.cursorX = *cursorX;
    loc.cursorY = *cursorY;
    loc.offsetX = *offsetX;
    loc.offsetY = *offsetY;

    jumpBackStack.push_back(loc);
    jumpForwardStack.clear();
}

void Editor::restoreJumpLocation(const JumpLocation& loc)
{
    if(loc.bufferIndex < 0 || loc.bufferIndex >= buffers.size())
        return;

    switchToBuffer(loc.bufferIndex);

    *cursorX = loc.cursorX;
    *cursorY = loc.cursorY;
    *offsetX = loc.offsetX;
    *offsetY = loc.offsetY;

    needsFullRedraw = true;
}

void Editor::scrollHalfPageDown(bool visual)
{
    int half = screenRows / 2;
    moveDown(half);
    adjustViewport();

    if(visual)
        updateVisualSelection();
}

void Editor::scrollHalfPageUp(bool visual)
{
    int half = screenRows / 2;
    moveUp(half);
    adjustViewport();

    if(visual)
        updateVisualSelection();
}

void Editor::moveToMatchingBracket()
{
    if(*cursorY >= lines->size())
        return;

    const std::string& line = (*lines)[*cursorY];
    if(*cursorX >= line.length())
        return;

    char c = line[*cursorX];
    char open, close;

    switch(c)
    {
    case '(':
        open = '(';
        close = ')';
        break;
    case ')':
        open = '(';
        close = ')';
        break;
    case '{':
        open = '{';
        close = '}';
        break;
    case '}':
        open = '{';
        close = '}';
        break;
    case '[':
        open = '[';
        close = ']';
        break;
    case ']':
        open = '[';
        close = ']';
        break;
    default:
        return;
    }

    int direction = (c == open) ? +1 : -1;
    int depth = 0;

    int y = *cursorY;
    int x = *cursorX;

    while(true)
    {
        x += direction;

        while(x < 0 || (y < lines->size() && x >= (*lines)[y].length()))
        {
            if(direction == +1)
            {
                y++;
                x = 0;
            }
            else
            {
                y--;
                if(y < 0)
                    return;
                x = (*lines)[y].length() - 1;
            }
            if(y < 0 || y >= lines->size())
                return;
        }

        char ch = (*lines)[y][x];

        if(ch == c)
            depth++;
        else if(ch == (direction == +1 ? close : open))
        {
            if(depth == 0)
            {
                *cursorY = y;
                *cursorX = x;
                *wantedX = x;
                return;
            }
            depth--;
        }
    }
}

void Editor::adjustViewport()
{
    if(*cursorY < *offsetY)
    {
        *offsetY = std::max(0, *cursorY);
    }
    else if(*cursorY >= *offsetY + screenRows)
    {
        *offsetY = std::min((int)lines->size() - screenRows,
                            *cursorY - screenRows + 1);
    }

    if(*cursorX < *offsetX)
    {
        *offsetX = *cursorX;
    }
    else if(*cursorX >= *offsetX + screenCols)
    {
        *offsetX = *cursorX - screenCols + 1;
    }
}

void Editor::centerScreen()
{
    *offsetY = std::max(0, *cursorY - screenRows / 2);
    if(*offsetY + screenRows > lines->size())
    {
        *offsetY = std::max(0, (int)lines->size() - screenRows);
    }
}
