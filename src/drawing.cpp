#include "editor.h"
#include "terminal.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

void Editor::drawRows()
{
    for(int y = 0; y < screenRows; y++)
    {
        int fileRow = y + *offsetY;
        Terminal::clearLine();

        if(fileRow >= lines->size())
        {
            Terminal::write('~');
        }
        else
        {
            const std::string& line = (*lines)[fileRow];

            for(int x = 0; x < screenCols; x++)
            {
                int fileCol = x + *offsetX;
                char ch = (fileCol < line.length()) ? line[fileCol] : ' ';

                bool isCursor = (fileRow == *cursorY && fileCol == *cursorX);

                bool inBlock = (currentMode == VISUAL_BLOCK &&
                                isInVisualBlock(fileRow, fileCol));

                bool inVisual =
                    ((currentMode == VISUAL || currentMode == VISUAL_LINE) &&
                     isInSelection(fileRow, fileCol));

                Terminal::write(theme.reset());

                /* ---------- VISUAL BLOCK ---------- */
                if(inBlock)
                {
                    if(isCursor)
                    {
                        // Cursor highlight (Neovim style)
                        Terminal::write(theme.cursor());
                        Terminal::write(ch);
                    }
                    else
                    {
                        // Visual block highlight
                        Terminal::write(theme.selection());
                        Terminal::write(ch);
                    }
                }
                /* ---------- VISUAL / VISUAL LINE ---------- */
                else if(inVisual)
                {
                    Terminal::write(theme.selection());
                    Terminal::write(ch);
                }
                /* ---------- NORMAL ---------- */
                else
                {
                    Terminal::write(ch);
                }

                Terminal::write(theme.reset());
            }
        }

        Terminal::write("\r\n");
    }
}

void Editor::drawStatusBar()
{
    Terminal::write(Terminal::NEWLINE_CLEAR);
    Terminal::write(theme.statusBar());

    std::string status = " " + getModeString() + " | ";

    // Add buffer indicator
    if(buffers.size() > 1)
    {
        status += "[" + std::to_string(currentBufferIndex + 1) + "/" +
                  std::to_string(buffers.size()) + "] ";
    }

    char rightStatus[32];
    snprintf(rightStatus, sizeof(rightStatus), " %d:%d ", *cursorY + 1,
             *cursorX + 1);

    std::string searchInfo;
    if(!searchQuery.empty())
    {
        if(!searchMatches.empty())
        {
            searchInfo = " [" + std::to_string(currentMatchIndex + 1) + "/" +
                         std::to_string(searchMatches.size()) + "]";
        }
        else
        {
            searchInfo = " [No matches]";
        }
    }

    std::string rightBlock = searchInfo + rightStatus;

    // Calculate available space for filename
    int rightLen = rightBlock.length();
    int availableForFile = screenCols - status.length() - rightLen - 1;

    std::string displayName = filename->empty() ? "[No Name]" : *filename;
    if(*dirty)
        displayName += " [+]";

    // Truncate filename from the beginning if too long
    if((int)displayName.length() > availableForFile && availableForFile > 4)
    {
        displayName = "..." + displayName.substr(displayName.length() -
                                                 availableForFile + 3);
    }

    status += displayName;

    Terminal::write(status);

    int padding = screenCols - status.length() - rightLen;
    while(padding-- > 0)
        Terminal::write(' ');
    Terminal::write(rightBlock);

    Terminal::write(theme.reset());
}

void Editor::drawMessageBar()
{
    Terminal::write(Terminal::NEWLINE_CLEAR);

    if(currentMode == COMMAND || currentMode == SEARCH_FORWARD ||
       currentMode == SEARCH_BACKWARD)
    {
        Terminal::write(commandBuffer);
        if(currentMode == SEARCH_FORWARD || currentMode == SEARCH_BACKWARD)
        {
            if(!searchMatches.empty())
            {
                std::string matchInfo =
                    " [" + std::to_string(currentMatchIndex + 1) + "/" +
                    std::to_string(searchMatches.size()) + "]";
                Terminal::write(matchInfo);
            }
            else if(!searchQuery.empty())
            {
                Terminal::write(" [No matches]");
            }
        }
    }
    else if(!statusMessage.empty())
    {
        int msglen = std::min((int)statusMessage.length(), screenCols);
        Terminal::write(statusMessage.substr(0, msglen));
    }
}

// Optimized drawing functions
void Editor::drawScrollUpdate(int scrollDelta)
{
    if(abs(scrollDelta) >= screenRows - 2)
    {
        drawFullScreen();
        return;
    }

    std::string output;
    output.reserve(screenRows * screenCols * 2);

    bool hideCursor = (currentMode == VISUAL || currentMode == VISUAL_LINE ||
                       currentMode == VISUAL_BLOCK);
    output += Terminal::ESC_HIDE_CURSOR;

    if(scrollDelta > 0)
    {
        output += Terminal::scrollRegion(1, screenRows);

        output += Terminal::ESC_CURSOR_HOME;
        for(int i = 0; i < scrollDelta; i++)
        {
            output += Terminal::ESC_DELETE_LINE;
        }

        for(int i = 0; i < scrollDelta; i++)
        {
            int row = screenRows - scrollDelta + i;
            int fileRow = row + *offsetY;

            output += Terminal::cursorPos(row + 1, 1);
            output += theme.reset();
            output += Terminal::ESC_CLEAR_LINE;

            if(fileRow < lines->size())
            {
                const std::string& line = (*lines)[fileRow];
                int start = *offsetX;
                int len = std::min((int)line.length() - start, screenCols);
                int visibleLen = (len > 0) ? len : 0;
                bool showCursor =
                    (currentMode == VISUAL || currentMode == VISUAL_LINE ||
                     currentMode == VISUAL_BLOCK);

                if(len > 0)
                {
                    if(isCppFile() || isRobotFile() || isPythonFile() ||
                       (isJsonFile() && syntaxJson) ||
                       (isYamlFile() && syntaxYaml))
                    {
                        // Use syntax highlighting for supported files
                        renderLineWithSyntax(output, line, start, len, fileRow);
                    }
                    else
                    {
                        bool needsHighlight = false;
                        if(showCursor && fileRow == *cursorY)
                        {
                            int cursorCol = *cursorX - *offsetX;
                            if(cursorCol >= 0 && cursorCol < len)
                                needsHighlight = true;
                        }
                        for(int x = 0; x < len; x++)
                        {
                            int col = x + *offsetX;
                            if(isInSelection(fileRow, col) ||
                               isInVisualBlock(fileRow, col) ||
                               isInSearchMatch(fileRow, col))
                            {
                                needsHighlight = true;
                                break;
                            }
                        }

                        if(!needsHighlight)
                        {
                            output.append(line, start, len);
                        }
                        else
                        {
                            for(int x = 0; x < len; x++)
                            {
                                int col = x + *offsetX;
                                bool highlighted = false;
                                bool isCursor =
                                    showCursor &&
                                    (fileRow == *cursorY && col == *cursorX);
                                if(isCursor)
                                {
                                    output += theme.cursor();
                                    highlighted = true;
                                }
                                else if(isInSelection(fileRow, col) ||
                                        isInVisualBlock(fileRow, col))
                                {
                                    output += theme.selection();
                                    highlighted = true;
                                }
                                else if(isInSearchMatch(fileRow, col))
                                {
                                    output += theme.searchMatch();
                                    highlighted = true;
                                }

                                output += line[col];

                                if(highlighted)
                                {
                                    output += theme.reset();
                                }
                            }
                        }
                    }
                }

                if(showCursor && fileRow == *cursorY)
                {
                    int cursorCol = *cursorX - *offsetX;
                    if(cursorCol >= visibleLen && cursorCol < screenCols)
                    {
                        output += theme.reset();
                        int pad = cursorCol - visibleLen;
                        if(pad > 0)
                            output.append(pad, ' ');
                        output += theme.cursor();
                        output += ' ';
                        output += theme.reset();
                    }
                }
            }
            else
            {
                output += theme.uiGutter();
                output += "~";
                output += theme.baseFg();
            }
        }

        output += Terminal::ESC_RESET_SCROLL_REGION;
    }
    else if(scrollDelta < 0)
    {
        int absDelta = -scrollDelta;

        output += Terminal::scrollRegion(1, screenRows);

        output += Terminal::ESC_CURSOR_HOME;
        for(int i = 0; i < absDelta; i++)
        {
            output += Terminal::ESC_INSERT_LINE;
        }

        for(int i = 0; i < absDelta; i++)
        {
            int fileRow = i + *offsetY;

            output += Terminal::cursorPos(i + 1, 1);
            output += theme.reset();
            output += Terminal::ESC_CLEAR_LINE;

            if(fileRow < lines->size())
            {
                const std::string& line = (*lines)[fileRow];
                int start = *offsetX;
                int len = std::min((int)line.length() - start, screenCols);
                int visibleLen = (len > 0) ? len : 0;
                bool showCursor =
                    (currentMode == VISUAL || currentMode == VISUAL_LINE ||
                     currentMode == VISUAL_BLOCK);

                if(len > 0)
                {
                    if(isCppFile() || isRobotFile() || isPythonFile() ||
                       (isJsonFile() && syntaxJson) ||
                       (isYamlFile() && syntaxYaml))
                    {
                        // Use syntax highlighting for supported files
                        renderLineWithSyntax(output, line, start, len, fileRow);
                    }
                    else
                    {
                        bool needsHighlight = false;
                        if(showCursor && fileRow == *cursorY)
                        {
                            int cursorCol = *cursorX - *offsetX;
                            if(cursorCol >= 0 && cursorCol < len)
                                needsHighlight = true;
                        }
                        for(int x = 0; x < len; x++)
                        {
                            int col = x + *offsetX;
                            if(isInSelection(fileRow, col) ||
                               isInVisualBlock(fileRow, col) ||
                               isInSearchMatch(fileRow, col))
                            {
                                needsHighlight = true;
                                break;
                            }
                        }

                        if(!needsHighlight)
                        {
                            output.append(line, start, len);
                        }
                        else
                        {
                            for(int x = 0; x < len; x++)
                            {
                                int col = x + *offsetX;
                                bool highlighted = false;
                                bool isCursor =
                                    showCursor &&
                                    (fileRow == *cursorY && col == *cursorX);
                                if(isCursor)
                                {
                                    output += theme.cursor();
                                    highlighted = true;
                                }
                                else if(isInSelection(fileRow, col) ||
                                        isInVisualBlock(fileRow, col))
                                {
                                    output += theme.selection();
                                    highlighted = true;
                                }
                                else if(isInSearchMatch(fileRow, col))
                                {
                                    output += theme.searchMatch();
                                    highlighted = true;
                                }

                                output += line[col];

                                if(highlighted)
                                {
                                    output += theme.reset();
                                }
                            }
                        }
                    }
                }

                if(showCursor && fileRow == *cursorY)
                {
                    int cursorCol = *cursorX - *offsetX;
                    if(cursorCol >= visibleLen && cursorCol < screenCols)
                    {
                        output += theme.reset();
                        int pad = cursorCol - visibleLen;
                        if(pad > 0)
                            output.append(pad, ' ');
                        output += theme.cursor();
                        output += ' ';
                        output += theme.reset();
                    }
                }
            }
            else
            {
                output += theme.uiGutter();
                output += "~";
                output += theme.baseFg();
            }
        }

        output += Terminal::ESC_RESET_SCROLL_REGION;
    }

    drawStatusBarQuick();
    drawMessageBarQuick(); // Add this to redraw message bar

    // Calculate cursor position
    int cursorRow, cursorCol;
    if(currentMode == COMMAND || currentMode == SEARCH_FORWARD ||
       currentMode == SEARCH_BACKWARD)
    {
        cursorRow = screenRows + 2;
        cursorCol = commandBuffer.length() + 1;
    }
    else
    {
        cursorRow = (*cursorY - *offsetY) + 1;
        cursorCol = (*cursorX - *offsetX) + 1;
    }
    output += Terminal::cursorPos(cursorRow, cursorCol);
    if(!hideCursor)
        output += Terminal::ESC_SHOW_CURSOR;

    lastCursorScreenY = cursorRow;
    lastCursorScreenX = cursorCol;

    Terminal::write(output);
    Terminal::flush();
}

void Editor::drawStatusBarQuick()
{
    std::string output;
    output += Terminal::cursorPos(screenRows + 1, 1);

    output += Terminal::ESC_CLEAR_LINE;
    output += theme.statusBar();

    std::string statusLeft = " " + getModeString() + " | ";

    if(buffers.size() > 1)
    {
        statusLeft += "[" + std::to_string(currentBufferIndex + 1) + "/" +
                      std::to_string(buffers.size()) + "] ";
    }

    char rightStatus[32];
    snprintf(rightStatus, sizeof(rightStatus), " %d:%d ", *cursorY + 1,
             *cursorX + 1);

    std::string searchInfo;
    if(!searchQuery.empty())
    {
        if(!searchMatches.empty())
        {
            searchInfo = " [" + std::to_string(currentMatchIndex + 1) + "/" +
                         std::to_string(searchMatches.size()) + "]";
        }
        else
        {
            searchInfo = " [No matches]";
        }
    }

    std::string rightBlock = searchInfo + rightStatus;

    // Calculate available space for filename
    int rightLen = rightBlock.length();
    int availableForFile = screenCols - statusLeft.length() - rightLen - 1;

    std::string displayName = filename->empty() ? "[No Name]" : *filename;
    if(*dirty)
        displayName += " [+]";

    // Truncate filename from the beginning if too long
    if((int)displayName.length() > availableForFile && availableForFile > 4)
    {
        displayName = "..." + displayName.substr(displayName.length() -
                                                 availableForFile + 3);
    }

    statusLeft += displayName;

    output += statusLeft;

    int padding = screenCols - statusLeft.length() - rightLen;
    if(padding > 0)
    {
        output.append(padding, ' ');
    }
    output += rightBlock;
    output += theme.reset();

    Terminal::write(output);
}

void Editor::drawMessageBarQuick()
{
    std::string output;
    output += Terminal::cursorPos(screenRows + 2, 1);

    output += Terminal::ESC_CLEAR_LINE;

    if(currentMode == COMMAND || currentMode == SEARCH_FORWARD ||
       currentMode == SEARCH_BACKWARD)
    {
        output += commandBuffer;
        if(currentMode == SEARCH_FORWARD || currentMode == SEARCH_BACKWARD)
        {
            if(!searchMatches.empty())
            {
                output += " [" + std::to_string(currentMatchIndex + 1) + "/" +
                          std::to_string(searchMatches.size()) + "]";
            }
            else if(!searchQuery.empty())
            {
                output += " [No matches]";
            }
        }
    }
    else if(!statusMessage.empty())
    {
        int msglen = std::min((int)statusMessage.length(), screenCols);
        output.append(statusMessage, 0, msglen);
    }

    // Completion popup (clangd)
    drawCompletionPopup(output);

    Terminal::write(output);
}

void Editor::drawFullScreen()
{
    adjustViewport();

    std::string output;
    output.reserve((screenRows + 3) * screenCols * 3);

    bool hideCursor = (currentMode == VISUAL || currentMode == VISUAL_LINE ||
                       currentMode == VISUAL_BLOCK);

    if(hideCursor)
        output += Terminal::ESC_HIDE_CURSOR;
    else
        output += Terminal::ESC_SHOW_CURSOR;

    output += theme.reset();
    output += Terminal::ESC_CLEAR_SCREEN;
    output += Terminal::ESC_CURSOR_HOME;

    for(int y = 0; y < screenRows; y++)
    {
        if(y > 0)
            output += "\r\n";

        output += theme.reset();
        output += Terminal::ESC_CLEAR_LINE;

        int fileRow = y + *offsetY;

        if(fileRow >= lines->size())
        {
            bool cursorRow =
                (currentMode == VISUAL || currentMode == VISUAL_LINE ||
                 currentMode == VISUAL_BLOCK) &&
                fileRow == *cursorY;
            if(cursorRow)
            {
                int cursorCol = *cursorX - *offsetX;
                if(cursorCol <= 0)
                {
                    output += theme.cursor();
                    output += ' ';
                    output += theme.reset();
                }
                else
                {
                    output += theme.uiGutter();
                    output += "~";
                    output += theme.baseFg();
                    int pad = cursorCol - 1;
                    if(pad > 0)
                        output.append(pad, ' ');
                    output += theme.cursor();
                    output += ' ';
                    output += theme.reset();
                }
            }
            else
            {
                output += theme.uiGutter();
                output += "~";
                output += theme.baseFg();
            }
        }
        else
        {
            const std::string& line = (*lines)[fileRow];
            int start = *offsetX;
            int len = line.length() - start;
            int visibleLen = 0;

            if(len > 0)
            {
                if(len > screenCols)
                    len = screenCols;
                visibleLen = len;

                bool hasHighlighting = false;

                if(currentMode == VISUAL || currentMode == VISUAL_LINE ||
                   currentMode == VISUAL_BLOCK || !searchMatches.empty())
                {
                    if((currentMode == VISUAL || currentMode == VISUAL_LINE ||
                        currentMode == VISUAL_BLOCK) &&
                       fileRow == *cursorY)
                    {
                        int cursorCol = *cursorX - *offsetX;
                        if(cursorCol >= 0 && cursorCol < len)
                            hasHighlighting = true;
                    }
                    for(int x = 0; x < len; x++)
                    {
                        int col = x + *offsetX;
                        if(isInSelection(fileRow, col) ||
                           isInVisualBlock(fileRow, col) ||
                           isInSearchMatch(fileRow, col))
                        {
                            hasHighlighting = true;
                            break;
                        }
                    }
                }

                // Check if we should use syntax highlighting
                if(isCppFile() || isRobotFile() || isPythonFile() ||
                   (isJsonFile() && syntaxJson) || (isYamlFile() && syntaxYaml))
                {
                    // Use syntax highlighting for supported files (handles
                    // selections too)
                    renderLineWithSyntax(output, line, start, len, fileRow);
                }
                else if(!hasHighlighting)
                {
                    // No highlighting needed
                    output.append(line, start, len);
                }
                else
                {
                    // Handle selection/search highlighting for non-C++ files
                    for(int x = 0; x < len; x++)
                    {
                        int col = x + *offsetX;
                        bool highlighted = false;
                        bool showCursor = (currentMode == VISUAL ||
                                           currentMode == VISUAL_LINE ||
                                           currentMode == VISUAL_BLOCK);
                        bool isCursor = showCursor && (fileRow == *cursorY &&
                                                       col == *cursorX);
                        if(isCursor)
                        {
                            output += theme.cursor();
                            highlighted = true;
                        }
                        else if(isInSelection(fileRow, col) ||
                                isInVisualBlock(fileRow, col))
                        {
                            output += theme.selection();
                            highlighted = true;
                        }
                        else if(isInSearchMatch(fileRow, col))
                        {
                            output += theme.searchMatch();
                            highlighted = true;
                        }

                        output += line[col];

                        if(highlighted)
                        {
                            output += theme.reset();
                        }
                    }
                }
            }

            if((currentMode == VISUAL || currentMode == VISUAL_LINE ||
                currentMode == VISUAL_BLOCK) &&
               fileRow == *cursorY)
            {
                int cursorCol = *cursorX - *offsetX;
                if(cursorCol >= visibleLen && cursorCol < screenCols)
                {
                    output += theme.reset();
                    int pad = cursorCol - visibleLen;
                    if(pad > 0)
                        output.append(pad, ' ');
                    output += theme.cursor();
                    output += ' ';
                    output += theme.reset();
                }
            }
        }
    }

    // Status bar
    output += Terminal::NEWLINE_CLEAR;
    output += theme.statusBar();

    std::string statusLeft = " " + getModeString() + " | ";

    if(buffers.size() > 1)
    {
        statusLeft += "[" + std::to_string(currentBufferIndex + 1) + "/" +
                      std::to_string(buffers.size()) + "] ";
    }

    char rightStatus[32];
    snprintf(rightStatus, sizeof(rightStatus), " %d:%d ", *cursorY + 1,
             *cursorX + 1);

    std::string searchInfo;
    if(!searchQuery.empty())
    {
        if(!searchMatches.empty())
        {
            searchInfo = " [" + std::to_string(currentMatchIndex + 1) + "/" +
                         std::to_string(searchMatches.size()) + "]";
        }
        else
        {
            searchInfo = " [No matches]";
        }
    }

    std::string rightBlock = searchInfo + rightStatus;

    // Calculate available space for filename
    int rightLen = rightBlock.length();
    int availableForFile = screenCols - statusLeft.length() - rightLen - 1;

    std::string displayName = filename->empty() ? "[No Name]" : *filename;
    if(*dirty)
        displayName += " [+]";

    // Truncate filename from the beginning if too long
    if((int)displayName.length() > availableForFile && availableForFile > 4)
    {
        displayName = "..." + displayName.substr(displayName.length() -
                                                 availableForFile + 3);
    }

    statusLeft += displayName;

    output += statusLeft;

    int padding = screenCols - statusLeft.length() - rightLen;
    if(padding > 0)
        output.append(padding, ' ');
    output += rightBlock;
    output += theme.reset();

    // Message bar
    output += Terminal::NEWLINE_CLEAR;

    if(currentMode == COMMAND || currentMode == SEARCH_FORWARD ||
       currentMode == SEARCH_BACKWARD)
    {
        output += commandBuffer;
        if(currentMode == SEARCH_FORWARD || currentMode == SEARCH_BACKWARD)
        {
            if(!searchMatches.empty())
            {
                output += " [" + std::to_string(currentMatchIndex + 1) + "/" +
                          std::to_string(searchMatches.size()) + "]";
            }
            else if(!searchQuery.empty())
            {
                output += " [No matches]";
            }
        }
    }
    else if(!statusMessage.empty())
    {
        int msglen = std::min((int)statusMessage.length(), screenCols);
        output.append(statusMessage, 0, msglen);
    }

    // Completion popup (clangd)
    drawCompletionPopup(output);

    Terminal::write(output);
    updateCursorPosition();
    Terminal::flush();
}
