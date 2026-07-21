#include "editor.h"
#include "enablelog.h"
#include "process_pipe.h"
#include "text_utils.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

void Editor::yankRange(int startY, int startX, int endY, int endX)
{
    yankBuffer.clear();
    if(startY == endY)
    {
        const std::string& line = (*lines)[startY];
        if(startX <= endX && startX < line.length())
            yankBuffer = line.substr(startX, endX - startX + 1);
    }
    else
    {
        const std::string& firstLine = (*lines)[startY];
        if(startX < firstLine.length())
            yankBuffer = firstLine.substr(startX);
        yankBuffer += "\n";
        for(int r = startY + 1; r < endY; ++r)
        {
            yankBuffer += (*lines)[r];
            yankBuffer += "\n";
        }
        const std::string& lastLine = (*lines)[endY];
        if(endX >= 0 && endX < lastLine.length())
            yankBuffer += lastLine.substr(0, endX + 1);
    }
    setStatusMessage("Yanked");
}

void Editor::deleteRange(int startY, int startX, int endY, int endX)
{
    if(startY == endY)
    {
        std::string& line = (*lines)[startY];
        if(startX <= endX && startX < line.length())
        {
            line.erase(startX, endX - startX + 1);
        }
    }
    else
    {
        std::string prefix = (*lines)[startY].substr(0, startX);
        std::string suffix = "";
        if(endX + 1 < (*lines)[endY].length())
            suffix = (*lines)[endY].substr(endX + 1);
        lines->erase(lines->begin() + startY, lines->begin() + endY + 1);
        lines->insert(lines->begin() + startY, prefix + suffix);
    }

    if(currentBuffer && currentBuffer->blameValid && startY != endY &&
       startY < (int)currentBuffer->blameEntries.size())
    {
        int removeCount =
            std::min((endY - startY + 1),
                     (int)currentBuffer->blameEntries.size() - startY);
        currentBuffer->blameEntries.erase(
            currentBuffer->blameEntries.begin() + startY,
            currentBuffer->blameEntries.begin() + startY + removeCount);
        currentBuffer->blameStart = 0;
        currentBuffer->blameEnd = (int)currentBuffer->blameEntries.size() - 1;
    }

    if(lines->empty())
        lines->push_back("");
    if(*cursorY >= lines->size())
        *cursorY = lines->size() - 1;
    if(*cursorX > (*lines)[*cursorY].length())
        *cursorX = (*lines)[*cursorY].length();
    needsFullRedraw = true;
}

void Editor::insertChar(char c)
{
    if(*cursorY >= lines->size())
    {
        lines->push_back("");
    }

    (*lines)[*cursorY].insert(*cursorX, 1, c);
    (*cursorX)++;
    *dirty = true;
    if(currentBuffer)
        currentBuffer->invalidateSyntaxCache();
}

void Editor::insertNewline()
{
    if(*cursorY >= lines->size())
    {
        lines->push_back("");
        (*cursorY)++;
        *cursorX = 0;
        *dirty = true;
        if(currentBuffer)
            currentBuffer->invalidateSyntaxCache();
        needsFullRedraw = true;
        return;
    }

    const std::string& currentLine = (*lines)[*cursorY];
    auto leading_ws_len = [](const std::string& s) -> size_t
    {
        size_t i = 0;
        while(i < s.size() && (s[i] == ' ' || s[i] == '\t'))
            ++i;
        return i;
    };
    auto ltrim = [&](const std::string& s) -> std::string
    {
        size_t i = leading_ws_len(s);
        return s.substr(i);
    };
    auto starts_with_kw = [](const std::string& s) -> bool
    {
        auto starts = [&](const char* kw) -> bool
        {
            size_t n = std::strlen(kw);
            if(s.size() < n)
                return false;
            if(s.compare(0, n, kw) != 0)
                return false;
            if(s.size() == n)
                return true;
            char next = s[n];
            return std::isspace((unsigned char)next) || next == ';';
        };
        return starts("return") || starts("break") || starts("continue") ||
               starts("throw") || starts("goto");
    };
    auto starts_control = [](const std::string& s) -> bool
    {
        auto starts = [&](const char* kw) -> bool
        {
            size_t n = std::strlen(kw);
            if(s.size() < n)
                return false;
            if(s.compare(0, n, kw) != 0)
                return false;
            if(s.size() == n)
                return true;
            char next = s[n];
            return std::isspace((unsigned char)next) || next == '(';
        };
        return starts("if") || starts("for") || starts("while") ||
               starts("else") || starts("switch");
    };

    if(autoTags &&
       (isFileType<FileType::Html>() || isFileType<FileType::Xml>()))
    {
        int cx = *cursorX;
        if(cx > 0 && cx <= (int)currentLine.size())
        {
            size_t gt = currentLine.rfind('>', (size_t)(cx - 1));
            if(text_utils::is_found(gt))
            {
                size_t lt = currentLine.rfind('<', gt);
                if(text_utils::is_found(lt) && lt + 1 < currentLine.size())
                {
                    char next = currentLine[lt + 1];
                    if(next != '/' && next != '!' && next != '?')
                    {
                        size_t nameStart = lt + 1;
                        while(nameStart < gt &&
                              (currentLine[nameStart] == ' ' ||
                               currentLine[nameStart] == '\t'))
                        {
                            ++nameStart;
                        }
                        size_t nameEnd = nameStart;
                        auto isTagChar = [](char ch)
                        {
                            return text_utils::is_alnum(ch) || ch == ':' ||
                                   ch == '_' || ch == '-';
                        };
                        while(nameEnd < gt && isTagChar(currentLine[nameEnd]))
                            ++nameEnd;
                        if(nameEnd > nameStart)
                        {
                            std::string openTag = currentLine.substr(
                                nameStart, nameEnd - nameStart);
                            size_t closeStart =
                                currentLine.find("</", (size_t)cx);
                            if(text_utils::is_found(closeStart))
                            {
                                size_t closeNameStart = closeStart + 2;
                                size_t closeNameEnd = closeNameStart;
                                while(closeNameEnd < currentLine.size() &&
                                      isTagChar(currentLine[closeNameEnd]))
                                    ++closeNameEnd;
                                if(closeNameEnd > closeNameStart)
                                {
                                    std::string closeTag = currentLine.substr(
                                        closeNameStart,
                                        closeNameEnd - closeNameStart);
                                    if(closeTag == openTag)
                                    {
                                        bool onlySpace = true;
                                        for(size_t k = (size_t)cx;
                                            k < closeStart; ++k)
                                        {
                                            char ch = currentLine[k];
                                            if(ch != ' ' && ch != '\t')
                                            {
                                                onlySpace = false;
                                                break;
                                            }
                                        }
                                        if(onlySpace)
                                        {
                                            size_t indent =
                                                leading_ws_len(currentLine);
                                            std::string indentStr =
                                                currentLine.substr(0, indent);
                                            std::string innerIndent =
                                                indentStr +
                                                std::string(tabSpaces, ' ');
                                            std::string before =
                                                currentLine.substr(0, gt + 1);
                                            std::string after =
                                                currentLine.substr(closeStart);
                                            (*lines)[*cursorY] = before;
                                            lines->insert(lines->begin() +
                                                              *cursorY + 1,
                                                          innerIndent);
                                            lines->insert(lines->begin() +
                                                              *cursorY + 2,
                                                          indentStr + after);
                                            (*cursorY)++;
                                            *cursorX = innerIndent.length();
                                            *dirty = true;
                                            needsFullRedraw = true;
                                            return;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    size_t indent = leading_ws_len(currentLine);
    std::string indentStr = currentLine.substr(0, indent);

    bool addExtraIndent = false;
    int extraIndentWidth = tabSpaces;
    if(isFileType<FileType::Cpp>() && *cursorX > 0)
    {
        int checkPos = *cursorX - 1;
        while(checkPos >= 0 &&
              (currentLine[checkPos] == ' ' || currentLine[checkPos] == '\t'))
        {
            checkPos--;
        }
        if(checkPos >= 0 && currentLine[checkPos] == '{')
        {
            addExtraIndent = true;
            extraIndentWidth = indentWidthForBraces();
        }
    }

    std::string remainder;
    if(*cursorX < currentLine.length())
    {
        remainder = currentLine.substr(*cursorX);
        size_t startPos = remainder.find_first_not_of(" \t");
        if(text_utils::is_found(startPos))
        {
            remainder = remainder.substr(startPos);
        }
        else
        {
            remainder = "";
        }
        (*lines)[*cursorY] = currentLine.substr(0, *cursorX);
    }

    if(isFileType<FileType::Cpp>() && !addExtraIndent && remainder.empty())
    {
        std::string trimmed = ltrim(currentLine);
        if(starts_with_kw(trimmed))
        {
            bool adjusted = false;
            for(int y = *cursorY - 1; y >= 0; --y)
            {
                const std::string& prevLine = (*lines)[y];
                std::string prevTrim = ltrim(prevLine);
                if(prevTrim.empty())
                    continue;
                size_t prevIndent = leading_ws_len(prevLine);
                if(prevIndent < indent)
                {
                    if(starts_control(prevTrim) &&
                       !text_utils::contains(prevTrim, '{'))
                    {
                        indentStr = prevLine.substr(0, prevIndent);
                        adjusted = true;
                    }
                    break;
                }
            }
            if(!adjusted && !indentStr.empty())
            {
                if(indentStr.back() == '\t')
                {
                    indentStr.pop_back();
                }
                else if(indentStr.length() >= 4)
                {
                    indentStr.erase(indentStr.length() - 4);
                }
                else
                {
                    indentStr.clear();
                }
            }
        }
    }

    std::string newLine = indentStr;
    if(addExtraIndent)
    {
        newLine.append(extraIndentWidth, ' ');
    }
    newLine += remainder;

    lines->insert(lines->begin() + *cursorY + 1, newLine);

    (*cursorY)++;
    *cursorX =
        (int)indentStr.length() + (addExtraIndent ? extraIndentWidth : 0);
    *dirty = true;
    if(currentBuffer)
        currentBuffer->invalidateSyntaxCache();
    needsFullRedraw = true;
}

void Editor::deleteChar()
{
    if(*cursorY >= lines->size())
        return;
    if(*cursorX == 0 && *cursorY == 0)
        return;

    if(*cursorX > 0)
    {
        std::string& line = (*lines)[*cursorY];
        if(utf8Mode)
        {
            int start = text_utils::prevUtf8CharStart(line, *cursorX);
            int len = *cursorX - start;
            if(len <= 0)
                return;
            line.erase(start, len);
            *cursorX = start;
        }
        else
        {
            line.erase(*cursorX - 1, 1);
            (*cursorX)--;
        }
    }
    else
    {
        *cursorX = (*lines)[*cursorY - 1].length();
        (*lines)[*cursorY - 1] += (*lines)[*cursorY];
        lines->erase(lines->begin() + *cursorY);
        if(currentBuffer && currentBuffer->blameValid &&
           *cursorY < (int)currentBuffer->blameEntries.size())
        {
            currentBuffer->blameEntries.erase(
                currentBuffer->blameEntries.begin() + *cursorY);
            currentBuffer->blameStart = 0;
            currentBuffer->blameEnd =
                (int)currentBuffer->blameEntries.size() - 1;
        }
        (*cursorY)--;
        needsFullRedraw = true;
    }
    *dirty = true;
    if(currentBuffer)
        currentBuffer->invalidateSyntaxCache();
}

void Editor::deleteCharForward()
{
    if(*cursorY >= lines->size())
        return;

    if(*cursorX < (*lines)[*cursorY].length())
    {
        std::string& line = (*lines)[*cursorY];
        if(utf8Mode)
        {
            int start = *cursorX;
            if(start > 0 && start < (int)line.size())
            {
                unsigned char c = (unsigned char)line[start];
                if((c & 0xC0) == 0x80)
                    start = text_utils::prevUtf8CharStart(line, start);
            }
            int end = text_utils::nextUtf8CharStart(line, start);
            int len = end - start;
            if(len <= 0)
                return;
            line.erase(start, len);
            *cursorX = start;
        }
        else
        {
            line.erase(*cursorX, 1);
        }
    }
    else if(*cursorY < lines->size() - 1)
    {
        (*lines)[*cursorY] += (*lines)[*cursorY + 1];
        lines->erase(lines->begin() + *cursorY + 1);
        if(currentBuffer && currentBuffer->blameValid &&
           *cursorY + 1 < (int)currentBuffer->blameEntries.size())
        {
            currentBuffer->blameEntries.erase(
                currentBuffer->blameEntries.begin() + *cursorY + 1);
            currentBuffer->blameStart = 0;
            currentBuffer->blameEnd =
                (int)currentBuffer->blameEntries.size() - 1;
        }
        needsFullRedraw = true;
    }
    *dirty = true;
    if(currentBuffer)
        currentBuffer->invalidateSyntaxCache();
}

void Editor::deleteLine()
{
    if(lines->empty())
        return;

    yankLine();

    lines->erase(lines->begin() + *cursorY);
    if(lines->empty())
    {
        lines->push_back("");
    }

    if(currentBuffer)
        currentBuffer->blameValid = false;

    if(*cursorY >= lines->size())
    {
        *cursorY = lines->size() - 1;
    }

    *cursorX = 0;
    *dirty = true;
    if(currentBuffer)
        currentBuffer->invalidateSyntaxCache();
    needsFullRedraw = true;
}

void Editor::deleteToLineEnd()
{
    if(*cursorY >= lines->size())
        return;

    if(*cursorX < (*lines)[*cursorY].length())
    {
        yankBuffer = (*lines)[*cursorY].substr(*cursorX);
        (*lines)[*cursorY] = (*lines)[*cursorY].substr(0, *cursorX);
        *dirty = true;
    }
}

void Editor::yankLine()
{
    if(*cursorY < lines->size())
    {
        yankBuffer = (*lines)[*cursorY] + "\n";

        std::string msg = "Line yanked";
        if(useSystemClipboard && !yankBuffer.empty())
        {
            setSystemClipboard(yankBuffer);
            msg += " (copied to clipboard)";
        }
        setStatusMessage(msg);
    }
}

void Editor::yankToLineEnd()
{
    if(*cursorY < lines->size() && *cursorX < (*lines)[*cursorY].length())
    {
        yankBuffer = (*lines)[*cursorY].substr(*cursorX);
        setStatusMessage("Yanked to line end");

        if(useSystemClipboard && !yankBuffer.empty())
        {
            setSystemClipboard(yankBuffer);
        }
    }
}

void Editor::yankSelection()
{
    yankBuffer.clear();

    if(currentMode == VISUAL_LINE)
    {
        int startY =
            std::min(currentBuffer->visualStartY, currentBuffer->visualEndY);
        int endY =
            std::max(currentBuffer->visualStartY, currentBuffer->visualEndY);

        for(int i = startY; i <= endY; i++)
        {
            yankBuffer += (*lines)[i] + "\n";
        }
        setStatusMessage(std::to_string(endY - startY + 1) + " lines yanked");
    }
    else
    {
        int startY, startX, endY, endX;
        getSelectionBounds(startY, startX, endY, endX);

        if(startY == endY)
        {
            yankBuffer = (*lines)[startY].substr(startX, endX - startX + 1);
        }
        else
        {
            yankBuffer = (*lines)[startY].substr(startX) + "\n";
            for(int i = startY + 1; i < endY; i++)
            {
                yankBuffer += (*lines)[i] + "\n";
            }
            yankBuffer += (*lines)[endY].substr(0, endX + 1);
        }
        setStatusMessage("Selection yanked");
    }

    if(useSystemClipboard && !yankBuffer.empty())
    {
        setSystemClipboard(yankBuffer);
    }
}

// System clipboard support
static std::vector<std::string> getClipboardCommand()
{
#ifdef __APPLE__
    return {"pbpaste"};
#elif defined(_WIN32)
    // TODO: Win32 clipboard via OpenClipboard/GetClipboardData(CF_UNICODETEXT)
    return {};
#else
    if(system("which xclip > /dev/null 2>&1") == 0)
        return {"xclip", "-selection", "clipboard", "-o"};
    if(system("which xsel > /dev/null 2>&1") == 0)
        return {"xsel", "--clipboard", "--output"};
    return {};
#endif
}

static std::vector<std::string> setClipboardCommand()
{
#ifdef __APPLE__
    return {"pbcopy"};
#elif defined(_WIN32)
    // TODO: Win32 clipboard via OpenClipboard/SetClipboardData
    return {};
#else
    if(system("which xclip > /dev/null 2>&1") == 0)
        return {"xclip", "-selection", "clipboard"};
    if(system("which xsel > /dev/null 2>&1") == 0)
        return {"xsel", "--clipboard", "--input"};
    return {};
#endif
}

std::string Editor::getSystemClipboard()
{
    auto cmd = getClipboardCommand();
    if(cmd.empty())
        return "";

    ProcessPipe pipe(cmd);
    if(!pipe)
        return "";

    return pipe.readAll(256);
}

void Editor::setSystemClipboard(const std::string& text)
{
    LOG_DEBUG(
        LOG,
        "setSystemClipboard called, text.length()={}, useSystemClipboard={}",
        text.length(), useSystemClipboard);

    if(text.empty())
    {
        LOG_DEBUG(LOG, "setSystemClipboard: text is empty, returning");
        return;
    }

    auto cmd = setClipboardCommand();
    if(cmd.empty())
        return;

    ProcessPipe pipe(cmd, "w");
    if(!pipe)
        return;

    pipe.write(text);
    pipe.flush();
    pipe.close();
}

void Editor::yankToSystemClipboard()
{
    if(yankBuffer.empty())
    {
        yankLine();
    }
    setSystemClipboard(yankBuffer);
    setStatusMessage("Yanked to system clipboard");
}

void Editor::pasteFromSystemClipboard()
{
    std::string clipboard = getSystemClipboard();
    if(clipboard.empty())
    {
        setStatusMessage("System clipboard is empty");
        return;
    }

    yankBuffer = clipboard;
    pasteAfter();
    setStatusMessage("Pasted from system clipboard");
}

void Editor::pasteAfter()
{
    if(useSystemClipboard)
    {
        std::string clipboard = getSystemClipboard();
        if(!clipboard.empty())
            yankBuffer = clipboard;
    }

    if(yankBuffer.empty())
        return;

    bool hasNewline = (text_utils::contains(yankBuffer, '\n'));
    if(yankBuffer.back() == '\n')
    {
        lines->insert(lines->begin() + *cursorY + 1, "");
        (*cursorY)++;
        *cursorX = 0;

        std::istringstream ss(yankBuffer);
        std::string line;
        int insertPos = *cursorY;

        while(std::getline(ss, line))
        {
            if(insertPos == *cursorY)
            {
                (*lines)[insertPos] = line;
            }
            else
            {
                lines->insert(lines->begin() + insertPos, line);
            }
            insertPos++;
        }
    }
    else if(hasNewline)
    {
        if(*cursorX < (*lines)[*cursorY].length())
        {
            if(utf8Mode)
            {
                std::string_view ln((*lines)[*cursorY]);
                *cursorX = text_utils::nextUtf8CharStart(ln, *cursorX);
            }
            else
            {
                (*cursorX)++;
            }
        }
        std::string& curLine = (*lines)[*cursorY];
        int insertPos =
            *cursorX < (int)curLine.length() ? *cursorX : (int)curLine.length();

        std::vector<std::string> parts;
        parts.reserve(8);
        size_t start = 0;
        while(start <= yankBuffer.size())
        {
            size_t pos = yankBuffer.find('\n', start);
            if(text_utils::is_not_found(pos))
            {
                parts.push_back(yankBuffer.substr(start));
                break;
            }
            parts.push_back(yankBuffer.substr(start, pos - start));
            start = pos + 1;
        }

        std::string suffix = curLine.substr(insertPos);
        curLine.erase(insertPos);
        curLine += parts[0];

        int insertRow = *cursorY + 1;
        for(size_t i = 1; i < parts.size(); ++i)
        {
            lines->insert(lines->begin() + insertRow, parts[i]);
            insertRow++;
        }

        int lastRow = *cursorY + (int)parts.size() - 1;
        (*lines)[lastRow] += suffix;
        *cursorY = lastRow;
        *cursorX = (int)(*lines)[lastRow].length() - (int)suffix.length();
    }
    else
    {
        if(*cursorX < (*lines)[*cursorY].length())
        {
            if(utf8Mode)
            {
                std::string_view ln((*lines)[*cursorY]);
                *cursorX = text_utils::nextUtf8CharStart(ln, *cursorX);
            }
            else
            {
                (*cursorX)++;
            }
        }
        int insertPos = *cursorX;
        (*lines)[*cursorY].insert(insertPos, yankBuffer);
        if(utf8Mode)
        {
            int lastStart = text_utils::prevUtf8CharStart(
                yankBuffer, (int)yankBuffer.size());
            *cursorX = insertPos + lastStart;
        }
        else
        {
            *cursorX = insertPos + (int)yankBuffer.length() - 1;
        }
    }

    *dirty = true;
    needsFullRedraw = true;
    saveState();
    setStatusMessage("Pasted");
}

void Editor::pasteBefore()
{
    if(useSystemClipboard)
    {
        std::string clipboard = getSystemClipboard();
        if(!clipboard.empty())
            yankBuffer = clipboard;
    }

    if(yankBuffer.empty())
        return;

    bool hasNewline = (text_utils::contains(yankBuffer, '\n'));
    if(yankBuffer.back() == '\n')
    {
        std::istringstream ss(yankBuffer);
        std::string line;
        int insertPos = *cursorY;

        while(std::getline(ss, line))
        {
            lines->insert(lines->begin() + insertPos, line);
            insertPos++;
        }
        *cursorX = 0;
    }
    else if(hasNewline)
    {
        std::string& curLine = (*lines)[*cursorY];
        int insertPos =
            *cursorX < (int)curLine.length() ? *cursorX : (int)curLine.length();

        std::vector<std::string> parts;
        parts.reserve(8);
        size_t start = 0;
        while(start <= yankBuffer.size())
        {
            size_t pos = yankBuffer.find('\n', start);
            if(text_utils::is_not_found(pos))
            {
                parts.push_back(yankBuffer.substr(start));
                break;
            }
            parts.push_back(yankBuffer.substr(start, pos - start));
            start = pos + 1;
        }

        std::string suffix = curLine.substr(insertPos);
        curLine.erase(insertPos);
        curLine += parts[0];

        int insertRow = *cursorY + 1;
        for(size_t i = 1; i < parts.size(); ++i)
        {
            lines->insert(lines->begin() + insertRow, parts[i]);
            insertRow++;
        }

        int lastRow = *cursorY + (int)parts.size() - 1;
        (*lines)[lastRow] += suffix;
        *cursorY = lastRow;
        *cursorX = (int)(*lines)[lastRow].length() - (int)suffix.length();
    }
    else
    {
        (*lines)[*cursorY].insert(*cursorX, yankBuffer);
    }

    *dirty = true;
    needsFullRedraw = true;
    saveState();
    setStatusMessage("Pasted");
}

void Editor::deleteVisualBlock()
{
    int startY, startX, endY, endX;
    getVisualBlockBounds(startY, startX, endY, endX);

    for(int row = startY; row <= endY && row < lines->size(); row++)
    {
        std::string& line = (*lines)[row];
        if(startX < line.length())
        {
            int deleteEnd = std::min(endX + 1, (int)line.length());
            line.erase(startX, deleteEnd - startX);
        }
    }

    *cursorY = startY;
    *cursorX = startX;
    if(*cursorX > (*lines)[*cursorY].length())
        *cursorX = (*lines)[*cursorY].length();

    *dirty = true;
    saveState();
    setMode(NORMAL);
    needsFullRedraw = true;
    setStatusMessage("Block deleted");
}

void Editor::yankVisualBlock()
{
    int startY, startX, endY, endX;
    getVisualBlockBounds(startY, startX, endY, endX);

    yankBuffer.clear();

    for(int row = startY; row <= endY && row < lines->size(); row++)
    {
        const std::string& line = (*lines)[row];
        if(startX < line.length())
        {
            int yankEnd = std::min(endX + 1, (int)line.length());
            yankBuffer += line.substr(startX, yankEnd - startX);
        }
        yankBuffer += "\n";
    }

    yankBuffer = "\x02" + yankBuffer;

    setStatusMessage("Block yanked");

    if(useSystemClipboard && yankBuffer.length() > 1)
    {
        setSystemClipboard(yankBuffer.substr(1));
    }
}

void Editor::changeVisualBlock()
{
    currentBuffer->visualBlockInsertText.clear();
    visualBlockChanging = true;

    int startY, startX, endY, endX;
    getVisualBlockBounds(startY, startX, endY, endX);

    for(int row = startY; row <= endY && row < lines->size(); row++)
    {
        std::string& line = (*lines)[row];
        if(startX < line.length())
        {
            int deleteEnd = std::min(endX + 1, (int)line.length());
            line.erase(startX, deleteEnd - startX);
        }
    }

    *cursorY = startY;
    *cursorX = startX;

    setMode(INSERT);
}

void Editor::applyVisualBlockInsert()
{
    if(currentBuffer->visualBlockInsertText.empty())
        return;

    int startY = currentBuffer->visualBlockStartY;
    int endY = currentBuffer->visualBlockEndY;
    int insertX = currentBuffer->visualBlockStartX;

    for(int row = startY; row <= endY && row < lines->size(); row++)
    {
        if(row == *cursorY)
            continue;

        std::string& line = (*lines)[row];
        while(line.length() < insertX)
        {
            line += " ";
        }
        line.insert(insertX, currentBuffer->visualBlockInsertText);
    }

    *dirty = true;
    saveState();
    needsFullRedraw = true;
    setStatusMessage("Block insert applied to " +
                     std::to_string(endY - startY + 1) + " lines");
}

void Editor::deleteSelection()
{
    if(currentMode == VISUAL_LINE)
    {
        int startY =
            std::min(currentBuffer->visualStartY, currentBuffer->visualEndY);
        int endY =
            std::max(currentBuffer->visualStartY, currentBuffer->visualEndY);

        yankSelection();

        for(int i = endY; i >= startY; i--)
        {
            lines->erase(lines->begin() + i);
        }

        if(lines->empty())
        {
            lines->push_back("");
        }

        *cursorY = std::min(startY, (int)lines->size() - 1);
        *cursorX = 0;
    }
    else
    {
        int startY, startX, endY, endX;
        getSelectionBounds(startY, startX, endY, endX);

        if(startY == endY)
        {
            (*lines)[startY].erase(startX, endX - startX + 1);
        }
        else
        {
            (*lines)[startY] = (*lines)[startY].substr(0, startX) +
                               (*lines)[endY].substr(endX + 1);
            for(int i = endY; i > startY; i--)
            {
                lines->erase(lines->begin() + i);
            }
        }

        *cursorY = startY;
        *cursorX = startX;
    }

    *dirty = true;
    needsFullRedraw = true;
}
