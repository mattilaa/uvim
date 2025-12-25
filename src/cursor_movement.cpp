#include "cursor_movement.h"
#include <algorithm>
#include <cctype>

CursorMovement::CursorMovement(EditorContext& ctx) : ctx(ctx) {}

void CursorMovement::moveLeft(int count)
{
    for(int i = 0; i < count; i++)
    {
        if(*ctx.cursorX > 0)
        {
            (*ctx.cursorX)--;
            *ctx.wantedX = *ctx.cursorX;
        }
        else if(*ctx.cursorY > 0)
        {
            (*ctx.cursorY)--;
            *ctx.cursorX = (*ctx.lines)[*ctx.cursorY].length();
            *ctx.wantedX = *ctx.cursorX;
        }
    }
}

void CursorMovement::moveRight(int count)
{
    for(int i = 0; i < count; i++)
    {
        if(*ctx.cursorY < (int)ctx.lines->size())
        {
            int lineLen = (*ctx.lines)[*ctx.cursorY].length();
            if(*ctx.cursorX < lineLen)
            {
                (*ctx.cursorX)++;
                *ctx.wantedX = *ctx.cursorX;
            }
            else if(*ctx.cursorY < (int)ctx.lines->size() - 1)
            {
                (*ctx.cursorY)++;
                *ctx.cursorX = 0;
                *ctx.wantedX = 0;
            }
        }
    }
}

void CursorMovement::moveUp(int count)
{
    for(int i = 0; i < count && *ctx.cursorY > 0; i++)
    {
        (*ctx.cursorY)--;
        int lineLen = (*ctx.lines)[*ctx.cursorY].length();
        *ctx.cursorX = std::min(*ctx.wantedX, lineLen);
    }
}

void CursorMovement::moveDown(int count)
{
    for(int i = 0; i < count && *ctx.cursorY < (int)ctx.lines->size() - 1; i++)
    {
        (*ctx.cursorY)++;
        int lineLen = (*ctx.lines)[*ctx.cursorY].length();
        *ctx.cursorX = std::min(*ctx.wantedX, lineLen);
    }
}

void CursorMovement::moveWordForward()
{
    if(ctx.lines->empty())
        return;

    if(*ctx.cursorY >= (int)ctx.lines->size())
    {
        *ctx.cursorY = ctx.lines->size() - 1;
    }

    const std::string& line = (*ctx.lines)[*ctx.cursorY];

    if(*ctx.cursorX >= (int)line.length())
    {
        if(*ctx.cursorY < (int)ctx.lines->size() - 1)
        {
            (*ctx.cursorY)++;
            *ctx.cursorX = 0;
            const std::string& newLine = (*ctx.lines)[*ctx.cursorY];
            while(
                *ctx.cursorX < (int)newLine.length() &&
                std::isspace(static_cast<unsigned char>(newLine[*ctx.cursorX])))
            {
                (*ctx.cursorX)++;
            }
        }
        *ctx.wantedX = *ctx.cursorX;
        return;
    }

    // Skip current word/punctuation
    if(ctx.isWordChar(line[*ctx.cursorX]))
    {
        while(*ctx.cursorX < (int)line.length() &&
              ctx.isWordChar(line[*ctx.cursorX]))
        {
            (*ctx.cursorX)++;
        }
    }
    else if(!std::isspace(static_cast<unsigned char>(line[*ctx.cursorX])))
    {
        while(*ctx.cursorX < (int)line.length() &&
              !ctx.isWordChar(line[*ctx.cursorX]) &&
              !std::isspace(static_cast<unsigned char>(line[*ctx.cursorX])))
        {
            (*ctx.cursorX)++;
        }
    }

    // Skip whitespace
    while(*ctx.cursorX < (int)line.length() &&
          std::isspace(static_cast<unsigned char>(line[*ctx.cursorX])))
    {
        (*ctx.cursorX)++;
    }

    // Move to next line if at end
    if(*ctx.cursorX >= (int)line.length() &&
       *ctx.cursorY < (int)ctx.lines->size() - 1)
    {
        (*ctx.cursorY)++;
        *ctx.cursorX = 0;
        const std::string& newLine = (*ctx.lines)[*ctx.cursorY];
        while(*ctx.cursorX < (int)newLine.length() &&
              std::isspace(static_cast<unsigned char>(newLine[*ctx.cursorX])))
        {
            (*ctx.cursorX)++;
        }
    }

    *ctx.wantedX = *ctx.cursorX;
}

void CursorMovement::moveWordBackward()
{
    if(ctx.lines->empty())
        return;

    if(*ctx.cursorY >= (int)ctx.lines->size())
    {
        *ctx.cursorY = ctx.lines->size() - 1;
    }

    if(*ctx.cursorX == 0)
    {
        if(*ctx.cursorY > 0)
        {
            (*ctx.cursorY)--;
            *ctx.cursorX = (*ctx.lines)[*ctx.cursorY].length();
        }
        else
        {
            *ctx.wantedX = *ctx.cursorX;
            return;
        }
    }

    const std::string& line = (*ctx.lines)[*ctx.cursorY];

    if(*ctx.cursorX > 0)
    {
        (*ctx.cursorX)--;
    }

    // Skip whitespace backwards
    while(*ctx.cursorX > 0 &&
          std::isspace(static_cast<unsigned char>(line[*ctx.cursorX])))
    {
        (*ctx.cursorX)--;
    }

    // Handle end of line whitespace
    if(*ctx.cursorX == 0 &&
       std::isspace(static_cast<unsigned char>(line[*ctx.cursorX])) &&
       *ctx.cursorY > 0)
    {
        (*ctx.cursorY)--;
        *ctx.cursorX = (*ctx.lines)[*ctx.cursorY].length();
        if(*ctx.cursorX > 0)
            (*ctx.cursorX)--;
        const std::string& prevLine = (*ctx.lines)[*ctx.cursorY];
        while(*ctx.cursorX > 0 &&
              std::isspace(static_cast<unsigned char>(prevLine[*ctx.cursorX])))
        {
            (*ctx.cursorX)--;
        }
    }

    if(*ctx.cursorY < (int)ctx.lines->size())
    {
        const std::string& currentLine = (*ctx.lines)[*ctx.cursorY];
        if(*ctx.cursorX < (int)currentLine.length())
        {
            if(ctx.isWordChar(currentLine[*ctx.cursorX]))
            {
                while(*ctx.cursorX > 0 &&
                      ctx.isWordChar(currentLine[*ctx.cursorX - 1]))
                {
                    (*ctx.cursorX)--;
                }
            }
            else if(!std::isspace(
                        static_cast<unsigned char>(currentLine[*ctx.cursorX])))
            {
                while(*ctx.cursorX > 0 &&
                      !ctx.isWordChar(currentLine[*ctx.cursorX - 1]) &&
                      !std::isspace(static_cast<unsigned char>(
                          currentLine[*ctx.cursorX - 1])))
                {
                    (*ctx.cursorX)--;
                }
            }
        }
    }

    *ctx.wantedX = *ctx.cursorX;
}

void CursorMovement::moveToEndOfWord()
{
    if(ctx.lines->empty())
        return;

    if(*ctx.cursorY >= (int)ctx.lines->size())
    {
        *ctx.cursorY = ctx.lines->size() - 1;
    }

    const std::string& line = (*ctx.lines)[*ctx.cursorY];

    if(*ctx.cursorX >= (int)line.length())
    {
        if(*ctx.cursorY < (int)ctx.lines->size() - 1)
        {
            (*ctx.cursorY)++;
            *ctx.cursorX = 0;
        }
        else
        {
            *ctx.wantedX = *ctx.cursorX;
            return;
        }
    }

    if(*ctx.cursorX < (int)(*ctx.lines)[*ctx.cursorY].length())
    {
        const std::string& currentLine = (*ctx.lines)[*ctx.cursorY];

        if(*ctx.cursorX + 1 < (int)currentLine.length() &&
           ctx.isWordChar(currentLine[*ctx.cursorX]) &&
           ctx.isWordChar(currentLine[*ctx.cursorX + 1]))
        {
            (*ctx.cursorX)++;
        }
        else if(*ctx.cursorX + 1 < (int)currentLine.length())
        {
            (*ctx.cursorX)++;
            while(*ctx.cursorX < (int)currentLine.length() &&
                  std::isspace(
                      static_cast<unsigned char>(currentLine[*ctx.cursorX])))
            {
                (*ctx.cursorX)++;
            }
            if(*ctx.cursorX >= (int)currentLine.length() &&
               *ctx.cursorY < (int)ctx.lines->size() - 1)
            {
                (*ctx.cursorY)++;
                *ctx.cursorX = 0;
                const std::string& newLine = (*ctx.lines)[*ctx.cursorY];
                while(*ctx.cursorX < (int)newLine.length() &&
                      std::isspace(
                          static_cast<unsigned char>(newLine[*ctx.cursorX])))
                {
                    (*ctx.cursorX)++;
                }
            }
        }
    }

    if(*ctx.cursorY < (int)ctx.lines->size())
    {
        const std::string& currentLine = (*ctx.lines)[*ctx.cursorY];
        if(*ctx.cursorX < (int)currentLine.length())
        {
            if(ctx.isWordChar(currentLine[*ctx.cursorX]))
            {
                while(*ctx.cursorX + 1 < (int)currentLine.length() &&
                      ctx.isWordChar(currentLine[*ctx.cursorX + 1]))
                {
                    (*ctx.cursorX)++;
                }
            }
            else if(!std::isspace(
                        static_cast<unsigned char>(currentLine[*ctx.cursorX])))
            {
                while(*ctx.cursorX + 1 < (int)currentLine.length() &&
                      !ctx.isWordChar(currentLine[*ctx.cursorX + 1]) &&
                      !std::isspace(static_cast<unsigned char>(
                          currentLine[*ctx.cursorX + 1])))
                {
                    (*ctx.cursorX)++;
                }
            }
        }
    }

    *ctx.wantedX = *ctx.cursorX;
}

void CursorMovement::moveToLineStart()
{
    *ctx.cursorX = 0;
    *ctx.wantedX = 0;
}

void CursorMovement::moveToLineEnd()
{
    if(*ctx.cursorY < (int)ctx.lines->size())
    {
        int lineLen = (*ctx.lines)[*ctx.cursorY].length();
        *ctx.cursorX = lineLen > 0 ? lineLen - 1 : 0;
        *ctx.wantedX = *ctx.cursorX;
    }
}

void CursorMovement::moveToFirstNonBlank()
{
    if(*ctx.cursorY >= (int)ctx.lines->size())
        return;

    const std::string& line = (*ctx.lines)[*ctx.cursorY];
    *ctx.cursorX = 0;
    while(*ctx.cursorX < (int)line.length() &&
          (line[*ctx.cursorX] == ' ' || line[*ctx.cursorX] == '\t'))
    {
        (*ctx.cursorX)++;
    }
    *ctx.wantedX = *ctx.cursorX;
}

void CursorMovement::moveToFirstLine()
{
    *ctx.cursorY = 0;
    *ctx.cursorX = 0;
    *ctx.wantedX = 0;
}

void CursorMovement::moveToLastLine()
{
    *ctx.cursorY = ctx.lines->size() - 1;
    *ctx.cursorX = 0;
    *ctx.wantedX = 0;
}

void CursorMovement::moveToLine(int line)
{
    *ctx.cursorY = std::max(0, std::min(line, (int)ctx.lines->size() - 1));
    *ctx.cursorX = 0;
    *ctx.wantedX = 0;
}

void CursorMovement::scrollHalfPageDown(bool visual)
{
    int halfPage = ctx.screenRows / 2;
    *ctx.offsetY += halfPage;
    *ctx.cursorY += halfPage;

    if(*ctx.cursorY >= (int)ctx.lines->size())
    {
        *ctx.cursorY = ctx.lines->size() - 1;
    }
    if(*ctx.offsetY + ctx.screenRows > (int)ctx.lines->size())
    {
        *ctx.offsetY = std::max(0, (int)ctx.lines->size() - ctx.screenRows);
    }

    ctx.needsFullRedraw = true;
}

void CursorMovement::scrollHalfPageUp(bool visual)
{
    int halfPage = ctx.screenRows / 2;
    *ctx.offsetY -= halfPage;
    *ctx.cursorY -= halfPage;

    if(*ctx.cursorY < 0)
        *ctx.cursorY = 0;
    if(*ctx.offsetY < 0)
        *ctx.offsetY = 0;

    ctx.needsFullRedraw = true;
}

void CursorMovement::centerScreen()
{
    *ctx.offsetY = *ctx.cursorY - ctx.screenRows / 2;
    if(*ctx.offsetY < 0)
        *ctx.offsetY = 0;
    ctx.needsFullRedraw = true;
}

void CursorMovement::moveToMatchingBracket()
{
    if(*ctx.cursorY >= (int)ctx.lines->size())
        return;

    const std::string& line = (*ctx.lines)[*ctx.cursorY];
    if(*ctx.cursorX >= (int)line.length())
        return;

    char current = line[*ctx.cursorX];
    char match;
    int direction;

    switch(current)
    {
    case '(':
        match = ')';
        direction = 1;
        break;
    case ')':
        match = '(';
        direction = -1;
        break;
    case '[':
        match = ']';
        direction = 1;
        break;
    case ']':
        match = '[';
        direction = -1;
        break;
    case '{':
        match = '}';
        direction = 1;
        break;
    case '}':
        match = '{';
        direction = -1;
        break;
    default:
        return;
    }

    int depth = 1;
    int y = *ctx.cursorY;
    int x = *ctx.cursorX + direction;

    while(depth > 0)
    {
        if(x < 0)
        {
            y--;
            if(y < 0)
                return;
            x = (*ctx.lines)[y].length() - 1;
        }
        else if(x >= (int)(*ctx.lines)[y].length())
        {
            y++;
            if(y >= (int)ctx.lines->size())
                return;
            x = 0;
        }
        else
        {
            char c = (*ctx.lines)[y][x];
            if(c == current)
                depth++;
            else if(c == match)
                depth--;

            if(depth > 0)
                x += direction;
        }
    }

    *ctx.cursorY = y;
    *ctx.cursorX = x;
    *ctx.wantedX = x;
}

void CursorMovement::findCharForward(char c)
{
    if(*ctx.cursorY >= (int)ctx.lines->size())
        return;

    const std::string& line = (*ctx.lines)[*ctx.cursorY];
    for(int i = *ctx.cursorX + 1; i < (int)line.length(); i++)
    {
        if(line[i] == c)
        {
            *ctx.cursorX = i;
            *ctx.wantedX = i;
            ctx.lastFindChar = c;
            ctx.lastFindForward = true;
            return;
        }
    }
}

void CursorMovement::findCharBackward(char c)
{
    if(*ctx.cursorY >= (int)ctx.lines->size())
        return;

    const std::string& line = (*ctx.lines)[*ctx.cursorY];
    for(int i = *ctx.cursorX - 1; i >= 0; i--)
    {
        if(line[i] == c)
        {
            *ctx.cursorX = i;
            *ctx.wantedX = i;
            ctx.lastFindChar = c;
            ctx.lastFindForward = false;
            return;
        }
    }
}

void CursorMovement::findCharTillForward(char c)
{
    if(*ctx.cursorY >= (int)ctx.lines->size())
        return;

    const std::string& line = (*ctx.lines)[*ctx.cursorY];
    for(int i = *ctx.cursorX + 1; i < (int)line.length(); i++)
    {
        if(line[i] == c)
        {
            *ctx.cursorX = i - 1;
            *ctx.wantedX = *ctx.cursorX;
            ctx.lastFindChar = c;
            ctx.lastFindForward = true;
            return;
        }
    }
}

void CursorMovement::findCharTillBackward(char c)
{
    if(*ctx.cursorY >= (int)ctx.lines->size())
        return;

    const std::string& line = (*ctx.lines)[*ctx.cursorY];
    for(int i = *ctx.cursorX - 1; i >= 0; i--)
    {
        if(line[i] == c)
        {
            *ctx.cursorX = i + 1;
            *ctx.wantedX = *ctx.cursorX;
            ctx.lastFindChar = c;
            ctx.lastFindForward = false;
            return;
        }
    }
}

void CursorMovement::repeatFindChar()
{
    if(ctx.lastFindChar == 0)
        return;

    if(ctx.lastFindForward)
        findCharForward(ctx.lastFindChar);
    else
        findCharBackward(ctx.lastFindChar);
}

void CursorMovement::repeatFindCharReverse()
{
    if(ctx.lastFindChar == 0)
        return;

    if(ctx.lastFindForward)
        findCharBackward(ctx.lastFindChar);
    else
        findCharForward(ctx.lastFindChar);
}

void CursorMovement::jumpForward()
{
    if(ctx.jumpForwardStack.empty())
    {
        ctx.statusMessage = "No forward jump point";
        return;
    }

    pushJumpLocation();
    ctx.jumpBackStack.pop_back();

    JumpLocation loc = ctx.jumpForwardStack.back();
    ctx.jumpForwardStack.pop_back();

    restoreJumpLocation(loc);
    ctx.statusMessage = "Jumped forward";
}

void CursorMovement::jumpBack()
{
    if(ctx.jumpBackStack.empty())
    {
        ctx.statusMessage = "No previous jump point";
        return;
    }

    JumpLocation current;
    current.bufferIndex = ctx.currentBufferIndex;
    current.cursorX = *ctx.cursorX;
    current.cursorY = *ctx.cursorY;
    current.offsetX = *ctx.offsetX;
    current.offsetY = *ctx.offsetY;
    ctx.jumpForwardStack.push_back(current);

    JumpLocation loc = ctx.jumpBackStack.back();
    ctx.jumpBackStack.pop_back();

    restoreJumpLocation(loc);
    ctx.statusMessage = "Jumped back";
}

void CursorMovement::pushJumpLocation()
{
    JumpLocation loc;
    loc.bufferIndex = ctx.currentBufferIndex;
    loc.cursorX = *ctx.cursorX;
    loc.cursorY = *ctx.cursorY;
    loc.offsetX = *ctx.offsetX;
    loc.offsetY = *ctx.offsetY;
    ctx.jumpBackStack.push_back(loc);

    ctx.jumpForwardStack.clear();

    if(ctx.jumpBackStack.size() > 100)
    {
        ctx.jumpBackStack.erase(ctx.jumpBackStack.begin());
    }
}

void CursorMovement::restoreJumpLocation(const JumpLocation& loc)
{
    // Note: Buffer switching would need BufferManager reference
    // For now, just restore cursor position
    *ctx.cursorX = loc.cursorX;
    *ctx.cursorY = loc.cursorY;
    *ctx.offsetX = loc.offsetX;
    *ctx.offsetY = loc.offsetY;
    *ctx.wantedX = *ctx.cursorX;
    ctx.needsFullRedraw = true;
}

void CursorMovement::adjustViewport()
{
    if(*ctx.cursorY < *ctx.offsetY)
    {
        *ctx.offsetY = *ctx.cursorY;
    }
    if(*ctx.cursorY >= *ctx.offsetY + ctx.screenRows)
    {
        *ctx.offsetY = *ctx.cursorY - ctx.screenRows + 1;
    }

    if(*ctx.cursorX < *ctx.offsetX)
    {
        *ctx.offsetX = *ctx.cursorX;
    }
    if(*ctx.cursorX >= *ctx.offsetX + ctx.screenCols)
    {
        *ctx.offsetX = *ctx.cursorX - ctx.screenCols + 1;
    }
}
