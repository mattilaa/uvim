#include "text_operations.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

TextOperations::TextOperations(EditorContext& ctx) : ctx(ctx) {}

bool TextOperations::isCppFile() const
{
    if(ctx.filename->empty())
        return false;

    size_t dotPos = ctx.filename->find_last_of('.');
    if(dotPos == std::string::npos)
    {
        const std::string& path = *ctx.filename;
        if(path.find("/c++/") != std::string::npos ||
           path.find("/bits/") != std::string::npos ||
           path.find("/ext/") != std::string::npos ||
           path.find("/__") != std::string::npos)
        {
            return true;
        }
        return false;
    }

    std::string ext = ctx.filename->substr(dotPos);
    return (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".h" ||
            ext == ".hpp" || ext == ".hxx" || ext == ".c" || ext == ".C");
}

void TextOperations::insertChar(char c)
{
    if(*ctx.cursorY >= ctx.lines->size())
    {
        ctx.lines->push_back("");
    }

    (*ctx.lines)[*ctx.cursorY].insert(*ctx.cursorX, 1, c);
    (*ctx.cursorX)++;
    *ctx.dirty = true;
}

void TextOperations::insertNewline()
{
    if(*ctx.cursorY >= ctx.lines->size())
    {
        ctx.lines->push_back("");
        (*ctx.cursorY)++;
        *ctx.cursorX = 0;
        *ctx.dirty = true;
        ctx.needsFullRedraw = true;
        return;
    }

    const std::string& currentLine = (*ctx.lines)[*ctx.cursorY];

    // Find leading whitespace
    size_t indent = 0;
    while(indent < currentLine.length() &&
          (currentLine[indent] == ' ' || currentLine[indent] == '\t'))
    {
        indent++;
    }
    std::string indentStr = currentLine.substr(0, indent);

    // Check for opening brace (C/C++)
    bool addExtraIndent = false;
    if(isCppFile() && *ctx.cursorX > 0)
    {
        int checkPos = *ctx.cursorX - 1;
        while(checkPos >= 0 &&
              (currentLine[checkPos] == ' ' || currentLine[checkPos] == '\t'))
        {
            checkPos--;
        }
        if(checkPos >= 0 && currentLine[checkPos] == '{')
        {
            addExtraIndent = true;
        }
    }

    std::string beforeCursor = currentLine.substr(0, *ctx.cursorX);
    std::string afterCursor = currentLine.substr(*ctx.cursorX);

    (*ctx.lines)[*ctx.cursorY] = beforeCursor;

    std::string newLine = indentStr;
    if(addExtraIndent)
    {
        newLine += "    ";
    }

    // Trim leading whitespace from afterCursor
    size_t afterIndent = 0;
    while(afterIndent < afterCursor.length() &&
          (afterCursor[afterIndent] == ' ' || afterCursor[afterIndent] == '\t'))
    {
        afterIndent++;
    }
    if(afterIndent > 0)
    {
        newLine = indentStr;
        if(addExtraIndent)
        {
            newLine += "    ";
        }
        newLine += afterCursor.substr(afterIndent);
    }
    else
    {
        newLine += afterCursor;
    }

    ctx.lines->insert(ctx.lines->begin() + *ctx.cursorY + 1, newLine);
    (*ctx.cursorY)++;
    *ctx.cursorX = indentStr.length() + (addExtraIndent ? 4 : 0);
    *ctx.wantedX = *ctx.cursorX;
    *ctx.dirty = true;
    ctx.needsFullRedraw = true;
}

void TextOperations::insertTab()
{
    // Insert 4 spaces
    for(int i = 0; i < 4; i++)
    {
        insertChar(' ');
    }
}

void TextOperations::deleteChar()
{
    if(*ctx.cursorX > 0)
    {
        (*ctx.lines)[*ctx.cursorY].erase(*ctx.cursorX - 1, 1);
        (*ctx.cursorX)--;
        *ctx.dirty = true;
    }
    else if(*ctx.cursorY > 0)
    {
        int prevLineLen = (*ctx.lines)[*ctx.cursorY - 1].length();
        (*ctx.lines)[*ctx.cursorY - 1] += (*ctx.lines)[*ctx.cursorY];
        ctx.lines->erase(ctx.lines->begin() + *ctx.cursorY);
        (*ctx.cursorY)--;
        *ctx.cursorX = prevLineLen;
        *ctx.dirty = true;
        ctx.needsFullRedraw = true;
    }
}

void TextOperations::deleteCharForward()
{
    if(*ctx.cursorY >= ctx.lines->size())
        return;

    const std::string& line = (*ctx.lines)[*ctx.cursorY];
    if(*ctx.cursorX < (int)line.length())
    {
        (*ctx.lines)[*ctx.cursorY].erase(*ctx.cursorX, 1);
        *ctx.dirty = true;
    }
    else if(*ctx.cursorY < (int)ctx.lines->size() - 1)
    {
        (*ctx.lines)[*ctx.cursorY] += (*ctx.lines)[*ctx.cursorY + 1];
        ctx.lines->erase(ctx.lines->begin() + *ctx.cursorY + 1);
        *ctx.dirty = true;
        ctx.needsFullRedraw = true;
    }
}

void TextOperations::deleteLine()
{
    if(ctx.lines->empty())
        return;

    ctx.yankBuffer = (*ctx.lines)[*ctx.cursorY] + "\n";

    if(ctx.lines->size() == 1)
    {
        (*ctx.lines)[0].clear();
    }
    else
    {
        ctx.lines->erase(ctx.lines->begin() + *ctx.cursorY);
        if(*ctx.cursorY >= (int)ctx.lines->size())
        {
            *ctx.cursorY = ctx.lines->size() - 1;
        }
    }

    const std::string& line = (*ctx.lines)[*ctx.cursorY];
    *ctx.cursorX = 0;
    while(*ctx.cursorX < (int)line.length() &&
          (line[*ctx.cursorX] == ' ' || line[*ctx.cursorX] == '\t'))
    {
        (*ctx.cursorX)++;
    }
    *ctx.wantedX = *ctx.cursorX;
    *ctx.dirty = true;
    ctx.needsFullRedraw = true;
}

void TextOperations::deleteToLineEnd()
{
    if(*ctx.cursorY >= ctx.lines->size())
        return;

    if(*ctx.cursorX < (*ctx.lines)[*ctx.cursorY].length())
    {
        ctx.yankBuffer = (*ctx.lines)[*ctx.cursorY].substr(*ctx.cursorX);
        (*ctx.lines)[*ctx.cursorY] =
            (*ctx.lines)[*ctx.cursorY].substr(0, *ctx.cursorX);
        *ctx.dirty = true;
    }
}

void TextOperations::deleteWord()
{
    // Implementation for dw
    // TODO: Implement using moveWordForward logic
}

void TextOperations::yankLine()
{
    if(*ctx.cursorY < ctx.lines->size())
    {
        ctx.yankBuffer = (*ctx.lines)[*ctx.cursorY] + "\n";
        ctx.statusMessage = "Line yanked";
    }
}

void TextOperations::yankToLineEnd()
{
    if(*ctx.cursorY < ctx.lines->size() &&
       *ctx.cursorX < (*ctx.lines)[*ctx.cursorY].length())
    {
        ctx.yankBuffer = (*ctx.lines)[*ctx.cursorY].substr(*ctx.cursorX);
        ctx.statusMessage = "Yanked to end of line";
    }
}

void TextOperations::yankSelection()
{
    if(ctx.currentMode != Mode::VISUAL &&
       ctx.currentMode != Mode::VISUAL_LINE &&
       ctx.currentMode != Mode::VISUAL_BLOCK)
        return;

    // Get selection bounds
    int startY = std::min(ctx.currentBuffer->visualStartY,
                          ctx.currentBuffer->visualEndY);
    int endY = std::max(ctx.currentBuffer->visualStartY,
                        ctx.currentBuffer->visualEndY);
    int startX = ctx.currentBuffer->visualStartX;
    int endX = ctx.currentBuffer->visualEndX;

    if(ctx.currentBuffer->visualStartY > ctx.currentBuffer->visualEndY ||
       (ctx.currentBuffer->visualStartY == ctx.currentBuffer->visualEndY &&
        ctx.currentBuffer->visualStartX > ctx.currentBuffer->visualEndX))
    {
        std::swap(startX, endX);
    }

    ctx.yankBuffer.clear();

    if(ctx.currentMode == Mode::VISUAL_LINE)
    {
        for(int y = startY; y <= endY && y < (int)ctx.lines->size(); y++)
        {
            ctx.yankBuffer += (*ctx.lines)[y] + "\n";
        }
        ctx.statusMessage = std::to_string(endY - startY + 1) + " lines yanked";
    }
    else
    {
        if(startY == endY)
        {
            ctx.yankBuffer =
                (*ctx.lines)[startY].substr(startX, endX - startX + 1);
        }
        else
        {
            ctx.yankBuffer = (*ctx.lines)[startY].substr(startX) + "\n";
            for(int y = startY + 1; y < endY; y++)
            {
                ctx.yankBuffer += (*ctx.lines)[y] + "\n";
            }
            ctx.yankBuffer += (*ctx.lines)[endY].substr(0, endX + 1);
        }
        ctx.statusMessage = "Selection yanked";
    }
}

void TextOperations::yankRange(int startY, int startX, int endY, int endX)
{
    ctx.yankBuffer.clear();
    if(startY == endY)
    {
        const std::string& line = (*ctx.lines)[startY];
        if(startX <= endX && startX < line.length())
            ctx.yankBuffer = line.substr(startX, endX - startX + 1);
    }
    else
    {
        const std::string& firstLine = (*ctx.lines)[startY];
        if(startX < firstLine.length())
            ctx.yankBuffer = firstLine.substr(startX);
        ctx.yankBuffer += "\n";
        for(int r = startY + 1; r < endY; ++r)
        {
            ctx.yankBuffer += (*ctx.lines)[r];
            ctx.yankBuffer += "\n";
        }
        const std::string& lastLine = (*ctx.lines)[endY];
        if(endX < lastLine.length())
            ctx.yankBuffer += lastLine.substr(0, endX + 1);
        else
            ctx.yankBuffer += lastLine;
    }
}

std::string TextOperations::getSystemClipboard()
{
    std::string result;
    FILE* pipe = popen("pbpaste 2>/dev/null || xclip -selection clipboard -o "
                       "2>/dev/null || xsel -b -o 2>/dev/null",
                       "r");
    if(pipe)
    {
        char buffer[4096];
        while(fgets(buffer, sizeof(buffer), pipe))
        {
            result += buffer;
        }
        pclose(pipe);
    }
    return result;
}

void TextOperations::setSystemClipboard(const std::string& text)
{
    FILE* pipe = popen("pbcopy 2>/dev/null || xclip -selection clipboard "
                       "2>/dev/null || xsel -b -i 2>/dev/null",
                       "w");
    if(pipe)
    {
        fwrite(text.c_str(), 1, text.length(), pipe);
        pclose(pipe);
    }
}

void TextOperations::yankToSystemClipboard()
{
    yankSelection();
    if(!ctx.yankBuffer.empty())
    {
        setSystemClipboard(ctx.yankBuffer);
        ctx.statusMessage = "Yanked to system clipboard";
    }
}

void TextOperations::pasteFromSystemClipboard()
{
    std::string clipboard = getSystemClipboard();
    if(clipboard.empty())
    {
        ctx.statusMessage = "System clipboard empty";
        return;
    }

    ctx.yankBuffer = clipboard;
    pasteAfter();
    ctx.statusMessage = "Pasted from system clipboard";
}

void TextOperations::pasteAfter()
{
    if(ctx.yankBuffer.empty())
        return;

    bool lineWise = !ctx.yankBuffer.empty() && ctx.yankBuffer.back() == '\n';

    if(lineWise)
    {
        std::istringstream iss(ctx.yankBuffer);
        std::string line;
        int insertPos = *ctx.cursorY + 1;
        while(std::getline(iss, line))
        {
            if(!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            ctx.lines->insert(ctx.lines->begin() + insertPos, line);
            insertPos++;
        }
        (*ctx.cursorY)++;
        *ctx.cursorX = 0;
        const std::string& newLine = (*ctx.lines)[*ctx.cursorY];
        while(*ctx.cursorX < (int)newLine.length() &&
              (newLine[*ctx.cursorX] == ' ' || newLine[*ctx.cursorX] == '\t'))
        {
            (*ctx.cursorX)++;
        }
    }
    else
    {
        if(*ctx.cursorY < ctx.lines->size())
        {
            int insertPos = *ctx.cursorX;
            if(!(*ctx.lines)[*ctx.cursorY].empty())
            {
                insertPos = std::min(insertPos + 1,
                                     (int)(*ctx.lines)[*ctx.cursorY].length());
            }
            (*ctx.lines)[*ctx.cursorY].insert(insertPos, ctx.yankBuffer);
            *ctx.cursorX = insertPos + ctx.yankBuffer.length() - 1;
        }
    }

    *ctx.wantedX = *ctx.cursorX;
    *ctx.dirty = true;
    ctx.needsFullRedraw = true;
}

void TextOperations::pasteBefore()
{
    if(ctx.yankBuffer.empty())
        return;

    bool lineWise = !ctx.yankBuffer.empty() && ctx.yankBuffer.back() == '\n';

    if(lineWise)
    {
        std::istringstream iss(ctx.yankBuffer);
        std::string line;
        int insertPos = *ctx.cursorY;
        while(std::getline(iss, line))
        {
            if(!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            ctx.lines->insert(ctx.lines->begin() + insertPos, line);
            insertPos++;
        }
        *ctx.cursorX = 0;
        const std::string& newLine = (*ctx.lines)[*ctx.cursorY];
        while(*ctx.cursorX < (int)newLine.length() &&
              (newLine[*ctx.cursorX] == ' ' || newLine[*ctx.cursorX] == '\t'))
        {
            (*ctx.cursorX)++;
        }
    }
    else
    {
        if(*ctx.cursorY < ctx.lines->size())
        {
            (*ctx.lines)[*ctx.cursorY].insert(*ctx.cursorX, ctx.yankBuffer);
            *ctx.cursorX += ctx.yankBuffer.length() - 1;
        }
    }

    *ctx.wantedX = *ctx.cursorX;
    *ctx.dirty = true;
    ctx.needsFullRedraw = true;
}

void TextOperations::deleteRange(int startY, int startX, int endY, int endX)
{
    if(startY > endY || (startY == endY && startX > endX))
    {
        std::swap(startY, endY);
        std::swap(startX, endX);
    }

    if(startY < 0 || startY >= (int)ctx.lines->size())
        return;
    if(endY < 0 || endY >= (int)ctx.lines->size())
        endY = ctx.lines->size() - 1;

    if(startY == endY)
    {
        std::string& line = (*ctx.lines)[startY];
        if(startX < (int)line.length())
        {
            int len = std::min(endX - startX + 1, (int)line.length() - startX);
            line.erase(startX, len);
        }
    }
    else
    {
        std::string newLine = (*ctx.lines)[startY].substr(0, startX);
        if(endX + 1 < (int)(*ctx.lines)[endY].length())
        {
            newLine += (*ctx.lines)[endY].substr(endX + 1);
        }
        (*ctx.lines)[startY] = newLine;

        for(int i = endY; i > startY; i--)
        {
            ctx.lines->erase(ctx.lines->begin() + i);
        }
    }

    *ctx.dirty = true;
    ctx.needsFullRedraw = true;
}

void TextOperations::deleteSelection()
{
    if(ctx.currentMode != Mode::VISUAL &&
       ctx.currentMode != Mode::VISUAL_LINE &&
       ctx.currentMode != Mode::VISUAL_BLOCK)
        return;

    yankSelection();

    int startY = std::min(ctx.currentBuffer->visualStartY,
                          ctx.currentBuffer->visualEndY);
    int endY = std::max(ctx.currentBuffer->visualStartY,
                        ctx.currentBuffer->visualEndY);
    int startX = ctx.currentBuffer->visualStartX;
    int endX = ctx.currentBuffer->visualEndX;

    if(ctx.currentBuffer->visualStartY > ctx.currentBuffer->visualEndY ||
       (ctx.currentBuffer->visualStartY == ctx.currentBuffer->visualEndY &&
        ctx.currentBuffer->visualStartX > ctx.currentBuffer->visualEndX))
    {
        std::swap(startX, endX);
    }

    if(ctx.currentMode == Mode::VISUAL_LINE)
    {
        for(int i = endY; i >= startY; i--)
        {
            if(ctx.lines->size() > 1)
            {
                ctx.lines->erase(ctx.lines->begin() + i);
            }
            else
            {
                (*ctx.lines)[0].clear();
            }
        }
        *ctx.cursorY = std::min(startY, (int)ctx.lines->size() - 1);
        *ctx.cursorX = 0;
    }
    else
    {
        if(startY == endY)
        {
            (*ctx.lines)[startY].erase(startX, endX - startX + 1);
        }
        else
        {
            (*ctx.lines)[startY] = (*ctx.lines)[startY].substr(0, startX) +
                                   (*ctx.lines)[endY].substr(endX + 1);
            for(int i = endY; i > startY; i--)
            {
                ctx.lines->erase(ctx.lines->begin() + i);
            }
        }
        *ctx.cursorY = startY;
        *ctx.cursorX = startX;
    }

    *ctx.wantedX = *ctx.cursorX;
    *ctx.dirty = true;
    ctx.needsFullRedraw = true;
}

int TextOperations::getLineIndent(int line)
{
    if(line < 0 || line >= (int)ctx.lines->size())
        return 0;

    const std::string& lineStr = (*ctx.lines)[line];
    int indent = 0;
    for(char c : lineStr)
    {
        if(c == ' ')
            indent++;
        else if(c == '\t')
            indent += 4;
        else
            break;
    }
    return indent;
}

void TextOperations::indentLine(int line, int spaces)
{
    if(line < 0 || line >= (int)ctx.lines->size())
        return;

    std::string& lineStr = (*ctx.lines)[line];

    size_t firstNonSpace = 0;
    while(firstNonSpace < lineStr.length() &&
          (lineStr[firstNonSpace] == ' ' || lineStr[firstNonSpace] == '\t'))
    {
        firstNonSpace++;
    }

    std::string content = lineStr.substr(firstNonSpace);
    lineStr = std::string(spaces, ' ') + content;
    *ctx.dirty = true;
}

void TextOperations::autoIndentLine(int line)
{
    if(line <= 0 || line >= (int)ctx.lines->size())
        return;

    int prevLine = line - 1;
    while(prevLine >= 0)
    {
        const std::string& pl = (*ctx.lines)[prevLine];
        bool hasContent = false;
        for(char c : pl)
        {
            if(c != ' ' && c != '\t')
            {
                hasContent = true;
                break;
            }
        }
        if(hasContent)
            break;
        prevLine--;
    }

    if(prevLine < 0)
        return;

    int indent = getLineIndent(prevLine);

    const std::string& prevLineStr = (*ctx.lines)[prevLine];
    size_t lastNonSpace = prevLineStr.find_last_not_of(" \t");
    if(lastNonSpace != std::string::npos && prevLineStr[lastNonSpace] == '{')
    {
        indent += 4;
    }

    const std::string& currLine = (*ctx.lines)[line];
    size_t firstNonSpace = currLine.find_first_not_of(" \t");
    if(firstNonSpace != std::string::npos && currLine[firstNonSpace] == '}')
    {
        indent = std::max(0, indent - 4);
    }

    indentLine(line, indent);
}

void TextOperations::autoIndentRange(int startLine, int endLine)
{
    for(int i = startLine; i <= endLine; i++)
    {
        autoIndentLine(i);
    }
    ctx.needsFullRedraw = true;
}

void TextOperations::shiftLineRight(int line, int spaces)
{
    if(line < 0 || line >= (int)ctx.lines->size())
        return;

    (*ctx.lines)[line] = std::string(spaces, ' ') + (*ctx.lines)[line];
    *ctx.dirty = true;
}

void TextOperations::shiftLineLeft(int line, int spaces)
{
    if(line < 0 || line >= (int)ctx.lines->size())
        return;

    std::string& lineStr = (*ctx.lines)[line];
    int removed = 0;
    while(removed < spaces && !lineStr.empty() &&
          (lineStr[0] == ' ' || lineStr[0] == '\t'))
    {
        if(lineStr[0] == '\t')
            removed += 4;
        else
            removed++;
        lineStr.erase(0, 1);
    }
    *ctx.dirty = true;
}

void TextOperations::joinLines(int count)
{
    for(int i = 0; i < count && *ctx.cursorY < (int)ctx.lines->size() - 1; i++)
    {
        std::string& currentLine = (*ctx.lines)[*ctx.cursorY];
        const std::string& nextLine = (*ctx.lines)[*ctx.cursorY + 1];

        // Find first non-whitespace in next line
        size_t nextStart = 0;
        while(nextStart < nextLine.length() &&
              (nextLine[nextStart] == ' ' || nextLine[nextStart] == '\t'))
        {
            nextStart++;
        }

        // Add space if needed
        if(!currentLine.empty() && currentLine.back() != ' ')
        {
            currentLine += ' ';
        }

        currentLine += nextLine.substr(nextStart);
        ctx.lines->erase(ctx.lines->begin() + *ctx.cursorY + 1);
    }

    *ctx.dirty = true;
    ctx.needsFullRedraw = true;
}

void TextOperations::toggleCase()
{
    if(*ctx.cursorY >= ctx.lines->size())
        return;

    std::string& line = (*ctx.lines)[*ctx.cursorY];
    if(*ctx.cursorX < (int)line.length())
    {
        char& c = line[*ctx.cursorX];
        if(std::islower(static_cast<unsigned char>(c)))
            c = std::toupper(static_cast<unsigned char>(c));
        else if(std::isupper(static_cast<unsigned char>(c)))
            c = std::tolower(static_cast<unsigned char>(c));
        *ctx.dirty = true;
    }
}

void TextOperations::toUpperCase()
{
    if(*ctx.cursorY >= ctx.lines->size())
        return;

    std::string& line = (*ctx.lines)[*ctx.cursorY];
    if(*ctx.cursorX < (int)line.length())
    {
        line[*ctx.cursorX] =
            std::toupper(static_cast<unsigned char>(line[*ctx.cursorX]));
        *ctx.dirty = true;
    }
}

void TextOperations::toLowerCase()
{
    if(*ctx.cursorY >= ctx.lines->size())
        return;

    std::string& line = (*ctx.lines)[*ctx.cursorY];
    if(*ctx.cursorX < (int)line.length())
    {
        line[*ctx.cursorX] =
            std::tolower(static_cast<unsigned char>(line[*ctx.cursorX]));
        *ctx.dirty = true;
    }
}
