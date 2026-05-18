#include "editor.h"
#include "editor_mode_controller.h"
#include "enablelog.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <limits.h>
#include <sstream>
#include <vector>

static bool isHeaderFile(const std::string& path)
{
    return path == ".h" || path == ".hpp";
}

void EditorModeController::handleOperatorPendingMode(int c)
{
    if(dispatchModeKey(c))
    {
        return;
    }

    // If user pressed ESC, cancel
    if(c == keyCode(control::ControlKey::ESC))
    {
        editor.setMode(NORMAL);
        editor.setStatusMessage("");
        return;
    }

    // If user pressed a digit while building a count (rare), ignore here for
    // simplicity Support keyCode(typed::TypedKey::KEY_I) or
    // keyCode(typed::TypedKey::KEY_A) to enter object-specifier substate
    if(!editor.pendingAwaitingObject && (c == keyCode(typed::TypedKey::KEY_I) ||
                                         c == keyCode(typed::TypedKey::KEY_A)))
    {
        editor.pendingAwaitingObject = true;
        editor.pendingObjectType = (char)c;
        editor.setStatusMessage(std::string("Operator: ") +
                                editor.pendingOperator + " " +
                                editor.pendingObjectType);
        return;
    }

    int startY, startX, endY, endX;
    bool rangeFound = false;

    if(editor.pendingAwaitingObject)
    {
        // Expect a text-object specifier now (e.g.
        // keyCode(command::CommandKey::KEY_LEFT_PAREN),
        // keyCode(command::CommandKey::KEY_LEFT_BRACE),
        // keyCode(command::CommandKey::KEY_DOUBLE_QUOTE),
        // keyCode(typed::TypedKey::KEY_W), etc.)
        char obj = (char)c;
        bool around =
            (editor.pendingObjectType == keyCode(typed::TypedKey::KEY_A));
        rangeFound =
            editor.getTextObjectRange(obj, around, startY, startX, endY, endX);
    }
    else
    {
        // Motion-based operator: treat c as a motion (w, b, e, $, 0, %, etc.)
        // We'll simulate the motion by saving cursor, doing it, reading
        // destination, then restoring.
        int saveX = *editor.cursorX, saveY = *editor.cursorY,
            saveWanted = *editor.wantedX, saveOffsetY = *editor.offsetY,
            saveOffsetX = *editor.offsetX;

        bool isExclusiveMotion = false; // Track if motion should be exclusive

        // Apply motion
        switch(c)
        {
        case keyCode(typed::TypedKey::KEY_W):
        {
            // For dw/cw: delete from cursor to start of next word (exclusive)
            // This is vim's behavior: delete current word + trailing whitespace
            // but stay on the same line
            const std::string& line = (*editor.lines)[*editor.cursorY];
            int end = *editor.cursorX;

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
            *editor.cursorX = end;
            // keyCode(typed::TypedKey::KEY_W) is exclusive, so we'll subtract 1
            // later
            isExclusiveMotion = true;
        }
        break;
        case keyCode(typed::TypedKey::KEY_B):
            editor.moveWordBackward();
            isExclusiveMotion =
                true; // keyCode(typed::TypedKey::KEY_B) is exclusive in vim
            break;
        case keyCode(typed::TypedKey::KEY_E):
            editor.moveToEndOfWord();
            isExclusiveMotion =
                false; // keyCode(typed::TypedKey::KEY_E) is inclusive in vim
            break;
        case keyCode(typed::TypedKey::KEY_0):
            editor.moveToLineStart();
            break;
        case keyCode(command::CommandKey::KEY_DOLLAR):
            editor.moveToLineEnd();
            break;
        case keyCode(command::CommandKey::KEY_PERCENT):
            editor.moveToMatchingBracket();
            break;
        case keyCode(typed::TypedKey::KEY_J):
            editor.moveDown(editor.pendingCount);
            break;
        case keyCode(typed::TypedKey::KEY_K):
            editor.moveUp(editor.pendingCount);
            break;
        case keyCode(typed::TypedKey::KEY_CAP_G):
            // Move to last line or specific line if count given
            if(editor.pendingCount > 0)
                editor.moveToLine(editor.pendingCount - 1);
            else
                editor.moveToLastLine();
            break;
        case keyCode(typed::TypedKey::KEY_G):
            // For gg motion - need to read next char
            {
                int nextChar = Terminal::readKey();
                if(nextChar == keyCode(typed::TypedKey::KEY_G))
                {
                    editor.moveToFirstLine();
                }
                else
                {
                    // Unsupported gg variant
                    editor.setStatusMessage("Unknown motion for operator");
                    editor.setMode(NORMAL);
                    *editor.cursorX = saveX;
                    *editor.cursorY = saveY;
                    *editor.wantedX = saveWanted;
                    *editor.offsetY = saveOffsetY;
                    *editor.offsetX = saveOffsetX;
                    return;
                }
            }
            break;
        case keyCode(command::CommandKey::KEY_LEFT_BRACE):
            // Move to beginning of paragraph (previous blank line)
            {
                int targetY = *editor.cursorY;
                // Skip current paragraph
                while(targetY > 0 && !(*editor.lines)[targetY].empty())
                    targetY--;
                // Skip blank editor.lines
                while(targetY > 0 && (*editor.lines)[targetY].empty())
                    targetY--;
                // Find beginning of previous paragraph
                while(targetY > 0 && !(*editor.lines)[targetY - 1].empty())
                    targetY--;
                *editor.cursorY = targetY;
                *editor.cursorX = 0;
            }
            break;
        case keyCode(command::CommandKey::KEY_RIGHT_BRACE):
            // Move to end of paragraph (next blank line)
            {
                int targetY = *editor.cursorY;
                int maxLine = editor.lines->size() - 1;
                // Skip current paragraph
                while(targetY < maxLine && !(*editor.lines)[targetY].empty())
                    targetY++;
                // Skip blank editor.lines
                while(targetY < maxLine && (*editor.lines)[targetY].empty())
                    targetY++;
                *editor.cursorY = targetY;
                *editor.cursorX = 0;
            }
            break;
        default:
            // unsupported motion -> cancel operator
            editor.setStatusMessage("Unknown motion for operator");
            editor.setMode(NORMAL);
            // restore
            *editor.cursorX = saveX;
            *editor.cursorY = saveY;
            *editor.wantedX = saveWanted;
            *editor.offsetY = saveOffsetY;
            *editor.offsetX = saveOffsetX;
            return;
        }

        // get destination
        int destX = *editor.cursorX, destY = *editor.cursorY;
        // restore original cursor
        *editor.cursorX = saveX;
        *editor.cursorY = saveY;
        *editor.wantedX = saveWanted;
        *editor.offsetY = saveOffsetY;
        *editor.offsetX = saveOffsetX;

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
                    endX = (*editor.lines)[endY].length() - 1;
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
        editor.setStatusMessage("No object found");
        editor.setMode(NORMAL);
        return;
    }

    // Apply operator
    char op = editor.pendingOperator;

    // DEBUG: Write to file what we're about to delete
    {
        std::ofstream dbg("/tmp/uvim_dw_debug.txt", std::ios::app);
        dbg << "=== dw operation ===" << std::endl;
        dbg << "op=" << op << std::endl;
        dbg << "startY=" << startY << " startX=" << startX << std::endl;
        dbg << "endY=" << endY << " endX=" << endX << std::endl;
        if(startY < (int)editor.lines->size())
        {
            dbg << "line[" << startY << "]=" << (*editor.lines)[startY]
                << std::endl;
            dbg << "deleting chars " << startX << " to " << endX << std::endl;
            if(startY == endY && startX < (int)(*editor.lines)[startY].length())
            {
                dbg << "text to delete: ["
                    << (*editor.lines)[startY].substr(startX, endX - startX + 1)
                    << "]" << std::endl;
            }
        }
        dbg << std::endl;
    }

    editor.applyOperatorToRange(op, startY, startX, endY, endX);

    // Clear state
    editor.pendingOperator = 0;
    editor.pendingAwaitingObject = false;
    editor.pendingObjectType = 0;
    editor.pendingCount = 0;

    if(op == keyCode(typed::TypedKey::KEY_C))
    {
        editor.setMode(INSERT);
    }
    else
    {
        editor.setMode(NORMAL);
    }
    editor.needsFullRedraw = true;
}

void EditorModeController::handleNormalMode(int c)
{
    LOG_DEBUG(LOG, "handleNormalMode c={} ('{}') commandBuffer='{}'", c,
              (char)c, editor.commandBuffer);

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

        if(!editor.lines || editor.lines->empty())
            return;

        if(*editor.cursorY < 0 || *editor.cursorY >= (int)editor.lines->size())
            return;

        std::string& line = (*editor.lines)[*editor.cursorY];
        if(*editor.cursorX < 0 || *editor.cursorX >= (int)line.size())
            return;

        line[*editor.cursorX] = (char)rc;

        editor.saveState(); // your editor.undo model saves *after* changes
        *editor.dirty = true;
        editor.needsFullRedraw =
            true; // IMPORTANT: otherwise NORMAL mode may not redraw text
        return;
    }

    if(c >= keyCode(typed::TypedKey::KEY_1) &&
       c <= keyCode(typed::TypedKey::KEY_9) && editor.repeatCount == 0 &&
       editor.commandBuffer.empty())
    {
        editor.repeatCount = c - keyCode(typed::TypedKey::KEY_0);
        return;
    }
    else if(c >= keyCode(typed::TypedKey::KEY_0) &&
            c <= keyCode(typed::TypedKey::KEY_9) && editor.repeatCount > 0)
    {
        editor.repeatCount =
            editor.repeatCount * 10 + (c - keyCode(typed::TypedKey::KEY_0));
        return;
    }
    int count = std::max(1, editor.repeatCount);

    // ----- Leader (space) prefixed commands (MUST be early) -----
    if(editor.commandBuffer == " ")
    {
        if(c == keyCode(typed::TypedKey::KEY_H))
        {
            // Leader + h: jump to alternate file (header/source)
            editor.jumpToAlternateFile();
            editor.commandBuffer.clear();
            editor.repeatCount = 0;
            return;
        }
        else if(c == keyCode(typed::TypedKey::KEY_B))
        {
            // Leader + b: start buffer command sequence
            editor.commandBuffer = " b";
            editor.setStatusMessage("Leader-b");
            editor.repeatCount = 0;
            return;
        }
        else if(c == keyCode(typed::TypedKey::KEY_Y))
        {
            // Leader + y: yank to system clipboard
            editor.yankToSystemClipboard();
            editor.commandBuffer.clear();
            editor.repeatCount = 0;
            return;
        }
        else if(c == keyCode(typed::TypedKey::KEY_P))
        {
            // Leader + p: paste from system clipboard
            editor.pasteFromSystemClipboard();
            editor.commandBuffer.clear();
            editor.repeatCount = 0;
            return;
        }
        else if(c == keyCode(typed::TypedKey::KEY_F))
        {
            // Leader + f: format file
            editor.commandBuffer.clear();
            editor.repeatCount = 0;

            if(editor.template isFileType<FileType::Python>())
            {
                editor.formatBuffer();
                return;
            }
            if(editor.template isFileType<FileType::Cpp>() ||
               isHeaderFile(*editor.filename))
            {
                editor.formatBuffer();
                return;
            }
            editor.setStatusMessage("format: unsupported file type (" +
                                    *editor.filename + ")");
            return;
        }
        else if(c == keyCode(typed::TypedKey::KEY_X))
        {
            std::string dir = ".";
            if(!editor.filename->empty())
            {
                size_t lastSlash = editor.filename->find_last_of("/");
                if(lastSlash != std::string::npos)
                {
                    dir = editor.filename->substr(0, lastSlash);
                    if(dir.empty())
                        dir = "/";
                }
            }
            editor.openFileBrowser(dir);
            return;
        }
        else if(c == keyCode(control::ControlKey::SPACE))
        {
            // Double space cancels
            editor.commandBuffer.clear();
            editor.setStatusMessage("");
            editor.repeatCount = 0;
            return;
        }
        else
        {
            // Unknown leader command - cancel
            editor.commandBuffer.clear();
            editor.setStatusMessage("");
        }
    }

    // ----- Leader-b (buffer) commands -----
    if(editor.commandBuffer == " b")
    {
        if(c == keyCode(typed::TypedKey::KEY_D))
        {
            // Leader + bd: close current buffer
            editor.commandBuffer.clear();
            editor.closeCurrentBuffer();
            editor.repeatCount = 0;
            return;
        }
        else
        {
            // Unknown buffer command - cancel
            editor.commandBuffer.clear();
            editor.setStatusMessage("");
        }
    }

    // ----- g-prefixed commands (MUST be first) -----
    if(editor.commandBuffer == "g")
    {
        if(c == keyCode(typed::TypedKey::KEY_D))
        {
            editor.goToDefinition();
            editor.repeatCount = 0;
            return;
        }
        else if(c == keyCode(typed::TypedKey::KEY_G))
        {
            editor.moveToFirstLine();
            editor.commandBuffer.clear();
            editor.repeatCount = 0;
            return;
        }
        else
        {
            // Unknown g-command → cancel
            editor.commandBuffer.clear();
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_D))
    {
        if(pendingDelete)
        {
            // dd detected
            for(int i = 0; i < count; i++)
            {
                editor.deleteLine();
            }
            if(editor.showGitBlame && editor.currentBuffer)
                editor.currentBuffer->blameValid = false;
            editor.saveState();
            editor.setStatusMessage(std::to_string(count) + " line(s) deleted");
            pendingDelete = false;
            editor.repeatCount = 0;
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
            // yy detected - yank multiple editor.lines
            LOG_DEBUG(LOG, "yy detected, count={}", count);
            editor.yankBuffer.clear();
            int startLine = *editor.cursorY;
            int endLine =
                std::min(startLine + count - 1, (int)editor.lines->size() - 1);

            for(int i = startLine; i <= endLine; i++)
            {
                editor.yankBuffer += (*editor.lines)[i] + "\n";
            }

            LOG_DEBUG(LOG,
                      "yy: editor.yankBuffer.length()={}, "
                      "editor.useSystemClipboard={}",
                      editor.yankBuffer.length(), editor.useSystemClipboard);

            int linesYanked = endLine - startLine + 1;
            std::string msg = std::to_string(linesYanked) + " line" +
                              (linesYanked > 1 ? "s" : "") + " yanked";

            if(editor.useSystemClipboard && !editor.yankBuffer.empty())
            {
                LOG_DEBUG(LOG, "yy: calling editor.setSystemClipboard");
                editor.setSystemClipboard(editor.yankBuffer);
                msg += " (copied to clipboard)";
            }

            editor.setStatusMessage(msg);
            pendingYank = false;
            editor.repeatCount = 0;
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
            int startLine = *editor.cursorY;
            int endLine =
                std::min(startLine + count - 1, (int)editor.lines->size() - 1);

            editor.autoIndentRange(startLine, endLine);

            int linesIndented = endLine - startLine + 1;
            editor.setStatusMessage(std::to_string(linesIndented) + " line" +
                                    (linesIndented > 1 ? "s" : "") +
                                    " indented");
            pendingIndent = false;
            editor.repeatCount = 0;
            editor.saveState();
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
            int startLine = *editor.cursorY;
            int endLine =
                std::min(startLine + count - 1, (int)editor.lines->size() - 1);

            for(int i = startLine; i <= endLine; i++)
            {
                int currentIndent = editor.getLineIndent(i);
                editor.indentLine(i, currentIndent + 4); // Add 4 spaces
            }

            int linesIndented = endLine - startLine + 1;
            editor.setStatusMessage(std::to_string(linesIndented) + " line" +
                                    (linesIndented > 1 ? "s" : "") + " >>");
            pendingShiftRight = false;
            editor.repeatCount = 0;
            editor.saveState();
            editor.needsFullRedraw = true;
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
            int startLine = *editor.cursorY;
            int endLine =
                std::min(startLine + count - 1, (int)editor.lines->size() - 1);

            for(int i = startLine; i <= endLine; i++)
            {
                int currentIndent = editor.getLineIndent(i);
                editor.indentLine(
                    i, std::max(0, currentIndent - 4)); // Remove 4 spaces
            }

            int linesIndented = endLine - startLine + 1;
            editor.setStatusMessage(std::to_string(linesIndented) + " line" +
                                    (linesIndented > 1 ? "s" : "") + " <<");
            pendingShiftLeft = false;
            editor.repeatCount = 0;
            editor.saveState();
            editor.needsFullRedraw = true;
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
        // keyCode(typed::TypedKey::KEY_Y) followed by motion command - enter
        // operator-pending mode
        pendingYank = false;
        editor.enterOperatorPending(keyCode(typed::TypedKey::KEY_Y));
        editor.pendingCount = count;
        // Process the motion command immediately
        handleOperatorPendingMode(c);
        editor.repeatCount = 0;
        return;
    }
    else if(pendingDelete && c != keyCode(typed::TypedKey::KEY_D))
    {
        // keyCode(typed::TypedKey::KEY_D) followed by motion command - enter
        // operator-pending mode
        pendingDelete = false;
        editor.enterOperatorPending(keyCode(typed::TypedKey::KEY_D));
        editor.pendingCount = count;
        // Process the motion command immediately
        handleOperatorPendingMode(c);
        editor.repeatCount = 0;
        return;
    }
    else if(pendingIndent && c != keyCode(command::CommandKey::KEY_EQUAL))
    {
        // keyCode(command::CommandKey::KEY_EQUAL) followed by motion command -
        // enter operator-pending mode
        pendingIndent = false;
        editor.enterOperatorPending(keyCode(command::CommandKey::KEY_EQUAL));
        editor.pendingCount = count;
        // Process the motion command immediately
        handleOperatorPendingMode(c);
        editor.repeatCount = 0;
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
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - editor.lastEscTime)
                .count();

        if(timeSinceLastEsc <= Editor::DOUBLE_ESC_TIMEOUT_MS)
        {
            if(!editor.searchMatches.empty() || !editor.searchQuery.empty())
            {
                // Double ESC detected - clear search highlights
                editor.clearSearch();
                editor.needsFullRedraw =
                    true; // Force full redraw to clear highlights
            }
            editor.setStatusMessage("");
            editor.needsFullRedraw = true;
            editor.lastEscTime =
                std::chrono::steady_clock::time_point(); // Reset
        }
        else
        {
            // First ESC or timeout exceeded
            editor.lastEscTime = now;
            // Clear any pending operations
            pendingDelete = false;
            pendingYank = false;
            pendingIndent = false;
            pendingShiftRight = false;
            pendingShiftLeft = false;
            editor.repeatCount = 0;
            editor.commandBuffer.clear();
        }
    }
    break;
    case keyCode(typed::TypedKey::KEY_I):
        editor.saveState();
        editor.setMode(INSERT);
        break;
    case keyCode(typed::TypedKey::KEY_CAP_I):
        editor.saveState();
        editor.moveToLineStart();
        editor.setMode(INSERT);
        break;
    case keyCode(typed::TypedKey::KEY_A):
        editor.saveState();
        if(*editor.cursorX < (*editor.lines)[*editor.cursorY].length())
            (*editor.cursorX)++;
        editor.setMode(INSERT);
        break;
    case keyCode(typed::TypedKey::KEY_CAP_A):
        editor.saveState();
        editor.moveToLineEnd();
        if(*editor.cursorX < (*editor.lines)[*editor.cursorY].length())
            (*editor.cursorX)++;
        editor.setMode(INSERT);
        break;
    case keyCode(typed::TypedKey::KEY_O):
    {
        // Get indentation from current line
        const std::string& currentLine = (*editor.lines)[*editor.cursorY];
        size_t indent = 0;
        while(indent < currentLine.length() &&
              (currentLine[indent] == keyCode(control::ControlKey::SPACE) ||
               currentLine[indent] == '\t'))
            indent++;

        // Insert new line below with same indentation
        std::string newLine(indent, keyCode(control::ControlKey::SPACE));
        editor.lines->insert(editor.lines->begin() + *editor.cursorY + 1,
                             newLine);
        *editor.cursorY += 1;
        *editor.cursorX = indent;

        editor.saveState();
        *editor.dirty = true;
        editor.setMode(INSERT);
        editor.needsFullRedraw = true;
        break;
    }
    case keyCode(typed::TypedKey::KEY_CAP_O):
    {
        // Insert new line above with same indentation
        const std::string& currentLine = (*editor.lines)[*editor.cursorY];
        size_t indent = 0;
        while(indent < currentLine.length() &&
              (currentLine[indent] == keyCode(control::ControlKey::SPACE) ||
               currentLine[indent] == '\t'))
            indent++;

        std::string newLine(indent, keyCode(control::ControlKey::SPACE));
        editor.lines->insert(editor.lines->begin() + *editor.cursorY, newLine);
        *editor.cursorY = std::max(0, *editor.cursorY);
        *editor.cursorX = indent;

        editor.saveState();
        *editor.dirty = true;
        editor.setMode(INSERT);
        editor.needsFullRedraw = true;
        break;
    }
    case keyCode(typed::TypedKey::KEY_V):
        editor.saveState();
        editor.setMode(VISUAL);
        break;
    case keyCode(typed::TypedKey::KEY_CAP_V):
        editor.saveState();
        editor.setMode(VISUAL_LINE);
        break;
    case 22: // Ctrl-V (ASCII 22)
        editor.saveState();
        editor.setMode(VISUAL_BLOCK);
        break;
    case keyCode(command::CommandKey::KEY_COLON):
        editor.setMode(COMMAND);
        break;
    case keyCode(command::CommandKey::KEY_SLASH):
        editor.setMode(SEARCH_FORWARD);
        break;
    case keyCode(command::CommandKey::KEY_QUESTION):
        editor.setMode(SEARCH_BACKWARD);
        break;
    case keyCode(typed::TypedKey::KEY_N):
        editor.searchNext();
        break;
    case keyCode(typed::TypedKey::KEY_CAP_N):
        editor.searchPrevious();
        break;
    case keyCode(command::CommandKey::KEY_HASH):
        // Vim-style: search backward for the word under the cursor.
        {
            std::string sym = editor.getSymbolUnderCursor();
            if(sym.empty())
            {
                editor.setStatusMessage("#: no word under cursor");
                break;
            }
            editor.searchWordUnderCursor(false);
            break;
        }
    case keyCode(command::CommandKey::KEY_ASTERISK):
        // Vim-style: search forward for the word under the cursor.
        {
            std::string sym = editor.getSymbolUnderCursor();
            if(sym.empty())
            {
                editor.setStatusMessage("*: no word under cursor");
                break;
            }
            editor.searchWordUnderCursor(true);
            break;
        }
    case keyCode(control::ControlKey::CTRL_P):
        editor.setMode(FUZZY_FIND);
        break;
    case keyCode(control::ControlKey::CTRL_W): // Ctrl+W for buffer browser
        editor.setMode(BUFFER_BROWSER);
        break;
    case keyCode(
        control::ControlKey::CTRL_S): // Ctrl+S for grep search (find in files)
        editor.setMode(GREP_SEARCH);
        break;
    case keyCode(control::ControlKey::CTRL_X):
        editor.setMode(REGEX_SEARCH);
        break;
    case keyCode(control::ControlKey::CTRL_O):
        editor.jumpBack();
        break;
    case keyCode(control::ControlKey::CTRL_I):
        editor.jumpForward();
        break;
    case keyCode(typed::TypedKey::KEY_H):
    case keyCode(navigation::NavigationKey::ARROW_LEFT):
        editor.moveLeft();
        break;
    case keyCode(typed::TypedKey::KEY_L):
    case keyCode(navigation::NavigationKey::ARROW_RIGHT):
        editor.moveRight();
        break;
    case keyCode(typed::TypedKey::KEY_J):
    case keyCode(navigation::NavigationKey::ARROW_DOWN):
        editor.moveDown();
        break;
    case keyCode(typed::TypedKey::KEY_K):
    case keyCode(navigation::NavigationKey::ARROW_UP):
        editor.moveUp();
        break;
    case keyCode(control::ControlKey::CTRL_D):
        editor.scrollHalfPageDown();
        break;
    case keyCode(control::ControlKey::CTRL_U):
        editor.scrollHalfPageUp();
        break;
    case keyCode(typed::TypedKey::KEY_W):
        editor.moveWordForward();
        break;
    case keyCode(typed::TypedKey::KEY_B):
        editor.moveWordBackward();
        break;
    case keyCode(typed::TypedKey::KEY_E):
        editor.moveToEndOfWord();
        break;
    case keyCode(typed::TypedKey::KEY_0):
        editor.moveToLineStart();
        break;
    case keyCode(command::CommandKey::KEY_DOLLAR):
        editor.moveToLineEnd();
        break;
    case keyCode(typed::TypedKey::KEY_G):
        editor.commandBuffer = "g";
        break;
    case keyCode(typed::TypedKey::KEY_CAP_G):
        editor.moveToLastLine();
        break;
    case keyCode(typed::TypedKey::KEY_X):
        editor.deleteCharAtCursor();
        editor.saveState();
        *editor.dirty = true;
        break;
    case keyCode(typed::TypedKey::KEY_S):
        // Substitute: delete char(s) under cursor and enter insert mode
        editor.deleteCharAtCursor();
        editor.saveState();
        *editor.dirty = true;
        editor.setMode(INSERT);
        break;
    case keyCode(typed::TypedKey::KEY_CAP_D):
        editor.deleteToLineEnd();
        editor.saveState();
        *editor.dirty = true;
        break;
    case keyCode(typed::TypedKey::KEY_CAP_Y):
        editor.yankLine();
        break;
    case keyCode(typed::TypedKey::KEY_CAP_J):
        // Join editor.lines: current line with next line(s)
        {
            int linesToJoin = (editor.repeatCount > 0) ? editor.repeatCount : 2;
            for(int i = 0; i < linesToJoin - 1; i++)
            {
                if(*editor.cursorY + 1 < (int)editor.lines->size())
                {
                    std::string& currentLine = (*editor.lines)[*editor.cursorY];
                    std::string& nextLine =
                        (*editor.lines)[*editor.cursorY + 1];
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
                    editor.lines->erase(editor.lines->begin() +
                                        *editor.cursorY + 1);
                }
            }
            *editor.dirty = true;
            editor.saveState();
            editor.setStatusMessage(std::to_string(linesToJoin) +
                                    " editor.lines joined");
            break;
        }
    case keyCode(typed::TypedKey::KEY_C):
        // change operator: enter operator pending (support e.g. cw, ci(, etc.)
        editor.enterOperatorPending(keyCode(typed::TypedKey::KEY_C));
        editor.pendingCount = count;
        break;
    case keyCode(typed::TypedKey::KEY_P):
        editor.pasteAfter();
        break;
    case keyCode(typed::TypedKey::KEY_CAP_P):
        editor.pasteBefore();
        break;
    case keyCode(typed::TypedKey::KEY_U):
        editor.undo();
        break;
    case keyCode(control::ControlKey::CTRL_R):
        editor.redo();
        break;
    case keyCode(command::CommandKey::KEY_PERCENT):
        editor.moveToMatchingBracket();
        break;
    default:
        if(c >= 32 && c <= 126)
        {
            // Unknown printable → clear pending ops
            pendingDelete = pendingYank = pendingIndent = false;
            pendingShiftLeft = pendingShiftRight = false;
            editor.repeatCount = 0;
            editor.commandBuffer.clear();
        }
        break;
    }

    // Handle g-prefixed commands at end (for 'gg' which needs switch case for
    // g)
    if(editor.commandBuffer == "g")
    {
        if(c == keyCode(typed::TypedKey::KEY_G))
        {
            editor.moveToFirstLine();
            editor.commandBuffer.clear();
        }
        else if(c != keyCode(typed::TypedKey::KEY_G) &&
                c != keyCode(typed::TypedKey::KEY_D))
        {
            // Unknown g-command → cancel
            editor.commandBuffer.clear();
        }
    }

    editor.repeatCount = 0; // reset after each command
}

// ============================================================================
// Mode Handler Wrappers - Delegate to state machine implementations
// ============================================================================

void EditorModeController::handleInsertMode(int c)
{
    if(dispatchModeKey(c))
    {
        return;
    }

    // Delegate to state machine implementation in insert_mode.cpp
    InsertMode state;
    ModeContext ctx = createModeContext(&editor);
    int key = (c);
    auto nextState = state.handle(ctx, key);

    // Handle state transition if returned
    if(nextState.has_value())
    {
        // Determine which mode to transition to based on the returned state
        if(std::holds_alternative<NormalMode>(*nextState))
        {
            editor.setMode(NORMAL);
        }
        else if(std::holds_alternative<InsertMode>(*nextState))
        {
            editor.setMode(INSERT);
        }
        // Add other state transitions as needed
    }
}

void EditorModeController::handleVisualMode(int c)
{
    if(dispatchModeKey(c))
    {
        return;
    }

    // Delegate to state machine implementation in visual_mode.cpp
    VisualMode state;
    ModeContext ctx = createModeContext(&editor);
    int key = (c);
    auto nextState = state.handle(ctx, key);

    // Handle state transition if returned
    if(nextState.has_value())
    {
        if(std::holds_alternative<NormalMode>(*nextState))
        {
            editor.setMode(NORMAL);
        }
        else if(std::holds_alternative<VisualLineMode>(*nextState))
        {
            editor.setMode(VISUAL_LINE);
        }
        else if(std::holds_alternative<VisualBlockMode>(*nextState))
        {
            editor.setMode(VISUAL_BLOCK);
        }
        else if(std::holds_alternative<InsertMode>(*nextState))
        {
            editor.setMode(INSERT);
        }
    }
}

void EditorModeController::handleVisualBlockMode(int c)
{
    if(dispatchModeKey(c))
    {
        return;
    }

    // Delegate to state machine implementation in visual_mode.cpp
    VisualBlockMode state;
    ModeContext ctx = createModeContext(&editor);
    int key = (c);
    auto nextState = state.handle(ctx, key);

    // Handle state transition if returned
    if(nextState.has_value())
    {
        if(std::holds_alternative<NormalMode>(*nextState))
        {
            editor.setMode(NORMAL);
        }
        else if(std::holds_alternative<InsertMode>(*nextState))
        {
            editor.setMode(INSERT);
        }
    }
}

void EditorModeController::handleCommandMode(int c)
{
    if(dispatchModeKey(c))
    {
        return;
    }

    if(c == keyCode(control::ControlKey::ESC))
    {
        editor.noteDoubleEscStatusClear();
        editor.commandBuffer.clear();
        editor.setMode(NORMAL);
        return;
    }

    if(c == keyCode(control::ControlKey::ENTER))
    {
        if(editor.commandBuffer.length() > 1)
        {
            std::string_view cmd(editor.commandBuffer);
            cmd.remove_prefix(1);
            editor.executeCommand(cmd);
        }
        editor.commandBuffer.clear();
        editor.setMode(NORMAL);
        return;
    }

    if(c == keyCode(control::ControlKey::BACKSPACE) || c == 127 || c == 8)
    {
        if(editor.commandBuffer.length() > 1)
        {
            editor.commandBuffer.pop_back();
        }
        else
        {
            editor.setMode(NORMAL);
        }
        return;
    }

    if(c >= 32 && c < 127)
    {
        editor.commandBuffer += static_cast<char>(c);
    }
}

void EditorModeController::handleSearchMode(int c)
{
    if(dispatchModeKey(c))
    {
        return;
    }

    // Determine which search mode we're in and delegate accordingly
    if(editor.currentMode == SEARCH_FORWARD)
    {
        SearchForwardMode state;
        ModeContext ctx = createModeContext(&editor);
        int key = (c);
        auto nextState = state.handle(ctx, key);

        if(nextState.has_value() &&
           std::holds_alternative<NormalMode>(*nextState))
        {
            editor.setMode(NORMAL);
        }
    }
    else if(editor.currentMode == SEARCH_BACKWARD)
    {
        SearchBackwardMode state;
        ModeContext ctx = createModeContext(&editor);
        int key = (c);
        auto nextState = state.handle(ctx, key);

        if(nextState.has_value() &&
           std::holds_alternative<NormalMode>(*nextState))
        {
            editor.setMode(NORMAL);
        }
    }
}
