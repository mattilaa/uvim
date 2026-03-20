#include "editor.h"
#include "enablelog.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

static bool isHeaderFile(const std::string& path)
{
    return path == ".h" || path == ".hpp";
}

static bool isSourceFile(const std::string& path)
{
    return path == ".c" || path == ".cpp" || path == ".cc";
}

void Editor::handleOperatorPendingMode(int c)
{
    if(dispatchModeKey(c))
    {
        return;
    }

    // If user pressed ESC, cancel
    if(c == keyCode(control::ControlKey::ESC))
    {
        setMode(NORMAL);
        setStatusMessage("");
        return;
    }

    // If user pressed a digit while building a count (rare), ignore here for
    // simplicity Support keyCode(typed::TypedKey::KEY_I) or keyCode(typed::TypedKey::KEY_A) to enter object-specifier substate
    if(!pendingAwaitingObject && (c == keyCode(typed::TypedKey::KEY_I) || c == keyCode(typed::TypedKey::KEY_A)))
    {
        pendingAwaitingObject = true;
        pendingObjectType = (char)c;
        setStatusMessage(std::string("Operator: ") + pendingOperator + " " +
                         pendingObjectType);
        return;
    }

    int startY, startX, endY, endX;
    bool rangeFound = false;

    if(pendingAwaitingObject)
    {
        // Expect a text-object specifier now (e.g. keyCode(command::CommandKey::KEY_LEFT_PAREN), keyCode(command::CommandKey::KEY_LEFT_BRACE), keyCode(command::CommandKey::KEY_DOUBLE_QUOTE), keyCode(typed::TypedKey::KEY_W), etc.)
        char obj = (char)c;
        bool around = (pendingObjectType == keyCode(typed::TypedKey::KEY_A));
        rangeFound =
            getTextObjectRange(obj, around, startY, startX, endY, endX);
    }
    else
    {
        // Motion-based operator: treat c as a motion (w, b, e, $, 0, %, etc.)
        // We'll simulate the motion by saving cursor, doing it, reading
        // destination, then restoring.
        int saveX = *cursorX, saveY = *cursorY, saveWanted = *wantedX,
            saveOffsetY = *offsetY, saveOffsetX = *offsetX;

        bool isExclusiveMotion = false; // Track if motion should be exclusive

        // Apply motion
        switch(c)
        {
        case keyCode(typed::TypedKey::KEY_W):
        {
            // For dw/cw: delete from cursor to start of next word (exclusive)
            // This is vim's behavior: delete current word + trailing whitespace
            // but stay on the same line
            const std::string& line = (*lines)[*cursorY];
            int end = *cursorX;

            // Helper lambda for word character check
            auto isWordChar = [](char ch)
            {
                return std::isalnum(static_cast<unsigned char>(ch)) ||
                       ch == keyCode(command::CommandKey::KEY_UNDERSCORE);
            };

            if(end < (int)line.length())
            {
                char startChar = line[end];

                if(std::isspace(static_cast<unsigned char>(startChar)))
                {
                    // On whitespace: skip whitespace then skip next word/punct
                    while(end < (int)line.length() &&
                          std::isspace(static_cast<unsigned char>(line[end])))
                        end++;

                    if(end < (int)line.length())
                    {
                        if(isWordChar(line[end]))
                        {
                            while(end < (int)line.length() &&
                                  isWordChar(line[end]))
                                end++;
                        }
                        else
                        {
                            while(end < (int)line.length() &&
                                  !isWordChar(line[end]) &&
                                  !std::isspace(
                                      static_cast<unsigned char>(line[end])))
                                end++;
                        }
                    }
                }
                else if(isWordChar(startChar))
                {
                    // On word: skip word + trailing whitespace
                    while(end < (int)line.length() && isWordChar(line[end]))
                        end++;
                    while(end < (int)line.length() &&
                          std::isspace(static_cast<unsigned char>(line[end])))
                        end++;
                }
                else
                {
                    // On punctuation: skip punctuation + trailing whitespace
                    while(end < (int)line.length() && !isWordChar(line[end]) &&
                          !std::isspace(static_cast<unsigned char>(line[end])))
                        end++;
                    while(end < (int)line.length() &&
                          std::isspace(static_cast<unsigned char>(line[end])))
                        end++;
                }
            }

            // Set destination
            *cursorX = end;
            // keyCode(typed::TypedKey::KEY_W) is exclusive, so we'll subtract 1 later
            isExclusiveMotion = true;
        }
        break;
        case keyCode(typed::TypedKey::KEY_B):
            moveWordBackward();
            isExclusiveMotion = true; // keyCode(typed::TypedKey::KEY_B) is exclusive in vim
            break;
        case keyCode(typed::TypedKey::KEY_E):
            moveToEndOfWord();
            isExclusiveMotion = false; // keyCode(typed::TypedKey::KEY_E) is inclusive in vim
            break;
        case keyCode(typed::TypedKey::KEY_0):
            moveToLineStart();
            break;
        case keyCode(command::CommandKey::KEY_DOLLAR):
            moveToLineEnd();
            break;
        case keyCode(command::CommandKey::KEY_PERCENT):
            moveToMatchingBracket();
            break;
        case keyCode(typed::TypedKey::KEY_J):
            moveDown(pendingCount);
            break;
        case keyCode(typed::TypedKey::KEY_K):
            moveUp(pendingCount);
            break;
        case keyCode(typed::TypedKey::KEY_CAP_G):
            // Move to last line or specific line if count given
            if(pendingCount > 0)
                moveToLine(pendingCount - 1);
            else
                moveToLastLine();
            break;
        case keyCode(typed::TypedKey::KEY_G):
            // For gg motion - need to read next char
            {
                int nextChar = Terminal::readKey();
                if(nextChar == keyCode(typed::TypedKey::KEY_G))
                {
                    moveToFirstLine();
                }
                else
                {
                    // Unsupported gg variant
                    setStatusMessage("Unknown motion for operator");
                    setMode(NORMAL);
                    *cursorX = saveX;
                    *cursorY = saveY;
                    *wantedX = saveWanted;
                    *offsetY = saveOffsetY;
                    *offsetX = saveOffsetX;
                    return;
                }
            }
            break;
        case keyCode(command::CommandKey::KEY_LEFT_BRACE):
            // Move to beginning of paragraph (previous blank line)
            {
                int targetY = *cursorY;
                // Skip current paragraph
                while(targetY > 0 && !(*lines)[targetY].empty())
                    targetY--;
                // Skip blank lines
                while(targetY > 0 && (*lines)[targetY].empty())
                    targetY--;
                // Find beginning of previous paragraph
                while(targetY > 0 && !(*lines)[targetY - 1].empty())
                    targetY--;
                *cursorY = targetY;
                *cursorX = 0;
            }
            break;
        case keyCode(command::CommandKey::KEY_RIGHT_BRACE):
            // Move to end of paragraph (next blank line)
            {
                int targetY = *cursorY;
                int maxLine = lines->size() - 1;
                // Skip current paragraph
                while(targetY < maxLine && !(*lines)[targetY].empty())
                    targetY++;
                // Skip blank lines
                while(targetY < maxLine && (*lines)[targetY].empty())
                    targetY++;
                *cursorY = targetY;
                *cursorX = 0;
            }
            break;
        default:
            // unsupported motion -> cancel operator
            setStatusMessage("Unknown motion for operator");
            setMode(NORMAL);
            // restore
            *cursorX = saveX;
            *cursorY = saveY;
            *wantedX = saveWanted;
            *offsetY = saveOffsetY;
            *offsetX = saveOffsetX;
            return;
        }

        // get destination
        int destX = *cursorX, destY = *cursorY;
        // restore original cursor
        *cursorX = saveX;
        *cursorY = saveY;
        *wantedX = saveWanted;
        *offsetY = saveOffsetY;
        *offsetX = saveOffsetX;

        // compute range between original and dest
        if(saveY < destY || (saveY == destY && saveX <= destX))
        {
            // Forward motion
            startY = saveY;
            startX = saveX;
            endY = destY;
            endX = destX;

            // For exclusive forward motions, don't include the character at
            // destination
            if(isExclusiveMotion)
            {
                // Make the range exclusive by moving end back one position
                if(endX > 0)
                {
                    endX--;
                }
                else if(endY > 0)
                {
                    // If at start of line, go to end of previous line
                    endY--;
                    endX = (*lines)[endY].length() - 1;
                }
            }
        }
        else
        {
            // Backward motion
            startY = destY;
            startX = destX;
            endY = saveY;
            endX = saveX;

            // For exclusive backward motions, adjust similarly
            if(isExclusiveMotion)
            {
                // The range is already correct for backward exclusive motions
                // because we want to delete from dest to just before current
            }
        }

        rangeFound = true;
    }

    if(!rangeFound)
    {
        setStatusMessage("No object found");
        setMode(NORMAL);
        return;
    }

    // Apply operator
    char op = pendingOperator;

    // DEBUG: Write to file what we're about to delete
    {
        std::ofstream dbg("/tmp/uvim_dw_debug.txt", std::ios::app);
        dbg << "=== dw operation ===" << std::endl;
        dbg << "op=" << op << std::endl;
        dbg << "startY=" << startY << " startX=" << startX << std::endl;
        dbg << "endY=" << endY << " endX=" << endX << std::endl;
        if(startY < (int)lines->size())
        {
            dbg << "line[" << startY << "]=" << (*lines)[startY] << std::endl;
            dbg << "deleting chars " << startX << " to " << endX << std::endl;
            if(startY == endY && startX < (int)(*lines)[startY].length())
            {
                dbg << "text to delete: ["
                    << (*lines)[startY].substr(startX, endX - startX + 1) << "]"
                    << std::endl;
            }
        }
        dbg << std::endl;
    }

    applyOperatorToRange(op, startY, startX, endY, endX);

    // Clear state
    pendingOperator = 0;
    pendingAwaitingObject = false;
    pendingObjectType = 0;
    pendingCount = 0;

    if(op == keyCode(typed::TypedKey::KEY_C))
    {
        setMode(INSERT);
    }
    else
    {
        setMode(NORMAL);
    }
    needsFullRedraw = true;
}

void Editor::handleNormalMode(int c)
{
    LOG_DEBUG(LOG, "handleNormalMode c={} ('{}') commandBuffer='{}'", c,
              (char)c, commandBuffer);

    if(dispatchModeKey(c))
    {
        return;
    }

    static bool pendingDelete = false;
    static bool pendingYank = false;
    static bool pendingIndent = false;
    static bool pendingShiftRight = false;
    static bool pendingShiftLeft = false;

    // ----- single-character replace (vim/neovim-style 'r{char}') -----
    if(c == keyCode(typed::TypedKey::KEY_R))
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

    if(c >= keyCode(typed::TypedKey::KEY_1) && c <= keyCode(typed::TypedKey::KEY_9) && repeatCount == 0 && commandBuffer.empty())
    {
        repeatCount = c - keyCode(typed::TypedKey::KEY_0);
        return;
    }
    else if(c >= keyCode(typed::TypedKey::KEY_0) && c <= keyCode(typed::TypedKey::KEY_9) && repeatCount > 0)
    {
        repeatCount = repeatCount * 10 + (c - keyCode(typed::TypedKey::KEY_0));
        return;
    }
    int count = std::max(1, repeatCount);

    // ----- Leader (space) prefixed commands (MUST be early) -----
    if(commandBuffer == " ")
    {
        if(c == keyCode(typed::TypedKey::KEY_H))
        {
            // Leader + h: jump to alternate file (header/source)
            jumpToAlternateFile();
            commandBuffer.clear();
            repeatCount = 0;
            return;
        }
        else if(c == keyCode(typed::TypedKey::KEY_B))
        {
            // Leader + b: start buffer command sequence
            commandBuffer = " b";
            setStatusMessage("Leader-b");
            repeatCount = 0;
            return;
        }
        else if(c == keyCode(typed::TypedKey::KEY_Y))
        {
            // Leader + y: yank to system clipboard
            yankToSystemClipboard();
            commandBuffer.clear();
            repeatCount = 0;
            return;
        }
        else if(c == keyCode(typed::TypedKey::KEY_P))
        {
            // Leader + p: paste from system clipboard
            pasteFromSystemClipboard();
            commandBuffer.clear();
            repeatCount = 0;
            return;
        }
        else if(c == keyCode(typed::TypedKey::KEY_F))
        {
            // Leader + f: format file
            commandBuffer.clear();
            repeatCount = 0;

            if(isFileType<FileType::Python>())
            {
                pythonFormatBuffer();
                return;
            }
            if(isFileType<FileType::Cpp>() || isHeaderFile(*filename))
            {
                clangFormatWithArgs("", "clang-format: formatted file");
                return;
            }
            setStatusMessage("format: unsupported file type (" + *filename +
                             ")");
            return;
        }
        else if(c == keyCode(typed::TypedKey::KEY_X))
        {
            std::string dir = ".";
            if(!filename->empty())
            {
                size_t lastSlash = filename->find_last_of("/");
                if(lastSlash != std::string::npos)
                {
                    dir = filename->substr(0, lastSlash);
                    if(dir.empty())
                        dir = "/";
                }
            }
            openFileBrowser(dir);
            return;
        }
        else if(c == keyCode(control::ControlKey::SPACE))
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
        if(c == keyCode(typed::TypedKey::KEY_D))
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
        if(c == keyCode(typed::TypedKey::KEY_D))
        {
            goToDefinition();
            repeatCount = 0;
            return;
        }
        else if(c == keyCode(typed::TypedKey::KEY_G))
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
    else if(c == keyCode(typed::TypedKey::KEY_D))
    {
        if(pendingDelete)
        {
            // dd detected
            for(int i = 0; i < count; i++)
            {
                deleteLine();
            }
            if(showGitBlame && currentBuffer)
                currentBuffer->blameValid = false;
            saveState();
            setStatusMessage(std::to_string(count) + " line(s) deleted");
            pendingDelete = false;
            repeatCount = 0;
            return;
        }
        else
        {
            // first keyCode(typed::TypedKey::KEY_D)
            pendingDelete = true;
            pendingYank = false;   // Cancel any pending yank
            pendingIndent = false; // Cancel any pending indent
            return;
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_Y))
    {
        if(pendingYank)
        {
            // yy detected - yank multiple lines
            LOG_DEBUG(LOG, "yy detected, count={}", count);
            yankBuffer.clear();
            int startLine = *cursorY;
            int endLine =
                std::min(startLine + count - 1, (int)lines->size() - 1);

            for(int i = startLine; i <= endLine; i++)
            {
                yankBuffer += (*lines)[i] + "\n";
            }

            LOG_DEBUG(LOG, "yy: yankBuffer.length()={}, useSystemClipboard={}",
                      yankBuffer.length(), useSystemClipboard);

            int linesYanked = endLine - startLine + 1;
            std::string msg = std::to_string(linesYanked) + " line" +
                              (linesYanked > 1 ? "s" : "") + " yanked";

            if(useSystemClipboard && !yankBuffer.empty())
            {
                LOG_DEBUG(LOG, "yy: calling setSystemClipboard");
                setSystemClipboard(yankBuffer);
                msg += " (copied to clipboard)";
            }

            setStatusMessage(msg);
            pendingYank = false;
            repeatCount = 0;
            return;
        }
        else
        {
            // first keyCode(typed::TypedKey::KEY_Y)
            pendingYank = true;
            pendingDelete = false; // Cancel any pending delete
            pendingIndent = false; // Cancel any pending indent
            return;
        }
    }
    else if(c == keyCode(command::CommandKey::KEY_EQUAL))
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
            // first keyCode(command::CommandKey::KEY_EQUAL)
            pendingIndent = true;
            pendingDelete = false; // Cancel any pending delete
            pendingYank = false;   // Cancel any pending yank
            return;
        }
    }
    else if(c == keyCode(command::CommandKey::KEY_GREATER))
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
            // first keyCode(command::CommandKey::KEY_GREATER)
            pendingShiftRight = true;
            pendingShiftLeft = false;
            pendingDelete = false;
            pendingYank = false;
            pendingIndent = false;
            return;
        }
    }
    else if(c == keyCode(command::CommandKey::KEY_LESS))
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
            // first keyCode(command::CommandKey::KEY_LESS)
            pendingShiftLeft = true;
            pendingShiftRight = false;
            pendingDelete = false;
            pendingYank = false;
            pendingIndent = false;
            return;
        }
    }
    else if(pendingYank && c != keyCode(typed::TypedKey::KEY_Y))
    {
        // keyCode(typed::TypedKey::KEY_Y) followed by motion command - enter operator-pending mode
        pendingYank = false;
        enterOperatorPending(keyCode(typed::TypedKey::KEY_Y));
        pendingCount = count;
        // Process the motion command immediately
        handleOperatorPendingMode(c);
        repeatCount = 0;
        return;
    }
    else if(pendingDelete && c != keyCode(typed::TypedKey::KEY_D))
    {
        // keyCode(typed::TypedKey::KEY_D) followed by motion command - enter operator-pending mode
        pendingDelete = false;
        enterOperatorPending(keyCode(typed::TypedKey::KEY_D));
        pendingCount = count;
        // Process the motion command immediately
        handleOperatorPendingMode(c);
        repeatCount = 0;
        return;
    }
    else if(pendingIndent && c != keyCode(command::CommandKey::KEY_EQUAL))
    {
        // keyCode(command::CommandKey::KEY_EQUAL) followed by motion command - enter operator-pending mode
        pendingIndent = false;
        enterOperatorPending(keyCode(command::CommandKey::KEY_EQUAL));
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
    case keyCode(control::ControlKey::ESC):
    {
        // Handle double ESC to clear search highlights
        auto now = std::chrono::steady_clock::now();
        auto timeSinceLastEsc =
            std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                                  lastEscTime)
                .count();

        if(timeSinceLastEsc <= DOUBLE_ESC_TIMEOUT_MS)
        {
            if(!searchMatches.empty() || !searchQuery.empty())
            {
                // Double ESC detected - clear search highlights
                clearSearch();
                needsFullRedraw = true; // Force full redraw to clear highlights
            }
            setStatusMessage("");
            needsFullRedraw = true;
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
    case keyCode(typed::TypedKey::KEY_I):
        saveState();
        setMode(INSERT);
        break;
    case keyCode(typed::TypedKey::KEY_CAP_I):
        saveState();
        moveToLineStart();
        setMode(INSERT);
        break;
    case keyCode(typed::TypedKey::KEY_A):
        saveState();
        if(*cursorX < (*lines)[*cursorY].length())
            (*cursorX)++;
        setMode(INSERT);
        break;
    case keyCode(typed::TypedKey::KEY_CAP_A):
        saveState();
        moveToLineEnd();
        if(*cursorX < (*lines)[*cursorY].length())
            (*cursorX)++;
        setMode(INSERT);
        break;
    case keyCode(typed::TypedKey::KEY_O):
    {
        // Get indentation from current line
        const std::string& currentLine = (*lines)[*cursorY];
        size_t indent = 0;
        while(indent < currentLine.length() &&
              (currentLine[indent] == keyCode(control::ControlKey::SPACE) || currentLine[indent] == '\t'))
            indent++;

        // Insert new line below with same indentation
        std::string newLine(indent, keyCode(control::ControlKey::SPACE));
        lines->insert(lines->begin() + *cursorY + 1, newLine);
        *cursorY += 1;
        *cursorX = indent;

        saveState();
        *dirty = true;
        setMode(INSERT);
        needsFullRedraw = true;
        break;
    }
    case keyCode(typed::TypedKey::KEY_CAP_O):
    {
        // Insert new line above with same indentation
        const std::string& currentLine = (*lines)[*cursorY];
        size_t indent = 0;
        while(indent < currentLine.length() &&
              (currentLine[indent] == keyCode(control::ControlKey::SPACE) || currentLine[indent] == '\t'))
            indent++;

        std::string newLine(indent, keyCode(control::ControlKey::SPACE));
        lines->insert(lines->begin() + *cursorY, newLine);
        *cursorY = std::max(0, *cursorY);
        *cursorX = indent;

        saveState();
        *dirty = true;
        setMode(INSERT);
        needsFullRedraw = true;
        break;
    }
    case keyCode(typed::TypedKey::KEY_V):
        saveState();
        setMode(VISUAL);
        break;
    case keyCode(typed::TypedKey::KEY_CAP_V):
        saveState();
        setMode(VISUAL_LINE);
        break;
    case 22: // Ctrl-V (ASCII 22)
        saveState();
        setMode(VISUAL_BLOCK);
        break;
    case keyCode(command::CommandKey::KEY_COLON):
        setMode(COMMAND);
        break;
    case keyCode(command::CommandKey::KEY_SLASH):
        setMode(SEARCH_FORWARD);
        break;
    case keyCode(command::CommandKey::KEY_QUESTION):
        setMode(SEARCH_BACKWARD);
        break;
    case keyCode(typed::TypedKey::KEY_N):
        searchNext();
        break;
    case keyCode(typed::TypedKey::KEY_CAP_N):
        searchPrevious();
        break;
    case keyCode(command::CommandKey::KEY_HASH):
        // Vim-style: search backward for the word under the cursor.
        {
            std::string sym = getSymbolUnderCursor();
            if(sym.empty())
            {
                setStatusMessage("#: no word under cursor");
                break;
            }
            searchWordUnderCursor(false);
            break;
        }
    case keyCode(command::CommandKey::KEY_ASTERISK):
        // Vim-style: search forward for the word under the cursor.
        {
            std::string sym = getSymbolUnderCursor();
            if(sym.empty())
            {
                setStatusMessage("*: no word under cursor");
                break;
            }
            searchWordUnderCursor(true);
            break;
        }
    case keyCode(control::ControlKey::CTRL_P):
        setMode(FUZZY_FIND);
        break;
    case keyCode(control::ControlKey::CTRL_W): // Ctrl+W for buffer browser
        setMode(BUFFER_BROWSER);
        break;
    case keyCode(control::ControlKey::CTRL_S): // Ctrl+S for grep search (find in files)
        setMode(GREP_SEARCH);
        break;
    case keyCode(control::ControlKey::CTRL_O):
        jumpBack();
        break;
    case keyCode(control::ControlKey::CTRL_I):
        jumpForward();
        break;
    case keyCode(typed::TypedKey::KEY_H):
    case keyCode(navigation::NavigationKey::ARROW_LEFT):
        moveLeft();
        break;
    case keyCode(typed::TypedKey::KEY_L):
    case keyCode(navigation::NavigationKey::ARROW_RIGHT):
        moveRight();
        break;
    case keyCode(typed::TypedKey::KEY_J):
    case keyCode(navigation::NavigationKey::ARROW_DOWN):
        moveDown();
        break;
    case keyCode(typed::TypedKey::KEY_K):
    case keyCode(navigation::NavigationKey::ARROW_UP):
        moveUp();
        break;
    case keyCode(control::ControlKey::CTRL_D):
        scrollHalfPageDown();
        break;
    case keyCode(control::ControlKey::CTRL_U):
        scrollHalfPageUp();
        break;
    case keyCode(typed::TypedKey::KEY_W):
        moveWordForward();
        break;
    case keyCode(typed::TypedKey::KEY_B):
        moveWordBackward();
        break;
    case keyCode(typed::TypedKey::KEY_E):
        moveToEndOfWord();
        break;
    case keyCode(typed::TypedKey::KEY_0):
        moveToLineStart();
        break;
    case keyCode(command::CommandKey::KEY_DOLLAR):
        moveToLineEnd();
        break;
    case keyCode(typed::TypedKey::KEY_G):
        commandBuffer = "g";
        break;
    case keyCode(typed::TypedKey::KEY_CAP_G):
        moveToLastLine();
        break;
    case keyCode(typed::TypedKey::KEY_X):
        deleteCharAtCursor();
        saveState();
        *dirty = true;
        break;
    case keyCode(typed::TypedKey::KEY_S):
        // Substitute: delete char(s) under cursor and enter insert mode
        deleteCharAtCursor();
        saveState();
        *dirty = true;
        setMode(INSERT);
        break;
    case keyCode(typed::TypedKey::KEY_CAP_D):
        deleteToLineEnd();
        saveState();
        *dirty = true;
        break;
    case keyCode(typed::TypedKey::KEY_CAP_Y):
        yankLine();
        break;
    case keyCode(typed::TypedKey::KEY_CAP_J):
        // Join lines: current line with next line(s)
        {
            int linesToJoin = (repeatCount > 0) ? repeatCount : 2;
            for(int i = 0; i < linesToJoin - 1; i++)
            {
                if(*cursorY + 1 < (int)lines->size())
                {
                    std::string& currentLine = (*lines)[*cursorY];
                    std::string& nextLine = (*lines)[*cursorY + 1];
                    size_t endPos = currentLine.find_last_not_of(" \t");
                    if(endPos != std::string::npos)
                    {
                        currentLine = currentLine.substr(0, endPos + 1);
                    }
                    size_t startPos = nextLine.find_first_not_of(" \t");
                    std::string trimmedNext = (startPos != std::string::npos)
                                                  ? nextLine.substr(startPos)
                                                  : "";
                    currentLine += " " + trimmedNext;
                    lines->erase(lines->begin() + *cursorY + 1);
                }
            }
            *dirty = true;
            saveState();
            setStatusMessage(std::to_string(linesToJoin) + " lines joined");
            break;
        }
    case keyCode(typed::TypedKey::KEY_C):
        // change operator: enter operator pending (support e.g. cw, ci(, etc.)
        enterOperatorPending(keyCode(typed::TypedKey::KEY_C));
        pendingCount = count;
        break;
    case keyCode(typed::TypedKey::KEY_P):
        pasteAfter();
        break;
    case keyCode(typed::TypedKey::KEY_CAP_P):
        pasteBefore();
        break;
    case keyCode(typed::TypedKey::KEY_U):
        undo();
        break;
    case keyCode(control::ControlKey::CTRL_R):
        redo();
        break;
    case keyCode(command::CommandKey::KEY_PERCENT):
        moveToMatchingBracket();
        break;
    default:
        if(c >= 32 && c <= 126)
        {
            // Unknown printable → clear pending ops
            pendingDelete = pendingYank = pendingIndent = false;
            pendingShiftLeft = pendingShiftRight = false;
            repeatCount = 0;
            commandBuffer.clear();
        }
        break;
    }

    // Handle g-prefixed commands at end (for 'gg' which needs switch case for
    // g)
    if(commandBuffer == "g")
    {
        if(c == keyCode(typed::TypedKey::KEY_G))
        {
            moveToFirstLine();
            commandBuffer.clear();
        }
        else if(c != keyCode(typed::TypedKey::KEY_G) && c != keyCode(typed::TypedKey::KEY_D))
        {
            // Unknown g-command → cancel
            commandBuffer.clear();
        }
    }

    repeatCount = 0; // reset after each command
}

// ============================================================================
// Mode Handler Wrappers - Delegate to state machine implementations
// ============================================================================

void Editor::handleInsertMode(int c)
{
    if(dispatchModeKey(c))
    {
        return;
    }

    // Delegate to state machine implementation in insert_mode.cpp
    InsertMode state;
    ModeContext ctx = createModeContext(this);
    int key = (c);
    auto nextState = state.handle(ctx, key);

    // Handle state transition if returned
    if(nextState.has_value())
    {
        // Determine which mode to transition to based on the returned state
        if(std::holds_alternative<NormalMode>(*nextState))
        {
            setMode(NORMAL);
        }
        else if(std::holds_alternative<InsertMode>(*nextState))
        {
            setMode(INSERT);
        }
        // Add other state transitions as needed
    }
}

void Editor::handleVisualMode(int c)
{
    if(dispatchModeKey(c))
    {
        return;
    }

    // Delegate to state machine implementation in visual_mode.cpp
    VisualMode state;
    ModeContext ctx = createModeContext(this);
    int key = (c);
    auto nextState = state.handle(ctx, key);

    // Handle state transition if returned
    if(nextState.has_value())
    {
        if(std::holds_alternative<NormalMode>(*nextState))
        {
            setMode(NORMAL);
        }
        else if(std::holds_alternative<VisualLineMode>(*nextState))
        {
            setMode(VISUAL_LINE);
        }
        else if(std::holds_alternative<VisualBlockMode>(*nextState))
        {
            setMode(VISUAL_BLOCK);
        }
        else if(std::holds_alternative<InsertMode>(*nextState))
        {
            setMode(INSERT);
        }
    }
}

void Editor::handleVisualBlockMode(int c)
{
    if(dispatchModeKey(c))
    {
        return;
    }

    // Delegate to state machine implementation in visual_mode.cpp
    VisualBlockMode state;
    ModeContext ctx = createModeContext(this);
    int key = (c);
    auto nextState = state.handle(ctx, key);

    // Handle state transition if returned
    if(nextState.has_value())
    {
        if(std::holds_alternative<NormalMode>(*nextState))
        {
            setMode(NORMAL);
        }
        else if(std::holds_alternative<InsertMode>(*nextState))
        {
            setMode(INSERT);
        }
    }
}

void Editor::handleSearchMode(int c)
{
    if(dispatchModeKey(c))
    {
        return;
    }

    // Determine which search mode we're in and delegate accordingly
    if(currentMode == SEARCH_FORWARD)
    {
        SearchForwardMode state;
        ModeContext ctx = createModeContext(this);
        int key = (c);
        auto nextState = state.handle(ctx, key);

        if(nextState.has_value() &&
           std::holds_alternative<NormalMode>(*nextState))
        {
            setMode(NORMAL);
        }
    }
    else if(currentMode == SEARCH_BACKWARD)
    {
        SearchBackwardMode state;
        ModeContext ctx = createModeContext(this);
        int key = (c);
        auto nextState = state.handle(ctx, key);

        if(nextState.has_value() &&
           std::holds_alternative<NormalMode>(*nextState))
        {
            setMode(NORMAL);
        }
    }
}
