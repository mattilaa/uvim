#include "operator_pending.h"
#include "cursor_movement.h"
#include "text_operations.h"
#include <algorithm>

OperatorPending::OperatorPending(EditorContext& ctx, TextOperations& textOps,
                                 CursorMovement& cursor)
    : ctx(ctx), textOps(textOps), cursor(cursor)
{
}

void OperatorPending::enter(char op)
{
    ctx.pendingOperator = op;
    ctx.pendingAwaitingObject = false;
    ctx.pendingObjectType = 0;
    ctx.currentMode = Mode::OP_PENDING;
}

void OperatorPending::cancel()
{
    ctx.pendingOperator = 0;
    ctx.pendingAwaitingObject = false;
    ctx.pendingObjectType = 0;
    ctx.pendingCount = 0;
    ctx.currentMode = Mode::NORMAL;
}

void OperatorPending::handleKey(int c)
{
    if(c == 27) // ESC
    {
        cancel();
        return;
    }

    // Awaiting text object type (i or a)
    if(ctx.pendingAwaitingObject)
    {
        int startY, startX, endY, endX;
        bool around = (ctx.pendingObjectType == 'a');

        if(getTextObjectRange((char)c, around, startY, startX, endY, endX))
        {
            applyOperatorToRange(ctx.pendingOperator, startY, startX, endY,
                                 endX);
        }

        cancel();
        return;
    }

    // Check for text object prefix (i or a)
    if(c == 'i' || c == 'a')
    {
        ctx.pendingAwaitingObject = true;
        ctx.pendingObjectType = (char)c;
        return;
    }

    // Double operator (e.g., dd, yy, cc)
    if(c == ctx.pendingOperator)
    {
        int count = std::max(1, ctx.pendingCount);
        int startY = *ctx.cursorY;
        int endY = std::min(startY + count - 1, (int)ctx.lines->size() - 1);

        // Line-wise operation
        int startX = 0;
        int endX = (*ctx.lines)[endY].length();

        // For line-wise yank, include newline
        if(ctx.pendingOperator == 'y')
        {
            ctx.yankBuffer.clear();
            for(int y = startY; y <= endY; y++)
            {
                ctx.yankBuffer += (*ctx.lines)[y] + "\n";
            }
            ctx.statusMessage =
                std::to_string(endY - startY + 1) + " lines yanked";
        }
        else if(ctx.pendingOperator == 'd')
        {
            // Yank first
            ctx.yankBuffer.clear();
            for(int y = startY; y <= endY; y++)
            {
                ctx.yankBuffer += (*ctx.lines)[y] + "\n";
            }

            // Delete lines
            for(int y = endY; y >= startY; y--)
            {
                if(ctx.lines->size() > 1)
                {
                    ctx.lines->erase(ctx.lines->begin() + y);
                }
                else
                {
                    (*ctx.lines)[0].clear();
                }
            }

            *ctx.cursorY = std::min(startY, (int)ctx.lines->size() - 1);
            *ctx.cursorX = 0;
            *ctx.dirty = true;
            ctx.needsFullRedraw = true;
        }
        else if(ctx.pendingOperator == 'c')
        {
            // Yank first
            ctx.yankBuffer.clear();
            for(int y = startY; y <= endY; y++)
            {
                ctx.yankBuffer += (*ctx.lines)[y] + "\n";
            }

            // Delete lines and leave one empty
            for(int y = endY; y > startY; y--)
            {
                ctx.lines->erase(ctx.lines->begin() + y);
            }
            (*ctx.lines)[startY].clear();

            *ctx.cursorY = startY;
            *ctx.cursorX = 0;
            *ctx.dirty = true;
            ctx.needsFullRedraw = true;
            ctx.currentMode = Mode::INSERT;
            cancel();
            return;
        }
        else if(ctx.pendingOperator == '=')
        {
            textOps.autoIndentRange(startY, endY);
            ctx.statusMessage =
                std::to_string(endY - startY + 1) + " lines indented";
        }

        cancel();
        return;
    }

    // Motion commands
    int startY = *ctx.cursorY;
    int startX = *ctx.cursorX;

    // Execute the motion
    switch(c)
    {
    case 'w':
        cursor.moveWordForward();
        break;
    case 'b':
        cursor.moveWordBackward();
        break;
    case 'e':
        cursor.moveToEndOfWord();
        break;
    case '0':
        cursor.moveToLineStart();
        break;
    case '$':
        cursor.moveToLineEnd();
        break;
    case 'h':
        cursor.moveLeft();
        break;
    case 'l':
        cursor.moveRight();
        break;
    case 'j':
        cursor.moveDown();
        break;
    case 'k':
        cursor.moveUp();
        break;
    case 'G':
        cursor.moveToLastLine();
        break;
    case 'g':
        // Wait for second 'g'
        return;
    case '%':
        cursor.moveToMatchingBracket();
        break;
    case 'f':
    case 'F':
    case 't':
    case 'T':
        // These need a character argument
        // For simplicity, skip for now
        cancel();
        return;
    default:
        cancel();
        return;
    }

    int endY = *ctx.cursorY;
    int endX = *ctx.cursorX;

    // Apply operator to range
    applyOperatorToRange(ctx.pendingOperator, startY, startX, endY, endX);
    cancel();
}

void OperatorPending::applyOperatorToRange(char op, int startY, int startX,
                                           int endY, int endX)
{
    // Normalize range
    if(startY > endY || (startY == endY && startX > endX))
    {
        std::swap(startY, endY);
        std::swap(startX, endX);
    }

    if(op == 'y')
    {
        textOps.yankRange(startY, startX, endY, endX);
        ctx.statusMessage = "Yanked";
    }
    else if(op == 'd')
    {
        textOps.yankRange(startY, startX, endY, endX);
        textOps.deleteRange(startY, startX, endY, endX);
        *ctx.cursorY = startY;
        *ctx.cursorX = startX;
        *ctx.dirty = true;
        ctx.needsFullRedraw = true;
    }
    else if(op == 'c')
    {
        textOps.yankRange(startY, startX, endY, endX);
        textOps.deleteRange(startY, startX, endY, endX);
        *ctx.cursorY = startY;
        *ctx.cursorX = startX;
        *ctx.dirty = true;
        ctx.needsFullRedraw = true;
        ctx.currentMode = Mode::INSERT;
    }
    else if(op == '=')
    {
        textOps.autoIndentRange(startY, endY);
        ctx.statusMessage =
            std::to_string(endY - startY + 1) + " lines indented";
    }
}

bool OperatorPending::getTextObjectRange(char objChar, bool around,
                                         int& outStartY, int& outStartX,
                                         int& outEndY, int& outEndX)
{
    switch(objChar)
    {
    case 'w':
        return findWordBoundaries(around, outStartY, outStartX, outEndY,
                                  outEndX);
    case 'W':
        return findWORDBoundaries(around, outStartY, outStartX, outEndY,
                                  outEndX);
    case '(':
    case ')':
    case 'b':
        return findMatchingPair('(', ')', around, outStartY, outStartX, outEndY,
                                outEndX);
    case '{':
    case '}':
    case 'B':
        return findMatchingPair('{', '}', around, outStartY, outStartX, outEndY,
                                outEndX);
    case '[':
    case ']':
        return findMatchingPair('[', ']', around, outStartY, outStartX, outEndY,
                                outEndX);
    case '<':
    case '>':
        return findMatchingPair('<', '>', around, outStartY, outStartX, outEndY,
                                outEndX);
    case '"':
        return findSurroundingQuotes('"', around, outStartY, outStartX, outEndY,
                                     outEndX);
    case '\'':
        return findSurroundingQuotes('\'', around, outStartY, outStartX,
                                     outEndY, outEndX);
    case '`':
        return findSurroundingQuotes('`', around, outStartY, outStartX, outEndY,
                                     outEndX);
    default:
        return false;
    }
}

bool OperatorPending::findWordBoundaries(bool around, int& startY, int& startX,
                                         int& endY, int& endX)
{
    if(*ctx.cursorY >= (int)ctx.lines->size())
        return false;

    const std::string& line = (*ctx.lines)[*ctx.cursorY];
    if(line.empty())
        return false;

    int x = *ctx.cursorX;
    if(x >= (int)line.length())
        x = line.length() - 1;

    startY = endY = *ctx.cursorY;

    // Find start of word
    startX = x;
    if(ctx.isWordChar(line[x]))
    {
        while(startX > 0 && ctx.isWordChar(line[startX - 1]))
        {
            startX--;
        }
    }
    else if(!std::isspace(static_cast<unsigned char>(line[x])))
    {
        while(startX > 0 && !ctx.isWordChar(line[startX - 1]) &&
              !std::isspace(static_cast<unsigned char>(line[startX - 1])))
        {
            startX--;
        }
    }

    // Find end of word
    endX = x;
    if(ctx.isWordChar(line[x]))
    {
        while(endX + 1 < (int)line.length() && ctx.isWordChar(line[endX + 1]))
        {
            endX++;
        }
    }
    else if(!std::isspace(static_cast<unsigned char>(line[x])))
    {
        while(endX + 1 < (int)line.length() &&
              !ctx.isWordChar(line[endX + 1]) &&
              !std::isspace(static_cast<unsigned char>(line[endX + 1])))
        {
            endX++;
        }
    }

    // Include surrounding whitespace for "around"
    if(around)
    {
        // Include trailing whitespace
        while(endX + 1 < (int)line.length() &&
              std::isspace(static_cast<unsigned char>(line[endX + 1])))
        {
            endX++;
        }
    }

    return true;
}

bool OperatorPending::findWORDBoundaries(bool around, int& startY, int& startX,
                                         int& endY, int& endX)
{
    if(*ctx.cursorY >= (int)ctx.lines->size())
        return false;

    const std::string& line = (*ctx.lines)[*ctx.cursorY];
    if(line.empty())
        return false;

    int x = *ctx.cursorX;
    if(x >= (int)line.length())
        x = line.length() - 1;

    startY = endY = *ctx.cursorY;

    // Find start of WORD (non-whitespace)
    startX = x;
    while(startX > 0 &&
          !std::isspace(static_cast<unsigned char>(line[startX - 1])))
    {
        startX--;
    }

    // Find end of WORD
    endX = x;
    while(endX + 1 < (int)line.length() &&
          !std::isspace(static_cast<unsigned char>(line[endX + 1])))
    {
        endX++;
    }

    // Include surrounding whitespace for "around"
    if(around)
    {
        while(endX + 1 < (int)line.length() &&
              std::isspace(static_cast<unsigned char>(line[endX + 1])))
        {
            endX++;
        }
    }

    return true;
}

bool OperatorPending::findMatchingPair(char openChar, char closeChar,
                                       bool around, int& startY, int& startX,
                                       int& endY, int& endX)
{
    // Search backwards for opening bracket
    int y = *ctx.cursorY;
    int x = *ctx.cursorX;
    int depth = 0;
    bool foundOpen = false;

    // First, check if we're on a bracket
    if(y < (int)ctx.lines->size() && x < (int)(*ctx.lines)[y].length())
    {
        char c = (*ctx.lines)[y][x];
        if(c == openChar)
        {
            foundOpen = true;
            startY = y;
            startX = x;
        }
        else if(c == closeChar)
        {
            // Search backwards for matching open
            depth = 1;
        }
    }

    if(!foundOpen)
    {
        // Search backwards
        while(y >= 0)
        {
            const std::string& line = (*ctx.lines)[y];
            while(x >= 0)
            {
                if(x < (int)line.length())
                {
                    char c = line[x];
                    if(c == closeChar)
                    {
                        depth++;
                    }
                    else if(c == openChar)
                    {
                        if(depth == 0)
                        {
                            foundOpen = true;
                            startY = y;
                            startX = x;
                            break;
                        }
                        depth--;
                    }
                }
                x--;
            }
            if(foundOpen)
                break;
            y--;
            if(y >= 0)
            {
                x = (*ctx.lines)[y].length() - 1;
            }
        }
    }

    if(!foundOpen)
        return false;

    // Now search forwards for closing bracket
    y = startY;
    x = startX + 1;
    depth = 1;
    bool foundClose = false;

    while(y < (int)ctx.lines->size())
    {
        const std::string& line = (*ctx.lines)[y];
        while(x < (int)line.length())
        {
            char c = line[x];
            if(c == openChar)
            {
                depth++;
            }
            else if(c == closeChar)
            {
                depth--;
                if(depth == 0)
                {
                    foundClose = true;
                    endY = y;
                    endX = x;
                    break;
                }
            }
            x++;
        }
        if(foundClose)
            break;
        y++;
        x = 0;
    }

    if(!foundClose)
        return false;

    // Adjust for "inner" vs "around"
    if(!around)
    {
        // Inner: exclude the brackets
        startX++;
        endX--;
        if(startX > endX && startY == endY)
        {
            // Empty brackets
            endX = startX - 1;
        }
    }

    return true;
}

bool OperatorPending::findSurroundingQuotes(char quoteChar, bool around,
                                            int& startY, int& startX, int& endY,
                                            int& endX)
{
    if(*ctx.cursorY >= (int)ctx.lines->size())
        return false;

    const std::string& line = (*ctx.lines)[*ctx.cursorY];
    int x = *ctx.cursorX;

    startY = endY = *ctx.cursorY;

    // Search backwards for opening quote
    int openQuote = -1;
    for(int i = x; i >= 0; i--)
    {
        if(line[i] == quoteChar)
        {
            // Check if escaped
            int backslashes = 0;
            for(int j = i - 1; j >= 0 && line[j] == '\\'; j--)
            {
                backslashes++;
            }
            if(backslashes % 2 == 0)
            {
                openQuote = i;
                break;
            }
        }
    }

    if(openQuote < 0)
        return false;

    // Search forwards for closing quote
    int closeQuote = -1;
    for(int i = openQuote + 1; i < (int)line.length(); i++)
    {
        if(line[i] == quoteChar)
        {
            int backslashes = 0;
            for(int j = i - 1; j >= 0 && line[j] == '\\'; j--)
            {
                backslashes++;
            }
            if(backslashes % 2 == 0)
            {
                closeQuote = i;
                break;
            }
        }
    }

    if(closeQuote < 0)
        return false;

    if(around)
    {
        startX = openQuote;
        endX = closeQuote;
    }
    else
    {
        startX = openQuote + 1;
        endX = closeQuote - 1;
        if(startX > endX)
        {
            endX = startX - 1;
        }
    }

    return true;
}
