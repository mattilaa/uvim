#include "renderer.h"
#include "syntax_highlighter.h"
#include "terminal.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

Renderer::Renderer(EditorContext& ctx, SyntaxHighlighter& syntax)
    : ctx(ctx), syntax(syntax)
{
}

std::string Renderer::getModeString() const
{
    switch(ctx.currentMode)
    {
    case Mode::NORMAL:
        return "NORMAL";
    case Mode::INSERT:
        return "INSERT";
    case Mode::VISUAL:
        return "VISUAL";
    case Mode::VISUAL_LINE:
        return "V-LINE";
    case Mode::VISUAL_BLOCK:
        return "V-BLOCK";
    case Mode::COMMAND:
        return "COMMAND";
    case Mode::SEARCH_FORWARD:
    case Mode::SEARCH_BACKWARD:
        return "SEARCH";
    case Mode::FILE_BROWSER:
        return "FILES";
    case Mode::FUZZY_FIND:
        return "FIND";
    case Mode::BUFFER_BROWSER:
        return "BUFFERS";
    case Mode::GREP_SEARCH:
        return "GREP";
    case Mode::OP_PENDING:
        return "OP-PEND";
    default:
        return "UNKNOWN";
    }
}

bool Renderer::isInSelection(int row, int col) const
{
    if(ctx.currentMode != Mode::VISUAL && ctx.currentMode != Mode::VISUAL_LINE)
        return false;

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
        return row >= startY && row <= endY;
    }

    if(row < startY || row > endY)
        return false;

    if(startY == endY)
    {
        return col >= startX && col <= endX;
    }

    if(row == startY)
        return col >= startX;
    if(row == endY)
        return col <= endX;

    return true;
}

bool Renderer::isInVisualBlock(int row, int col) const
{
    if(ctx.currentMode != Mode::VISUAL_BLOCK)
        return false;

    int startY = std::min(ctx.currentBuffer->visualStartY,
                          ctx.currentBuffer->visualEndY);
    int endY = std::max(ctx.currentBuffer->visualStartY,
                        ctx.currentBuffer->visualEndY);
    int startX = std::min(ctx.currentBuffer->visualStartX,
                          ctx.currentBuffer->visualEndX);
    int endX = std::max(ctx.currentBuffer->visualStartX,
                        ctx.currentBuffer->visualEndX);

    return row >= startY && row <= endY && col >= startX && col <= endX;
}

bool Renderer::isInSearchMatch(int row, int col) const
{
    for(const auto& match : ctx.searchMatches)
    {
        if(match.line == row && col >= match.startCol && col <= match.endCol)
        {
            return true;
        }
    }
    return false;
}

void Renderer::draw()
{
    refreshScreen();
}

void Renderer::refreshScreen()
{
    // Check if we need a full redraw
    if(ctx.needsFullRedraw || ctx.lastDrawnOffsetY != *ctx.offsetY)
    {
        drawFullScreen();
        ctx.needsFullRedraw = false;
        ctx.lastDrawnOffsetY = *ctx.offsetY;
    }
    else
    {
        // Quick status bar update
        drawStatusBarQuick();
        drawMessageBarQuick();
    }

    updateCursorPosition();
}

void Renderer::drawFullScreen()
{
    std::string output;
    output.reserve(ctx.screenRows * ctx.screenCols * 2);

    output += Terminal::ESC_CURSOR_HOME;

    drawRows();

    // Draw status bar at screenRows + 1
    output += Terminal::cursorPos(ctx.screenRows + 1, 1);
    drawStatusBar();

    // Draw message bar at screenRows + 2
    output += Terminal::cursorPos(ctx.screenRows + 2, 1);
    drawMessageBar();

    std::cout << output << std::flush;
}

void Renderer::drawRows()
{
    std::string output;

    for(int y = 0; y < ctx.screenRows; y++)
    {
        int fileRow = y + *ctx.offsetY;

        output += Terminal::ESC_CLEAR_LINE;

        if(fileRow >= (int)ctx.lines->size())
        {
            // Empty line - show tilde
            output += Terminal::FG_BLUE;
            output += "~";
            output += Terminal::ESC_RESET_ALL;
        }
        else
        {
            const std::string& line = (*ctx.lines)[fileRow];

            // Calculate line number width
            int lineNumWidth = std::to_string(ctx.lines->size()).length();
            lineNumWidth = std::max(lineNumWidth, 3);

            // Draw line number
            output += Terminal::FG_BRIGHT_BLACK;
            std::ostringstream numStream;
            numStream << std::setw(lineNumWidth) << (fileRow + 1);
            output += numStream.str();
            output += " ";
            output += Terminal::ESC_RESET_ALL;

            int textStartCol = lineNumWidth + 1;
            int availableCols = ctx.screenCols - textStartCol;

            // Check for selection
            bool lineInSelection = isInSelection(fileRow, 0) ||
                                   ctx.currentMode == Mode::VISUAL_LINE;
            int selStartCol = -1, selEndCol = -1;

            if(ctx.currentMode == Mode::VISUAL ||
               ctx.currentMode == Mode::VISUAL_LINE)
            {
                int startY = std::min(ctx.currentBuffer->visualStartY,
                                      ctx.currentBuffer->visualEndY);
                int endY = std::max(ctx.currentBuffer->visualStartY,
                                    ctx.currentBuffer->visualEndY);
                int startX = ctx.currentBuffer->visualStartX;
                int endX = ctx.currentBuffer->visualEndX;

                if(ctx.currentBuffer->visualStartY >
                       ctx.currentBuffer->visualEndY ||
                   (ctx.currentBuffer->visualStartY ==
                        ctx.currentBuffer->visualEndY &&
                    ctx.currentBuffer->visualStartX >
                        ctx.currentBuffer->visualEndX))
                {
                    std::swap(startX, endX);
                }

                if(fileRow >= startY && fileRow <= endY)
                {
                    if(ctx.currentMode == Mode::VISUAL_LINE)
                    {
                        selStartCol = 0;
                        selEndCol = line.length();
                    }
                    else if(startY == endY)
                    {
                        selStartCol = startX;
                        selEndCol = endX;
                    }
                    else if(fileRow == startY)
                    {
                        selStartCol = startX;
                        selEndCol = line.length();
                    }
                    else if(fileRow == endY)
                    {
                        selStartCol = 0;
                        selEndCol = endX;
                    }
                    else
                    {
                        selStartCol = 0;
                        selEndCol = line.length();
                    }
                }
            }
            else if(ctx.currentMode == Mode::VISUAL_BLOCK)
            {
                int startY = std::min(ctx.currentBuffer->visualStartY,
                                      ctx.currentBuffer->visualEndY);
                int endY = std::max(ctx.currentBuffer->visualStartY,
                                    ctx.currentBuffer->visualEndY);
                int startX = std::min(ctx.currentBuffer->visualStartX,
                                      ctx.currentBuffer->visualEndX);
                int endX = std::max(ctx.currentBuffer->visualStartX,
                                    ctx.currentBuffer->visualEndX);

                if(fileRow >= startY && fileRow <= endY)
                {
                    selStartCol = startX;
                    selEndCol = endX;
                }
            }

            // Check for search match
            int searchStartCol = -1, searchEndCol = -1;
            for(const auto& match : ctx.searchMatches)
            {
                if(match.line == fileRow)
                {
                    searchStartCol = match.startCol;
                    searchEndCol = match.endCol;
                    break;
                }
            }

            // Render line with syntax highlighting
            syntax.renderLineWithSyntax(
                output, line, fileRow, *ctx.offsetX, availableCols,
                selStartCol >= 0, selStartCol, selEndCol, searchStartCol >= 0,
                searchStartCol, searchEndCol);
        }

        if(y < ctx.screenRows - 1)
        {
            output += "\r\n";
        }
    }

    std::cout << output << std::flush;
}

void Renderer::drawStatusBar()
{
    std::string output;

    output += Terminal::ESC_REVERSE;

    // Left side: mode and filename
    std::string left = " " + getModeString() + " | ";

    std::string displayFilename =
        ctx.filename->empty() ? "[No Name]" : *ctx.filename;

    // Truncate long filenames
    int maxFilenameLen = ctx.screenCols / 2;
    if((int)displayFilename.length() > maxFilenameLen)
    {
        displayFilename =
            "..." + displayFilename.substr(displayFilename.length() -
                                           maxFilenameLen + 3);
    }

    left += displayFilename;
    if(*ctx.dirty)
    {
        left += " [+]";
    }

    // Right side: position info
    std::ostringstream rightStream;
    rightStream << "Ln " << (*ctx.cursorY + 1) << ", Col "
                << (*ctx.cursorX + 1);
    rightStream << " | " << ctx.lines->size() << " lines ";
    std::string right = rightStream.str();

    // Calculate padding
    int padding = ctx.screenCols - left.length() - right.length();
    if(padding < 0)
        padding = 0;

    output += left;
    output += std::string(padding, ' ');
    output += right;

    output += Terminal::ESC_RESET_ALL;

    std::cout << output << std::flush;
}

void Renderer::drawMessageBar()
{
    std::string output;

    output += Terminal::ESC_CLEAR_LINE;

    if(ctx.currentMode == Mode::COMMAND)
    {
        output += ":";
        output += ctx.commandBuffer;
    }
    else if(ctx.currentMode == Mode::SEARCH_FORWARD)
    {
        output += "/";
        output += ctx.searchQuery;
    }
    else if(ctx.currentMode == Mode::SEARCH_BACKWARD)
    {
        output += "?";
        output += ctx.searchQuery;
    }
    else if(!ctx.statusMessage.empty())
    {
        // Truncate long messages
        std::string msg = ctx.statusMessage;
        if((int)msg.length() > ctx.screenCols)
        {
            msg = msg.substr(0, ctx.screenCols - 3) + "...";
        }
        output += msg;
    }

    std::cout << output << std::flush;
}

void Renderer::drawStatusBarQuick()
{
    std::cout << Terminal::cursorPos(ctx.screenRows + 1, 1);
    drawStatusBar();
}

void Renderer::drawMessageBarQuick()
{
    std::cout << Terminal::cursorPos(ctx.screenRows + 2, 1);
    drawMessageBar();
}

void Renderer::updateCursorPosition()
{
    // Calculate line number width
    int lineNumWidth = std::to_string(ctx.lines->size()).length();
    lineNumWidth = std::max(lineNumWidth, 3);

    int screenX = *ctx.cursorX - *ctx.offsetX + lineNumWidth + 2;
    int screenY = *ctx.cursorY - *ctx.offsetY + 1;

    // Clamp to screen bounds
    screenX = std::max(1, std::min(screenX, ctx.screenCols));
    screenY = std::max(1, std::min(screenY, ctx.screenRows));

    std::cout << Terminal::cursorPos(screenY, screenX) << std::flush;
}

void Renderer::drawScrollUpdate(int scrollDelta)
{
    // For now, just do a full redraw
    // Could optimize with terminal scroll regions
    drawFullScreen();
}

void Renderer::drawFileBrowser()
{
    std::string output;
    output += Terminal::ESC_CURSOR_HOME;

    // Header
    output += Terminal::ESC_REVERSE;
    std::string header = " FILE BROWSER: " + ctx.currentDirectory + " ";
    header.resize(ctx.screenCols, ' ');
    output += header;
    output += Terminal::ESC_RESET_ALL;
    output += "\r\n";

    // File list
    int visibleRows = ctx.screenRows - 2;
    for(int i = 0; i < visibleRows; i++)
    {
        int fileIdx = ctx.fileBrowserOffset + i;
        output += Terminal::ESC_CLEAR_LINE;

        if(fileIdx < (int)ctx.fileList.size())
        {
            const FileEntry& entry = ctx.fileList[fileIdx];

            if(fileIdx == ctx.fileBrowserIndex)
            {
                output += Terminal::ESC_REVERSE;
            }

            if(entry.isDirectory)
            {
                output += Terminal::FG_BLUE;
                output += "  📁 ";
            }
            else
            {
                output += Terminal::FG_WHITE;
                output += "  📄 ";
            }

            output += entry.name;

            if(fileIdx == ctx.fileBrowserIndex)
            {
                output += Terminal::ESC_RESET_ALL;
            }
            else
            {
                output += Terminal::ESC_RESET_ALL;
            }
        }

        output += "\r\n";
    }

    // Footer
    output += Terminal::ESC_CLEAR_LINE;
    output += Terminal::FG_BRIGHT_BLACK;
    output += " [Enter] Open  [h] Toggle hidden  [q] Quit";
    output += Terminal::ESC_RESET_ALL;

    std::cout << output << std::flush;
}

void Renderer::drawFuzzyFind()
{
    std::string output;
    output += Terminal::ESC_CURSOR_HOME;

    // Header with search input
    output += Terminal::ESC_REVERSE;
    std::string header = " FIND FILE: " + ctx.fuzzyQuery;
    if((int)header.length() < ctx.screenCols)
    {
        header += std::string(ctx.screenCols - header.length(), ' ');
    }
    output += header;
    output += Terminal::ESC_RESET_ALL;
    output += "\r\n";

    // Results
    int visibleRows = ctx.screenRows - 2;
    for(int i = 0; i < visibleRows; i++)
    {
        int matchIdx = ctx.fuzzyScrollOffset + i;
        output += Terminal::ESC_CLEAR_LINE;

        if(matchIdx < (int)ctx.fuzzyMatches.size())
        {
            const FuzzyMatch& match = ctx.fuzzyMatches[matchIdx];

            if(matchIdx == ctx.fuzzySelectedIndex)
            {
                output += Terminal::ESC_REVERSE;
            }

            output += "  ";

            // Highlight matching characters
            const std::string& path = match.path;
            size_t matchPosIdx = 0;
            for(size_t j = 0; j < path.length(); j++)
            {
                if(matchPosIdx < match.matchPositions.size() &&
                   j == match.matchPositions[matchPosIdx])
                {
                    output += Terminal::FG_YELLOW;
                    output += Terminal::ESC_BOLD;
                    output += path[j];
                    output += Terminal::ESC_RESET_ALL;
                    if(matchIdx == ctx.fuzzySelectedIndex)
                    {
                        output += Terminal::ESC_REVERSE;
                    }
                    matchPosIdx++;
                }
                else
                {
                    output += path[j];
                }
            }

            if(matchIdx == ctx.fuzzySelectedIndex)
            {
                output += Terminal::ESC_RESET_ALL;
            }
        }

        output += "\r\n";
    }

    // Footer
    output += Terminal::ESC_CLEAR_LINE;
    output += Terminal::FG_BRIGHT_BLACK;
    output += " " + std::to_string(ctx.fuzzyMatches.size()) + " matches";
    output += Terminal::ESC_RESET_ALL;

    std::cout << output << std::flush;
}

void Renderer::drawBufferBrowser()
{
    std::string output;
    output += Terminal::ESC_CURSOR_HOME;

    // Header
    output += Terminal::ESC_REVERSE;
    std::string header = " BUFFERS: " + ctx.bufferQuery;
    if((int)header.length() < ctx.screenCols)
    {
        header += std::string(ctx.screenCols - header.length(), ' ');
    }
    output += header;
    output += Terminal::ESC_RESET_ALL;
    output += "\r\n";

    // Buffer list
    int visibleRows = ctx.screenRows - 2;
    for(int i = 0; i < visibleRows; i++)
    {
        int matchIdx = i;
        output += Terminal::ESC_CLEAR_LINE;

        if(matchIdx < (int)ctx.bufferMatches.size())
        {
            const FuzzyMatch& match = ctx.bufferMatches[matchIdx];

            if(matchIdx == ctx.bufferSelectedIndex)
            {
                output += Terminal::ESC_REVERSE;
            }

            output += "  " + std::to_string(matchIdx + 1) + ": ";
            output += match.path;

            if(matchIdx == ctx.bufferSelectedIndex)
            {
                output += Terminal::ESC_RESET_ALL;
            }
        }

        output += "\r\n";
    }

    std::cout << output << std::flush;
}

void Renderer::drawGrepSearch()
{
    std::string output;
    output += Terminal::ESC_CURSOR_HOME;

    // Header with search input
    output += Terminal::ESC_REVERSE;
    std::string header = " GREP: " + ctx.grepQuery;
    if(ctx.grepSearching)
    {
        header += " (searching...)";
    }
    if((int)header.length() < ctx.screenCols)
    {
        header += std::string(ctx.screenCols - header.length(), ' ');
    }
    output += header;
    output += Terminal::ESC_RESET_ALL;
    output += "\r\n";

    // Results
    int visibleRows = ctx.screenRows - 2;
    for(int i = 0; i < visibleRows; i++)
    {
        int matchIdx = ctx.grepScrollOffset + i;
        output += Terminal::ESC_CLEAR_LINE;

        if(matchIdx < (int)ctx.grepMatches.size())
        {
            const GrepMatch& match = ctx.grepMatches[matchIdx];

            if(matchIdx == ctx.grepSelectedIndex)
            {
                output += Terminal::ESC_REVERSE;
            }

            // Format: filepath:line: content
            output += Terminal::FG_CYAN;
            output += match.filepath;
            output += Terminal::ESC_RESET_ALL;
            if(matchIdx == ctx.grepSelectedIndex)
            {
                output += Terminal::ESC_REVERSE;
            }
            output += ":";
            output += Terminal::FG_YELLOW;
            output += std::to_string(match.lineNumber);
            output += Terminal::ESC_RESET_ALL;
            if(matchIdx == ctx.grepSelectedIndex)
            {
                output += Terminal::ESC_REVERSE;
            }
            output += ": ";

            // Show line content with match highlighted
            const std::string& content = match.lineContent;
            for(int j = 0; j < (int)content.length() && j < ctx.screenCols - 30;
                j++)
            {
                if(j >= match.matchStart && j <= match.matchEnd)
                {
                    output += Terminal::FG_RED;
                    output += Terminal::ESC_BOLD;
                    output += content[j];
                    output += Terminal::ESC_RESET_ALL;
                    if(matchIdx == ctx.grepSelectedIndex)
                    {
                        output += Terminal::ESC_REVERSE;
                    }
                }
                else
                {
                    output += content[j];
                }
            }

            if(matchIdx == ctx.grepSelectedIndex)
            {
                output += Terminal::ESC_RESET_ALL;
            }
        }

        output += "\r\n";
    }

    // Footer
    output += Terminal::ESC_CLEAR_LINE;
    output += Terminal::FG_BRIGHT_BLACK;
    output += " " + std::to_string(ctx.grepMatches.size()) + " matches";
    output += Terminal::ESC_RESET_ALL;

    std::cout << output << std::flush;
}

void Renderer::drawCompletionPopup(std::string& output) const
{
    if(!ctx.completionActive || ctx.filteredCompletions.empty())
        return;

    // Calculate popup position
    int lineNumWidth = std::to_string(ctx.lines->size()).length();
    lineNumWidth = std::max(lineNumWidth, 3);

    int popupX = *ctx.cursorX - *ctx.offsetX + lineNumWidth + 2;
    int popupY = *ctx.cursorY - *ctx.offsetY + 2; // Below cursor

    int maxItems = std::min(10, (int)ctx.filteredCompletions.size());
    int popupWidth = 40;

    // Adjust if popup would go off screen
    if(popupY + maxItems > ctx.screenRows)
    {
        popupY = *ctx.cursorY - *ctx.offsetY - maxItems;
    }
    if(popupX + popupWidth > ctx.screenCols)
    {
        popupX = ctx.screenCols - popupWidth;
    }

    // Draw popup
    for(int i = 0; i < maxItems; i++)
    {
        output += Terminal::cursorPos(popupY + i, popupX);

        if(i == ctx.completionIndex)
        {
            output += Terminal::ESC_REVERSE;
        }
        else
        {
            output += Terminal::BG_BLACK;
            output += Terminal::FG_WHITE;
        }

        const CompletionItem& item = ctx.filteredCompletions[i];
        std::string label = item.label;
        if((int)label.length() > popupWidth - 2)
        {
            label = label.substr(0, popupWidth - 5) + "...";
        }

        output += " ";
        output += label;

        // Pad to popup width
        int padding = popupWidth - label.length() - 2;
        if(padding > 0)
        {
            output += std::string(padding, ' ');
        }
        output += " ";

        output += Terminal::ESC_RESET_ALL;
    }
}
