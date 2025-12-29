#include "editor.h"
#include "terminal.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "text_utils.h"

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

