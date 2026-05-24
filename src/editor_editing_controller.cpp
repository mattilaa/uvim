#include "editor_editing_controller.h"
#include "constants.h"
#include "editor.h"
#include "enablelog.h"
#include "key_enums.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>

EditorEditingController::EditorEditingController(Editor& editor)
    : editor(editor)
{
}

void EditorEditingController::insertTab()
{
    editor.insertTabImpl();
}

void EditorEditingController::toggleCase()
{
    editor.toggleCaseImpl();
}

void EditorEditingController::joinLines()
{
    editor.joinLinesImpl();
}

void EditorEditingController::insertLineAbove()
{
    editor.insertLineAboveImpl();
}

void EditorEditingController::insertLineBelow()
{
    editor.insertLineBelowImpl();
}

void EditorEditingController::deleteCurrentLine()
{
    editor.deleteCurrentLineImpl();
}

void EditorEditingController::deleteToLineStart()
{
    editor.deleteToLineStartImpl();
}

void EditorEditingController::deleteCharAtCursor()
{
    editor.deleteCharAtCursorImpl();
}

void EditorEditingController::deleteCharBeforeCursor()
{
    editor.deleteCharBeforeCursorImpl();
}

void EditorEditingController::deleteWordBackward()
{
    editor.deleteWordBackwardImpl();
}

void EditorEditingController::deleteWord()
{
    editor.deleteWordImpl();
}

void EditorEditingController::yankWord()
{
    editor.yankWordImpl();
}

void EditorEditingController::handleBackspace()
{
    editor.handleBackspaceImpl();
}

void EditorEditingController::replaceCharAtCursor(char c)
{
    editor.replaceCharAtCursorImpl(c);
}

void EditorEditingController::beginChangeRecording(int count)
{
    editor.beginChangeRecordingImpl(count);
}

void EditorEditingController::recordChangeKey(int key)
{
    editor.recordChangeKeyImpl(key);
}

void EditorEditingController::deferChangeRecordingCommit()
{
    editor.deferChangeRecordingCommitImpl();
}

void EditorEditingController::commitChangeRecording()
{
    editor.commitChangeRecordingImpl();
}

void EditorEditingController::cancelChangeRecording()
{
    editor.cancelChangeRecordingImpl();
}

void EditorEditingController::finishChangeRecordingIfDeferred()
{
    editor.finishChangeRecordingIfDeferredImpl();
}

bool EditorEditingController::isRecordingChange() const
{
    return editor.isRecordingChangeImpl();
}

bool EditorEditingController::isReplayingChange() const
{
    return editor.isReplayingChangeImpl();
}

int EditorEditingController::readKeyRecorded()
{
    return editor.readKeyRecordedImpl();
}

void EditorEditingController::repeatLastChange(int times)
{
    editor.repeatLastChangeImpl(times);
}

void EditorEditingController::insertUtf8Char(int c)
{
    editor.insertUtf8CharImpl(c);
}

void EditorEditingController::indentCurrentLine()
{
    editor.indentCurrentLineImpl();
}

void EditorEditingController::dedentCurrentLine()
{
    editor.dedentCurrentLineImpl();
}

void EditorEditingController::handleLinewiseOperator(char op, int count)
{
    editor.handleLinewiseOperatorImpl(op, count);
}

void Editor::insertTabImpl()
{
    for(int i = 0; i < tabSpaces; i++)
    {
        insertChar(' ');
    }
}

void Editor::toggleCaseImpl()
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

void Editor::joinLinesImpl()
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

void Editor::insertLineAboveImpl()
{
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
    size_t indent = 0;
    while(indent < currentLine.length() &&
          (currentLine[indent] == ' ' || currentLine[indent] == '\t'))
    {
        indent++;
    }
    std::string indentStr = currentLine.substr(0, indent);
    bool cppClosingBraceLine = false;
    if(isFileType<FileType::Cpp>())
    {
        std::string trimmed = ltrim(currentLine);
        cppClosingBraceLine = !trimmed.empty() && trimmed[0] == '}';
        if(cppClosingBraceLine)
            indentStr.append(indentWidthForBraces(), ' ');
    }
    if(autoTags &&
       (isFileType<FileType::Html>() || isFileType<FileType::Xml>()))
    {
        size_t pos = currentLine.find('<');
        if(text_utils::is_found(pos))
        {
            size_t gt = currentLine.find('>', pos);
            if(text_utils::is_found(gt) && pos + 1 < currentLine.size())
            {
                char next = currentLine[pos + 1];
                if(next != '/' && next != '!' && next != '?')
                {
                    size_t nameStart = pos + 1;
                    while(nameStart < gt && (currentLine[nameStart] == ' ' ||
                                             currentLine[nameStart] == '\t'))
                        ++nameStart;
                    size_t nameEnd = nameStart;
                    auto isTagChar = [](char ch)
                    {
                        return text_utils::is_alnum(ch) || ch == ':' ||
                               ch == '_' || ch == '-';
                    };
                    while(nameEnd < gt && isTagChar(currentLine[nameEnd]))
                        ++nameEnd;
                    bool isVoid = false;
                    if(nameEnd > nameStart)
                    {
                        std::string_view tag =
                            std::string_view(currentLine)
                                .substr(nameStart, nameEnd - nameStart);
                        if(isFileType<FileType::Html>())
                        {
                            for(auto v : constants::html_void_tags)
                            {
                                if(text_utils::iequals_ascii(tag, v))
                                {
                                    isVoid = true;
                                    break;
                                }
                            }
                        }
                    }
                    if(isVoid)
                        return;
                    indentStr.append(tabSpaces, ' ');
                }
            }
        }
    }
    if(isFileType<FileType::Cpp>())
    {
        std::string trimmed = ltrim(currentLine);
        if(!cppClosingBraceLine && starts_with_kw(trimmed))
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

    lines->insert(lines->begin() + *cursorY, indentStr);
    *cursorX = (int)indentStr.length();
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

void Editor::insertLineBelowImpl()
{
    if(*cursorY >= (int)lines->size())
    {
        lines->push_back("");
    }
    else
    {
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
        size_t indent = 0;
        while(indent < currentLine.length() &&
              (currentLine[indent] == ' ' || currentLine[indent] == '\t'))
        {
            indent++;
        }
        std::string indentStr = currentLine.substr(0, indent);

        bool addExtraIndent = false;
        int extraIndentWidth = tabSpaces;
        if(isFileType<FileType::Cpp>())
        {
            size_t lastNonSpace = currentLine.find_last_not_of(" \t");
            if(text_utils::is_found(lastNonSpace) &&
               currentLine[lastNonSpace] == '{')
            {
                addExtraIndent = true;
                extraIndentWidth = indentWidthForBraces();
            }
        }
        if(autoTags &&
           (isFileType<FileType::Html>() || isFileType<FileType::Xml>()))
        {
            bool htmlShouldIndent = false;
            size_t lt = currentLine.rfind('<');
            size_t gt = currentLine.rfind('>');
            if(text_utils::is_found(lt) && text_utils::is_found(gt) &&
               lt < gt && lt + 1 < currentLine.size())
            {
                char next = currentLine[lt + 1];
                if(next != '/' && next != '!' && next != '?')
                {
                    size_t selfClose = currentLine.rfind('/');
                    if(text_utils::is_not_found(selfClose) || selfClose < lt ||
                       selfClose > gt)
                    {
                        bool isVoid = false;
                        size_t nameStart = lt + 1;
                        while(nameStart < gt &&
                              (currentLine[nameStart] == ' ' ||
                               currentLine[nameStart] == '\t'))
                            ++nameStart;
                        size_t nameEnd = nameStart;
                        auto isTagChar = [](char ch)
                        {
                            return text_utils::is_alnum(ch) || ch == ':' ||
                                   ch == '_' || ch == '-';
                        };
                        while(nameEnd < gt && isTagChar(currentLine[nameEnd]))
                            ++nameEnd;
                        if(nameEnd > nameStart && isFileType<FileType::Html>())
                        {
                            std::string_view tag =
                                std::string_view(currentLine)
                                    .substr(nameStart, nameEnd - nameStart);
                            for(auto v : constants::html_void_tags)
                            {
                                if(text_utils::iequals_ascii(tag, v))
                                {
                                    isVoid = true;
                                    break;
                                }
                            }
                        }
                        if(!isVoid)
                            htmlShouldIndent = true;
                    }
                }
            }
            if(htmlShouldIndent)
            {
                addExtraIndent = true;
                extraIndentWidth = tabSpaces;
            }
        }

        if(isFileType<FileType::Cpp>() && !addExtraIndent)
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
            newLine.append(extraIndentWidth, ' ');

        lines->insert(lines->begin() + *cursorY + 1, newLine);
    }
    (*cursorY)++;
    if(*cursorY >= 0 && *cursorY < (int)lines->size())
        *cursorX = (int)(*lines)[*cursorY].length();
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

void Editor::deleteCurrentLineImpl()
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

void Editor::deleteToLineStartImpl()
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

void Editor::deleteCharAtCursorImpl()
{
    deleteCharForward();
    saveState();
}

void Editor::deleteCharBeforeCursorImpl()
{
    if(*cursorX > 0)
    {
        deleteChar();
        saveState();
    }
}

void Editor::deleteWordBackwardImpl()
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

void Editor::deleteWordImpl()
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

void Editor::yankWordImpl()
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

void Editor::handleBackspaceImpl()
{
    deleteChar();
}

void Editor::replaceCharAtCursorImpl(char c)
{
    if(*cursorY >= (int)lines->size())
        return;
    std::string& line = (*lines)[*cursorY];
    if(*cursorX >= (int)line.length())
        return;

    line[*cursorX] = c;
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

void Editor::beginChangeRecordingImpl(int count)
{
    if(replayingChange || recordingChange)
        return;
    recordingChange = true;
    deferChangeCommit = false;
    pendingChangeKeys.clear();
    pendingChangeCount = (count > 0) ? count : 1;
}

void Editor::recordChangeKeyImpl(int key)
{
    if(!recordingChange || replayingChange)
        return;
    pendingChangeKeys.push_back(key);
}

void Editor::deferChangeRecordingCommitImpl()
{
    if(!recordingChange || replayingChange)
        return;
    deferChangeCommit = true;
}

void Editor::commitChangeRecordingImpl()
{
    if(!recordingChange || replayingChange)
        return;
    if(!pendingChangeKeys.empty())
    {
        lastChangeKeys = pendingChangeKeys;
        lastChangeCount = pendingChangeCount;
    }
    recordingChange = false;
    deferChangeCommit = false;
    pendingChangeKeys.clear();
    pendingChangeCount = 1;
}

bool Editor::moveLineBlock(int startY, int endY, int delta)
{
    if(!currentBuffer || !lines || !cursorY || !cursorX || lines->empty())
        return false;
    if(delta != -1 && delta != 1)
        return false;

    if(startY > endY)
        std::swap(startY, endY);
    startY = std::clamp(startY, 0, (int)lines->size() - 1);
    endY = std::clamp(endY, 0, (int)lines->size() - 1);

    if(delta < 0 && startY == 0)
        return false;
    if(delta > 0 && endY >= (int)lines->size() - 1)
        return false;

    const bool firstContentChange =
        currentBuffer->undoIndex == 0 && currentBuffer->undoStack.size() == 1;
    const int originalCursorX = *cursorX;
    const int originalCursorY = *cursorY;

    auto moveRange = [&](auto& values)
    {
        if(delta < 0)
        {
            std::rotate(values.begin() + startY - 1, values.begin() + startY,
                        values.begin() + endY + 1);
        }
        else
        {
            std::rotate(values.begin() + startY, values.begin() + endY + 1,
                        values.begin() + endY + 2);
        }
    };

    moveRange(*lines);
    if(currentBuffer->blameEntries.size() == lines->size())
        moveRange(currentBuffer->blameEntries);

    if(*cursorY >= startY && *cursorY <= endY)
        *cursorY += delta;
    else if(delta < 0 && *cursorY == startY - 1)
        *cursorY = endY;
    else if(delta > 0 && *cursorY == endY + 1)
        *cursorY = startY;

    *cursorY = std::clamp(*cursorY, 0, (int)lines->size() - 1);
    *cursorX = std::clamp(*cursorX, 0, (int)(*lines)[*cursorY].size());
    *dirty = true;
    currentBuffer->lspSyncNeeded = true;
    currentBuffer->blameValid = false;
    saveState();
    if(firstContentChange && !currentBuffer->undoStack.empty())
    {
        currentBuffer->undoStack[0].cursorX = originalCursorX;
        currentBuffer->undoStack[0].cursorY = originalCursorY;
    }
    adjustViewport();
    needsFullRedraw = true;
    return true;
}

void Editor::cancelChangeRecordingImpl()
{
    recordingChange = false;
    deferChangeCommit = false;
    pendingChangeKeys.clear();
    pendingChangeCount = 1;
}

void Editor::finishChangeRecordingIfDeferredImpl()
{
    if(recordingChange && deferChangeCommit)
    {
        commitChangeRecording();
    }
}

bool Editor::isRecordingChangeImpl() const
{
    return recordingChange;
}

bool Editor::isReplayingChangeImpl() const
{
    return replayingChange;
}

int Editor::readKeyRecordedImpl()
{
    int key = Terminal::readKey();
    recordChangeKey(key);
    return key;
}

void Editor::repeatLastChangeImpl(int times)
{
    if(lastChangeKeys.empty())
    {
        setStatusMessage("No previous change");
        return;
    }
    int repeats = std::max(1, times);
    replayingChange = true;

    for(int i = repeats - 1; i >= 0; --i)
    {
        std::vector<int> sequence;
        if(lastChangeCount > 1)
        {
            std::string countStr = std::to_string(lastChangeCount);
            for(char ch : countStr)
                sequence.push_back(static_cast<unsigned char>(ch));
        }
        sequence.insert(sequence.end(), lastChangeKeys.begin(),
                        lastChangeKeys.end());
        for(auto it = sequence.rbegin(); it != sequence.rend(); ++it)
            Terminal::unreadKey(*it);
    }
}

void Editor::insertUtf8CharImpl(int c)
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

void Editor::indentCurrentLineImpl()
{
    if(*cursorY >= (int)lines->size())
        return;
    (*lines)[*cursorY] = "    " + (*lines)[*cursorY];
    *cursorX += 4;
    *dirty = true;
}

void Editor::dedentCurrentLineImpl()
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

void Editor::handleLinewiseOperatorImpl(char op, int count)
{
    switch(op)
    {
    case keyCode(typed::TypedKey::KEY_D):
        for(int i = 0; i < count && !lines->empty(); i++)
        {
            deleteCurrentLine();
        }
        break;
    case keyCode(typed::TypedKey::KEY_Y):
    {
        LOG_DEBUG(LOG,
                  "handleLinewiseOperator: yy detected, count={}, cursorY={}",
                  count, *cursorY);
        yankBuffer.clear();
        int endLine = std::min(*cursorY + count, (int)lines->size());
        for(int y = *cursorY; y < endLine; y++)
        {
            yankBuffer += (*lines)[y] + "\n";
        }

        LOG_DEBUG(LOG,
                  "handleLinewiseOperator: yankBuffer.length()={}, "
                  "useSystemClipboard={}",
                  yankBuffer.length(), useSystemClipboard);

        std::string msg = std::to_string(count) + " lines yanked";
        if(useSystemClipboard && !yankBuffer.empty())
        {
            LOG_DEBUG(LOG,
                      "handleLinewiseOperator: calling setSystemClipboard");
            setSystemClipboard(yankBuffer);
            msg += " (copied to clipboard)";
        }
        setStatusMessage(msg);
    }
    break;
    case keyCode(command::CommandKey::KEY_GREATER):
        for(int i = 0; i < count && *cursorY + i < (int)lines->size(); i++)
        {
            (*lines)[*cursorY + i] = "    " + (*lines)[*cursorY + i];
        }
        *dirty = true;
        saveState();
        needsFullRedraw = true;
        break;
    case keyCode(command::CommandKey::KEY_LESS):
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
    case keyCode(command::CommandKey::KEY_EQUAL):
        for(int i = 0; i < count && *cursorY + i < (int)lines->size(); i++)
        {
            autoIndentLine(*cursorY + i);
        }
        *dirty = true;
        saveState();
        needsFullRedraw = true;
        break;
    case keyCode(typed::TypedKey::KEY_C):
        for(int i = 0; i < count && !lines->empty(); i++)
        {
            deleteCurrentLine();
        }
        insertLineAbove();
        break;
    }
}
