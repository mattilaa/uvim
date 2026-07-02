#include "ascii.h"
#include "editor.h"
#include "editor_drawing_controller.h"
#ifdef UVIM_ENABLE_BROWSER_TOOLS
#include "file_browser_mode.h"
#endif
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#include "widgets/status_bar.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace
{
void writeGitBlameField(const Theme& theme, std::string_view blame, int width)
{
    Terminal::write(theme.uiDim());
    Terminal::write(std::string(blame));
    int blameWidth = text_utils::utf8DisplayWidth(blame);
    if(blameWidth < width)
        Terminal::write(std::string(width - blameWidth, ' '));
    Terminal::write(theme.uiGutter());
    Terminal::write(ascii::utf8(ascii::BOX_HEAVY_VERTICAL));
}

void appendGitBlameField(std::string& output, const Theme& theme,
                         std::string_view blame, int width)
{
    output += theme.uiDim();
    output.append(blame.data(), blame.size());
    int blameWidth = text_utils::utf8DisplayWidth(blame);
    if(blameWidth < width)
        output.append(width - blameWidth, ' ');
    output += theme.uiGutter();
    ascii::append(output, ascii::BOX_HEAVY_VERTICAL);
}

struct VisibleTextRange
{
    int start = 0;
    int len = 0;
    int width = 0;
};

VisibleTextRange visibleTextRange(std::string_view line, int requestedStart,
                                  int maxWidth, bool utf8Mode)
{
    VisibleTextRange range;
    range.start = std::clamp(requestedStart, 0, (int)line.size());
    if(maxWidth <= 0 || range.start >= (int)line.size())
        return range;

    if(!utf8Mode)
    {
        range.len = std::min(maxWidth, (int)line.size() - range.start);
        range.width = range.len;
        return range;
    }

    if(range.start > 0)
    {
        const unsigned char byte =
            static_cast<unsigned char>(line[(std::size_t)range.start]);
        if((byte & 0xC0) == 0x80)
            range.start = text_utils::prevUtf8CharStart(line, range.start);
    }

    int pos = range.start;
    while(pos < (int)line.size() && range.width < maxWidth)
    {
        const int next = text_utils::nextUtf8CharStart(line, pos);
        const std::string_view ch =
            line.substr((std::size_t)pos, (std::size_t)(next - pos));
        const int chWidth = std::max(0, text_utils::utf8DisplayWidth(ch));
        if(range.width + chWidth > maxWidth)
            break;
        range.width += chWidth;
        pos = next;
    }
    range.len = pos - range.start;
    return range;
}

int nextVisibleCharStart(std::string_view line, int col, int end,
                         bool utf8Mode)
{
    if(!utf8Mode)
        return std::min(col + 1, end);
    int next = text_utils::nextUtf8CharStart(line, col);
    return std::clamp(next, col + 1, end);
}

int displayColumnFromByteRange(std::string_view line, int start, int end,
                               bool utf8Mode)
{
    start = std::clamp(start, 0, (int)line.size());
    end = std::clamp(end, 0, (int)line.size());
    if(end < start)
        return 0;
    if(!utf8Mode)
        return end - start;
    return text_utils::utf8DisplayWidth(line.substr((std::size_t)start,
                                                    (std::size_t)(end - start)));
}

#ifdef UVIM_ENABLE_BROWSER_TOOLS
std::string fitToWidth(std::string text, int width)
{
    if(width <= 0)
        return {};

    while(text_utils::displayWidth(text) > width && !text.empty())
        text.pop_back();

    int visible = text_utils::displayWidth(text);
    if(visible < width)
        text.append(width - visible, ' ');
    return text;
}

std::string renderFileBrowserPaneRow(
    const editor::statemachine::FileBrowserMode& browser, const Theme& theme,
    int localRow, int paneWidth)
{
    std::string row;
    row.reserve(std::max(1, paneWidth) * 2);

    if(paneWidth <= 0)
        return row;

    if(localRow == 0)
    {
        row += theme.uiInfo();
        row += fitToWidth("  " + browser.currentDirectory, paneWidth);
        row += theme.reset();
        return row;
    }

    const int index = browser.browserOffset + localRow - 1;
    const int count = browser.filterActive
                          ? static_cast<int>(browser.filterMatches.size())
                          : static_cast<int>(browser.fileList.size());

    if(index < 0 || index >= count)
    {
        row += theme.uiGutter();
        row += "~";
        row += theme.baseFg();
        if(paneWidth > 1)
            row.append(paneWidth - 1, ' ');
        return row;
    }

    int fileIndex = index;
    if(browser.filterActive)
        fileIndex = browser.filterMatches[index];
    if(fileIndex < 0 || fileIndex >= static_cast<int>(browser.fileList.size()))
    {
        row.append(paneWidth, ' ');
        return row;
    }

    const FileEntry& entry = browser.fileList[fileIndex];
    const bool selected =
        entry.name != ".." && browser.selectedFiles.count(entry.path) > 0;
    const bool cursor = index == browser.browserCursor;

    if(cursor && selected)
    {
        row += color::rgbBg(56, 120, 72);
        row += theme.baseFg();
    }
    else if(cursor)
    {
        row += theme.selection();
    }
    else if(selected)
    {
        row += color::rgbBg(24, 64, 36);
        row += theme.baseFg();
    }
    else if(entry.isDirectory)
    {
        row += theme.uiDirectory();
    }
    else
    {
        row += theme.baseFg();
    }

    std::string label = entry.isDirectory ? "  [D] " : "      ";
    label += entry.name;
    if(entry.isDirectory && entry.name != "..")
        label += "/";

    row += fitToWidth(std::move(label), paneWidth);
    row += theme.reset();
    return row;
}
#endif
} // namespace

std::string Editor::buildTabBarLine(int width)
{
    if(!showTabs || buffers.empty())
        return "";

    width = std::max(1, width);
    const int maxLabelWidth = std::min(24, std::max(8, width / 4));

    std::vector<std::string> labels;
    labels.reserve(buffers.size());
    for(size_t bi = 0; bi < buffers.size(); ++bi)
    {
        const Buffer* buf = buffers[bi].get();
        std::string name = buf->filename.empty() ? "[No Name]" : buf->filename;
        if(!buf->filename.empty())
            name = text_utils::basename(name);
#ifndef UVIM_ENABLE_TERMINAL_COLORS
        if(static_cast<int>(bi) == currentBufferIndex)
            name += "+";
#endif
        if(buf->dirty)
            name += "*";

        std::string numberPrefix;
        if(showTabNumbers)
            numberPrefix = std::to_string(bi + 1) + ":";

        int nameMax = std::max(1, maxLabelWidth - 2 - (int)numberPrefix.size());
        if((int)name.size() > nameMax)
        {
            if(nameMax >= 3)
                name = name.substr(0, nameMax - 3) + "...";
            else
                name.resize(nameMax);
        }

        labels.push_back(" " + numberPrefix + name + " ");
    }

    int offset = std::clamp(tabBarOffset, 0, (int)labels.size() - 1);
    auto computeEnd = [&](int start, int rightReserve) -> int
    {
        int leftReserve = (start > 0) ? 1 : 0;
        int available = width - leftReserve - rightReserve;
        int used = 0;
        int end = start - 1;
        for(int i = start; i < (int)labels.size(); ++i)
        {
            int w = (int)labels[i].size();
            if(used + w > available)
                break;
            used += w;
            end = i;
        }
        if(end < start)
            end = start;
        return end;
    };

    int end = computeEnd(offset, 0);
    bool rightHidden = end < (int)labels.size() - 1;
    if(rightHidden)
        end = computeEnd(offset, 1);

    while(currentBufferIndex > end && offset < (int)labels.size() - 1)
    {
        offset++;
        end = computeEnd(offset, 0);
        rightHidden = end < (int)labels.size() - 1;
        if(rightHidden)
            end = computeEnd(offset, 1);
    }
    while(currentBufferIndex < offset && offset > 0)
    {
        offset--;
        end = computeEnd(offset, 0);
        rightHidden = end < (int)labels.size() - 1;
        if(rightHidden)
            end = computeEnd(offset, 1);
    }

    tabBarOffset = offset;

    std::string line;
    line.reserve(width * 2);

    if(offset > 0)
    {
        line += theme.uiDim();
        line += "<";
        line += theme.tabBar();
    }

    for(int i = offset; i <= end && i < (int)labels.size(); ++i)
    {
        if(i == currentBufferIndex)
        {
            line += theme.selection();
            line += labels[i];
            line += theme.tabBar();
        }
        else
        {
            line += theme.uiDim();
            line += labels[i];
            line += theme.tabBar();
        }
    }

    if(rightHidden)
    {
        line += theme.uiDim();
        line += ">";
        line += theme.tabBar();
    }

    return line;
}

void Editor::drawRows()
{
    int tabRows = tabBarRows();
    int rows = contentRows();
    int textCols = std::max(1, screenCols - gutterWidth());
    int blameWidth = gitBlameWidth();
    int numberWidth = lineNumberWidth();
    bool showNumbers = numberWidth > 0;
    std::unordered_map<int, LspDiagnosticSummary> diagnosticsByLine =
        getClangdDiagnosticsByLine();

    if(tabRows > 0)
    {
        std::string line = buildTabBarLine(screenCols);
        Terminal::write(theme.tabBar());
        Terminal::clearLine();
        Terminal::write(line);
        Terminal::write(Terminal::ESC_CLEAR_LINE);
        Terminal::write(theme.reset());
        Terminal::write("\r\n");
    }

    for(int y = 0; y < rows; y++)
    {
        int fileRow = y + *offsetY;
        Terminal::clearLine();

        auto renderGutter = [&](int row)
        {
            if(showGitBlame)
            {
                std::string blame = blameDisplayForLine(row);
                writeGitBlameField(theme, blame, blameWidth);
            }
            else
            {
                auto diagIt = diagnosticsByLine.find(row);
                if(diagIt != diagnosticsByLine.end())
                {
                    if(diagIt->second.severity == 1)
                        Terminal::write(theme.uiError());
                    else
                        Terminal::write(theme.uiWarning());
                    Terminal::write(diagIt->second.severity == 1 ? 'E' : 'W');
                }
                else
                {
                    Terminal::write(theme.uiGutter());
                    Terminal::write(' ');
                }
            }

            if(showNumbers)
            {
                std::string num;
                bool isCurrent = false;
                if(row >= 0 && row < (int)lines->size())
                {
                    if(row == *cursorY)
                    {
                        isCurrent = true;
                        num = std::to_string(row + 1);
                    }
                    else
                    {
                        int rel = std::abs(row - *cursorY);
                        num = std::to_string(rel);
                    }
                }
                Terminal::write(isCurrent ? theme.uiInfo() : theme.uiDim());
                if((int)num.size() < numberWidth)
                    Terminal::write(std::string(numberWidth - num.size(), ' '));
                Terminal::write(num);
                Terminal::write(' ');
            }

            Terminal::write(theme.baseFg());
        };

        if(fileRow >= lines->size())
        {
            renderGutter(fileRow);
            Terminal::write(theme.uiGutter());
            Terminal::write('~');
            Terminal::write(theme.baseFg());
        }
        else
        {
            const std::string& line = (*lines)[fileRow];

            renderGutter(fileRow);

            for(int x = 0, fileCol = *offsetX; x < textCols;)
            {
                const bool hasChar = fileCol < (int)line.length();
                int nextCol =
                    hasChar ? nextVisibleCharStart(line, fileCol,
                                                   (int)line.length(), utf8Mode)
                            : fileCol + 1;
                std::string ch =
                    hasChar ? line.substr((std::size_t)fileCol,
                                          (std::size_t)(nextCol - fileCol))
                            : std::string(1, ' ');
                int chWidth =
                    hasChar && utf8Mode
                        ? std::max(1, text_utils::utf8DisplayWidth(ch))
                        : 1;

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
                fileCol = nextCol;
                x += chWidth;
            }
        }

        Terminal::write("\r\n");
    }
}

void Editor::drawStatusBar()
{
    drawingController->drawStatusBar();
}

void Editor::drawMessageBar()
{
    drawingController->drawMessageBar();
}

// Optimized drawing functions
void Editor::drawScrollUpdate(int scrollDelta)
{
    if(tabBarRows() > 0)
    {
        drawFullScreen();
        return;
    }

    if(abs(scrollDelta) >= screenRows - 2)
    {
        drawFullScreen();
        return;
    }

    int textCols = std::max(1, screenCols - gutterWidth());
    int blameWidth = gitBlameWidth();
    int numberWidth = lineNumberWidth();
    bool showNumbers = numberWidth > 0;
    std::unordered_map<int, LspDiagnosticSummary> diagnosticsByLine =
        getClangdDiagnosticsByLine();

    std::string output;
    output.reserve(screenRows * screenCols * 2);
    const bool syncOutput = Terminal::useSynchronizedOutput();

    bool hideCursor = (currentMode == VISUAL || currentMode == VISUAL_LINE ||
                       currentMode == VISUAL_BLOCK);
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_BEGIN);
    output += Terminal::ESC_HIDE_CURSOR;

    auto appendGutter = [&](int row)
    {
        if(showGitBlame)
        {
            std::string blame = blameDisplayForLine(row);
            appendGitBlameField(output, theme, blame, blameWidth);
        }
        else
        {
            auto diagIt = diagnosticsByLine.find(row);
            if(diagIt != diagnosticsByLine.end())
            {
                output += (diagIt->second.severity == 1) ? theme.uiError()
                                                         : theme.uiWarning();
                output += (diagIt->second.severity == 1) ? 'E' : 'W';
            }
            else
            {
                output += theme.uiGutter();
                output += ' ';
            }
        }

        if(showNumbers)
        {
            std::string num;
            bool isCurrent = false;
            if(row >= 0 && row < (int)lines->size())
            {
                if(row == *cursorY)
                {
                    isCurrent = true;
                    num = std::to_string(row + 1);
                }
                else
                {
                    int rel = std::abs(row - *cursorY);
                    num = std::to_string(rel);
                }
            }
            output += isCurrent ? theme.uiInfo() : theme.uiDim();
            if((int)num.size() < numberWidth)
                output.append(numberWidth - num.size(), ' ');
            output += num;
            output += ' ';
        }

        output += theme.baseFg();
    };

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
                appendGutter(fileRow);

                const std::string& line = (*lines)[fileRow];
                VisibleTextRange visible =
                    visibleTextRange(line, *offsetX, textCols, utf8Mode);
                int start = visible.start;
                int len = visible.len;
                int visibleLen = visible.width;
                bool showCursor =
                    (currentMode == VISUAL || currentMode == VISUAL_LINE ||
                     currentMode == VISUAL_BLOCK);

                if(len > 0)
                {
                    if(isFileType<FileType::Cpp>() ||
                       isFileType<FileType::Mla>() ||
                       isFileType<FileType::Robot>() ||
                       isFileType<FileType::Python>() ||
                       isFileType<FileType::Asm>() ||
                       isFileType<FileType::CMake>() ||
                       isFileType<FileType::Shell>() ||
                       isFileType<FileType::Html>() ||
                       isFileType<FileType::Css>() ||
                       isFileType<FileType::JavaScript>() ||
                       isFileType<FileType::TypeScript>() ||
                       isFileType<FileType::Xml>() ||
                       isFileType<FileType::MarkupText>() ||
                       isFileType<FileType::Rdoc>() ||
                       (isFileType<FileType::Json>() && syntaxJson) ||
                       (isFileType<FileType::Yaml>() && syntaxYaml) ||
                       isFileType<FileType::Toml>())
                    {
                        // Use syntax highlighting for supported files
                        renderLineWithSyntax(output, line, start, len, fileRow);
                    }
                    else
                    {
                        bool needsHighlight = false;
                        if(showCursor && fileRow == *cursorY)
                        {
                            int cursorCol = *cursorX - start;
                            if(cursorCol >= 0 && cursorCol < len)
                                needsHighlight = true;
                        }
                        for(int x = 0; x < len;)
                        {
                            int col = x + start;
                            if(isInSelection(fileRow, col) ||
                               isInVisualBlock(fileRow, col) ||
                               isInSearchMatch(fileRow, col))
                            {
                                needsHighlight = true;
                                break;
                            }
                            x += nextVisibleCharStart(line, col, start + len,
                                                      utf8Mode) -
                                 col;
                        }

                        if(!needsHighlight)
                        {
                            output.append(line, start, len);
                        }
                        else
                        {
                            for(int x = 0; x < len;)
                            {
                                int col = x + start;
                                int nextCol = nextVisibleCharStart(
                                    line, col, start + len, utf8Mode);
                                int charLen = nextCol - col;
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

                                output.append(line, col, charLen);

                                if(highlighted)
                                {
                                    output += theme.reset();
                                }
                                x += charLen;
                            }
                        }
                    }
                }

                if(showCursor && fileRow == *cursorY)
                {
                    int cursorCol =
                        displayColumnFromByteRange(line, start, *cursorX,
                                                   utf8Mode);
                    if(*cursorX >= start + len && cursorCol < textCols)
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
                appendGutter(fileRow);
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
                appendGutter(fileRow);

                const std::string& line = (*lines)[fileRow];
                VisibleTextRange visible =
                    visibleTextRange(line, *offsetX, textCols, utf8Mode);
                int start = visible.start;
                int len = visible.len;
                int visibleLen = visible.width;
                bool showCursor =
                    (currentMode == VISUAL || currentMode == VISUAL_LINE ||
                     currentMode == VISUAL_BLOCK);

                if(len > 0)
                {
                    if(isFileType<FileType::Cpp>() ||
                       isFileType<FileType::Mla>() ||
                       isFileType<FileType::Robot>() ||
                       isFileType<FileType::Python>() ||
                       isFileType<FileType::Asm>() ||
                       isFileType<FileType::CMake>() ||
                       isFileType<FileType::Shell>() ||
                       isFileType<FileType::Html>() ||
                       isFileType<FileType::Xml>() ||
                       isFileType<FileType::MarkupText>() ||
                       isFileType<FileType::Rdoc>() ||
                       (isFileType<FileType::Json>() && syntaxJson) ||
                       (isFileType<FileType::Yaml>() && syntaxYaml) ||
                       isFileType<FileType::Toml>())
                    {
                        // Use syntax highlighting for supported files
                        renderLineWithSyntax(output, line, start, len, fileRow);
                    }
                    else
                    {
                        bool needsHighlight = false;
                        if(showCursor && fileRow == *cursorY)
                        {
                            int cursorCol = *cursorX - start;
                            if(cursorCol >= 0 && cursorCol < len)
                                needsHighlight = true;
                        }
                        for(int x = 0; x < len;)
                        {
                            int col = x + start;
                            if(isInSelection(fileRow, col) ||
                               isInVisualBlock(fileRow, col) ||
                               isInSearchMatch(fileRow, col))
                            {
                                needsHighlight = true;
                                break;
                            }
                            x += nextVisibleCharStart(line, col, start + len,
                                                      utf8Mode) -
                                 col;
                        }

                        if(!needsHighlight)
                        {
                            output.append(line, start, len);
                        }
                        else
                        {
                            for(int x = 0; x < len;)
                            {
                                int col = x + start;
                                int nextCol = nextVisibleCharStart(
                                    line, col, start + len, utf8Mode);
                                int charLen = nextCol - col;
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

                                output.append(line, col, charLen);

                                if(highlighted)
                                {
                                    output += theme.reset();
                                }
                                x += charLen;
                            }
                        }
                    }
                }

                if(showCursor && fileRow == *cursorY)
                {
                    int cursorCol =
                        displayColumnFromByteRange(line, start, *cursorX,
                                                   utf8Mode);
                    if(*cursorX >= start + len && cursorCol < textCols)
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
                appendGutter(fileRow);
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
        int promptLen = (int)commandBuffer.length();
        if(commandBuffer.empty())
            promptLen = 1;
        else if(currentMode == COMMAND && commandBuffer[0] != ':')
            promptLen += 1;
        cursorCol = promptLen + 1;
    }
    else
    {
        cursorRow = (*cursorY - *offsetY) + 1;
        if(utf8Mode && *cursorY >= 0 && *cursorY < (int)lines->size())
        {
            const std::string& line = (*lines)[*cursorY];
            int start = std::clamp(*offsetX, 0, (int)line.size());
            int end = std::clamp(*cursorX, 0, (int)line.size());
            if(end < start)
                std::swap(start, end);
            cursorCol = text_utils::utf8DisplayWidth(
                            std::string_view(line).substr(start, end - start)) +
                        1 + gutterWidth();
        }
        else
        {
            cursorCol = (*cursorX - *offsetX) + 1 + gutterWidth();
        }
    }
    output += Terminal::cursorPos(cursorRow, cursorCol);
    if(!hideCursor)
        output += Terminal::ESC_SHOW_CURSOR;

    setLastCursorScreenPosition(cursorRow, cursorCol);

    Terminal::write(output);
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_END);
    Terminal::flush();
}

void Editor::drawGutterQuick()
{
    int numberWidth = lineNumberWidth();
    if(numberWidth <= 0)
        return;

    int tabRows = tabBarRows();
    int rows = contentRows();
    int blameWidth = gitBlameWidth();
    std::unordered_map<int, LspDiagnosticSummary> diagnosticsByLine =
        getClangdDiagnosticsByLine();

    std::string output;
    output.reserve(rows * std::max(16, gutterWidth() + 8));

    auto appendGutter = [&](int row)
    {
        if(showGitBlame)
        {
            std::string blame = blameDisplayForLine(row);
            appendGitBlameField(output, theme, blame, blameWidth);
        }
        else
        {
            auto diagIt = diagnosticsByLine.find(row);
            if(diagIt != diagnosticsByLine.end())
            {
                output += (diagIt->second.severity == 1) ? theme.uiError()
                                                         : theme.uiWarning();
                output += (diagIt->second.severity == 1) ? 'E' : 'W';
            }
            else
            {
                output += theme.uiGutter();
                output += ' ';
            }
        }

        std::string num;
        bool isCurrent = false;
        if(row >= 0 && row < (int)lines->size())
        {
            if(row == *cursorY)
            {
                isCurrent = true;
                num = std::to_string(row + 1);
            }
            else
            {
                int rel = std::abs(row - *cursorY);
                num = std::to_string(rel);
            }
        }
        output += isCurrent ? theme.uiInfo() : theme.uiDim();
        if((int)num.size() < numberWidth)
            output.append(numberWidth - num.size(), ' ');
        output += num;
        output += ' ';
        output += theme.baseFg();
    };

    for(int y = 0; y < rows; ++y)
    {
        int fileRow = y + *offsetY;
        output += Terminal::cursorPos(y + 1 + tabRows, 1);
        output += theme.reset();
        appendGutter(fileRow);
    }

    Terminal::write(output);
}

void Editor::drawStatusBarQuick()
{
    drawingController->drawStatusBarQuick();
}

void Editor::drawMessageBarQuick()
{
    drawingController->drawMessageBarQuick();
}

void Editor::drawFullScreen()
{
    if(splitActive && canSplit())
    {
        drawSplitFullScreen();
        return;
    }
    drawFullScreenSingle();
}

void Editor::drawFullScreenSingle()
{
    adjustViewport();

    std::string output;
    output.reserve((screenRows + 3) * screenCols * 3);

    int textCols = std::max(1, screenCols - gutterWidth());
    int blameWidth = gitBlameWidth();
    int numberWidth = lineNumberWidth();
    bool showNumbers = numberWidth > 0;
    std::unordered_map<int, LspDiagnosticSummary> diagnosticsByLine =
        getClangdDiagnosticsByLine();

    output += Terminal::ESC_HIDE_CURSOR;

    output += theme.reset();
    // Full-screen clear causes visible flashing in tmux. Repaint in-place.
    output += Terminal::ESC_CURSOR_HOME;
    if(!Terminal::isTmux())
        output += Terminal::ESC_CLEAR_SCREEN;

    int tabRows = tabBarRows();
    int rows = contentRows();

    if(tabRows > 0)
    {
        output += theme.reset();
        output += Terminal::ESC_CLEAR_LINE;
        std::string line = buildTabBarLine(screenCols);
        output += theme.tabBar();
        output += line;
        output += Terminal::ESC_CLEAR_LINE;
        output += theme.reset();
        output += "\r\n";
    }

    auto appendGutter = [&](int row)
    {
        if(showGitBlame)
        {
            std::string blame = blameDisplayForLine(row);
            appendGitBlameField(output, theme, blame, blameWidth);
        }
        else
        {
            auto diagIt = diagnosticsByLine.find(row);
            if(diagIt != diagnosticsByLine.end())
            {
                output += (diagIt->second.severity == 1) ? theme.uiError()
                                                         : theme.uiWarning();
                output += (diagIt->second.severity == 1) ? 'E' : 'W';
            }
            else
            {
                output += theme.uiGutter();
                output += ' ';
            }
        }

        if(showNumbers)
        {
            std::string num;
            bool isCurrent = false;
            if(row >= 0 && row < (int)lines->size())
            {
                if(row == *cursorY)
                {
                    isCurrent = true;
                    num = std::to_string(row + 1);
                }
                else
                {
                    int rel = std::abs(row - *cursorY);
                    num = std::to_string(rel);
                }
            }
            output += isCurrent ? theme.uiInfo() : theme.uiDim();
            if((int)num.size() < numberWidth)
                output.append(numberWidth - num.size(), ' ');
            output += num;
            output += ' ';
        }

        output += theme.baseFg();
    };

    for(int y = 0; y < rows; y++)
    {
        if(y > 0)
            output += "\r\n";

        output += theme.reset();
        output += Terminal::ESC_CLEAR_LINE;

        int fileRow = y + *offsetY;

        if(fileRow >= lines->size())
        {
            appendGutter(fileRow);
            output += theme.uiGutter();
            output += "~";
            output += theme.baseFg();
        }
        else
        {
            appendGutter(fileRow);
            const std::string& line = (*lines)[fileRow];
            VisibleTextRange visible =
                visibleTextRange(line, *offsetX, textCols, utf8Mode);
            int start = visible.start;
            int len = visible.len;
            int visibleLen = visible.width;

            if(len > 0)
            {
                bool hasHighlighting = false;

                if(currentMode == VISUAL || currentMode == VISUAL_LINE ||
                   currentMode == VISUAL_BLOCK || !searchMatches.empty())
                {
                    if((currentMode == VISUAL || currentMode == VISUAL_LINE ||
                        currentMode == VISUAL_BLOCK) &&
                       fileRow == *cursorY)
                    {
                        int cursorCol = *cursorX - start;
                        if(cursorCol >= 0 && cursorCol < len)
                            hasHighlighting = true;
                    }
                    for(int x = 0; x < len;)
                    {
                        int col = x + start;
                        if(isInSelection(fileRow, col) ||
                           isInVisualBlock(fileRow, col) ||
                           isInSearchMatch(fileRow, col))
                        {
                            hasHighlighting = true;
                            break;
                        }
                        x += nextVisibleCharStart(line, col, start + len,
                                                  utf8Mode) -
                             col;
                    }
                }

                // Check if we should use syntax highlighting
                if(isFileType<FileType::Cpp>() || isFileType<FileType::Mla>() ||
                   isFileType<FileType::Robot>() ||
                   isFileType<FileType::Python>() ||
                   isFileType<FileType::Asm>() ||
                   isFileType<FileType::CMake>() ||
                   isFileType<FileType::Shell>() ||
                   isFileType<FileType::Html>() ||
                   isFileType<FileType::Css>() ||
                   isFileType<FileType::JavaScript>() ||
                   isFileType<FileType::TypeScript>() ||
                   isFileType<FileType::Xml>() ||
                   isFileType<FileType::MarkupText>() ||
                   isFileType<FileType::Rdoc>() ||
                   (isFileType<FileType::Json>() && syntaxJson) ||
                   (isFileType<FileType::Yaml>() && syntaxYaml) ||
                   isFileType<FileType::Toml>())
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
                    for(int x = 0; x < len;)
                    {
                        int col = x + start;
                        int nextCol =
                            nextVisibleCharStart(line, col, start + len,
                                                 utf8Mode);
                        int charLen = nextCol - col;
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

                        output.append(line, col, charLen);

                        if(highlighted)
                        {
                            output += theme.reset();
                        }
                        x += charLen;
                    }
                }
            }

            if((currentMode == VISUAL || currentMode == VISUAL_LINE ||
                currentMode == VISUAL_BLOCK) &&
               fileRow == *cursorY)
            {
                int cursorCol =
                    displayColumnFromByteRange(line, start, *cursorX, utf8Mode);
                if(*cursorX >= start + len && cursorCol < textCols)
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

    output += Terminal::NEWLINE_CLEAR;
    drawingController->appendStatusBar(output);
    output += Terminal::NEWLINE_CLEAR;
    drawingController->appendMessageBar(output, true);

    const bool syncOutput = Terminal::useSynchronizedOutput();
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_BEGIN);
    Terminal::write(output);
    updateCursorPosition(false);
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_END);
    Terminal::flush();
}

void Editor::drawSplitFullScreen()
{
    int blameWidth = gitBlameWidth();
    int tabRows = tabBarRows();

    std::vector<PaneLayout> layouts(splitPanes.size());
    std::vector<std::string> tabLines(splitPanes.size());
    for(int pane = 0; pane < static_cast<int>(splitPanes.size()); ++pane)
    {
        layouts[pane] = getPaneLayout(pane);
        int rows = std::max(1, layouts[pane].rows - tabRows);
        int cols = std::max(1, layouts[pane].cols - gutterWidth());
        adjustViewportForPane(splitPanes[pane], rows, cols);
    }

    if(tabRows > 0)
    {
        int savedIndex = currentBufferIndex;
        int savedTabOffset = tabBarOffset;
        for(int pane = 0; pane < static_cast<int>(splitPanes.size()); ++pane)
        {
            currentBufferIndex = splitPanes[pane].bufferIndex;
            tabBarOffset = splitTabBarOffset[pane];
            tabLines[pane] = buildTabBarLine(layouts[pane].cols);
            splitTabBarOffset[pane] = tabBarOffset;
        }
        currentBufferIndex = savedIndex;
        tabBarOffset = savedTabOffset;
    }

    struct PanePointerGuard
    {
        Editor* editor;
        int* cursorX;
        int* cursorY;
        int* wantedX;
        int* offsetX;
        int* offsetY;

        PanePointerGuard(Editor* ed, int pane) : editor(ed)
        {
            cursorX = ed->cursorX;
            cursorY = ed->cursorY;
            wantedX = ed->wantedX;
            offsetX = ed->offsetX;
            offsetY = ed->offsetY;
            ed->setPanePointers(pane);
        }

        ~PanePointerGuard()
        {
            editor->cursorX = cursorX;
            editor->cursorY = cursorY;
            editor->wantedX = wantedX;
            editor->offsetX = offsetX;
            editor->offsetY = offsetY;
        }
    };

    struct BufferPointerGuard
    {
        Editor* editor;
        int savedIndex;
        Buffer* savedBuffer;
        std::vector<std::string>* savedLines;
        std::string* savedFilename;
        bool* savedDirty;

        BufferPointerGuard(Editor* ed, int bufferIndex) : editor(ed)
        {
            savedIndex = ed->currentBufferIndex;
            savedBuffer = ed->currentBuffer;
            savedLines = ed->lines;
            savedFilename = ed->filename;
            savedDirty = ed->dirty;
            if(bufferIndex >= 0 &&
               bufferIndex < static_cast<int>(ed->buffers.size()))
            {
                ed->currentBufferIndex = bufferIndex;
                ed->currentBuffer = ed->buffers[bufferIndex].get();
                ed->lines = &ed->currentBuffer->lines;
                ed->filename = &ed->currentBuffer->filename;
                ed->dirty = &ed->currentBuffer->dirty;
            }
        }

        ~BufferPointerGuard()
        {
            editor->currentBufferIndex = savedIndex;
            editor->currentBuffer = savedBuffer;
            editor->lines = savedLines;
            editor->filename = savedFilename;
            editor->dirty = savedDirty;
        }
    };

#ifdef UVIM_ENABLE_PER_PANE_LSP
    std::vector<std::unordered_map<int, LspDiagnosticSummary>>
        paneDiagnosticsByLine(splitPanes.size());
    if(currentMode != INSERT && !showGitBlame)
    {
        for(int pane = 0; pane < static_cast<int>(splitPanes.size()); ++pane)
        {
            BufferPointerGuard bufferGuard(this, splitPanes[pane].bufferIndex);
            PanePointerGuard paneGuard(this, pane);
            syncClangdDiagnosticsIfNeeded(false, pane == activePane);
            syncMlangSemanticTokensIfNeeded(false, pane == activePane);
            paneDiagnosticsByLine[pane] = getClangdDiagnosticsByLine();
        }
    }
#endif

    int numberWidth = lineNumberWidth();
    bool showNumbers = numberWidth > 0;
#ifndef UVIM_ENABLE_PER_PANE_LSP
    std::unordered_map<int, LspDiagnosticSummary> diagnosticsByLine =
        getClangdDiagnosticsByLine();
#endif

    auto renderPaneRow = [&](int pane, int localRow,
                             int paneWidth) -> std::string
    {
        std::string row;
        row.reserve(paneWidth * 2);

        PaneLayout layout = getPaneLayout(pane);
        if(localRow < 0 || localRow >= layout.rows)
        {
            row.append(paneWidth, ' ');
            return row;
        }

        if(tabRows > 0 && localRow == 0)
        {
            const std::string& tabLine = tabLines[pane];
            row += theme.tabBar();
            row += tabLine;
            if(paneWidth == screenCols)
            {
                row += Terminal::ESC_CLEAR_LINE;
                row += theme.reset();
                return row;
            }
            int visible = text_utils::displayWidth(tabLine);
            if(visible < paneWidth)
                row.append(paneWidth - visible, ' ');
            row += theme.reset();
            return row;
        }

#ifdef UVIM_ENABLE_BROWSER_TOOLS
        if(currentMode == FILE_BROWSER && pane == activePane &&
           modeStateMachine)
        {
            if(auto* browser = modeStateMachine->getState<
                   editor::statemachine::FileBrowserMode>())
            {
                return renderFileBrowserPaneRow(*browser, theme,
                                                localRow - tabRows, paneWidth);
            }
        }
#endif

        BufferPointerGuard bufferGuard(this, splitPanes[pane].bufferIndex);
        PanePointerGuard guard(this, pane);
#ifdef UVIM_ENABLE_PER_PANE_LSP
        const auto& diagnosticsByLine = paneDiagnosticsByLine[pane];
#endif

        int textCols = std::max(1, paneWidth - gutterWidth());
        int fileRow = (localRow - tabRows) + *offsetY;

        auto appendGutter = [&](int rowIndex)
        {
            if(showGitBlame)
            {
                std::string blame = blameDisplayForLine(rowIndex);
                appendGitBlameField(row, theme, blame, blameWidth);
            }
            else
            {
                auto diagIt = diagnosticsByLine.find(rowIndex);
                if(diagIt != diagnosticsByLine.end())
                {
                    row += (diagIt->second.severity == 1) ? theme.uiError()
                                                          : theme.uiWarning();
                    row += (diagIt->second.severity == 1) ? 'E' : 'W';
                }
                else
                {
                    row += theme.uiGutter();
                    row += ' ';
                }
            }

            if(showNumbers)
            {
                std::string num;
                bool isCurrent = false;
                if(rowIndex >= 0 && rowIndex < (int)lines->size())
                {
                    if(rowIndex == *cursorY)
                    {
                        isCurrent = true;
                        num = std::to_string(rowIndex + 1);
                    }
                    else
                    {
                        int rel = std::abs(rowIndex - *cursorY);
                        num = std::to_string(rel);
                    }
                }
                row += isCurrent ? theme.uiInfo() : theme.uiDim();
                if((int)num.size() < numberWidth)
                    row.append(numberWidth - num.size(), ' ');
                row += num;
                row += ' ';
            }

            row += theme.baseFg();
        };

        if(fileRow >= (int)lines->size())
        {
            appendGutter(fileRow);
            row += theme.uiGutter();
            row += "~";
            row += theme.baseFg();
            int visible = gutterWidth() + 1;
            if(visible < paneWidth)
                row.append(paneWidth - visible, ' ');
            return row;
        }

        appendGutter(fileRow);
        const std::string& line = (*lines)[fileRow];
        VisibleTextRange visible =
            visibleTextRange(line, *offsetX, textCols, utf8Mode);
        int start = visible.start;
        int len = visible.len;
        int visibleLen = visible.width;

        if(len > 0)
        {
            bool hasHighlighting = false;
            if(currentMode == VISUAL || currentMode == VISUAL_LINE ||
               currentMode == VISUAL_BLOCK || !searchMatches.empty())
            {
                if((currentMode == VISUAL || currentMode == VISUAL_LINE ||
                    currentMode == VISUAL_BLOCK) &&
                   fileRow == *cursorY)
                {
                    int cursorCol = *cursorX - start;
                    if(cursorCol >= 0 && cursorCol < len)
                        hasHighlighting = true;
                }
                for(int x = 0; x < len;)
                {
                    int col = x + start;
                    if(isInSelection(fileRow, col) ||
                       isInVisualBlock(fileRow, col) ||
                       isInSearchMatch(fileRow, col))
                    {
                        hasHighlighting = true;
                        break;
                    }
                    x += nextVisibleCharStart(line, col, start + len,
                                              utf8Mode) -
                         col;
                }
            }

            if(isFileType<FileType::Cpp>() || isFileType<FileType::Mla>() ||
               isFileType<FileType::Robot>() ||
               isFileType<FileType::Python>() ||
               isFileType<FileType::Asm>() ||
               isFileType<FileType::CMake>() || isFileType<FileType::Shell>() ||
               isFileType<FileType::Html>() || isFileType<FileType::Css>() ||
               isFileType<FileType::JavaScript>() ||
               isFileType<FileType::TypeScript>() ||
               isFileType<FileType::Xml>() ||
               isFileType<FileType::MarkupText>() ||
               isFileType<FileType::Rdoc>() ||
               (isFileType<FileType::Json>() && syntaxJson) ||
               (isFileType<FileType::Yaml>() && syntaxYaml) ||
               isFileType<FileType::Toml>())
            {
                renderLineWithSyntax(row, line, start, len, fileRow);
            }
            else if(!hasHighlighting)
            {
                row.append(line, start, len);
            }
            else
            {
                for(int x = 0; x < len;)
                {
                    int col = x + start;
                    int nextCol =
                        nextVisibleCharStart(line, col, start + len, utf8Mode);
                    int charLen = nextCol - col;
                    bool highlighted = false;
                    bool showCursor =
                        (currentMode == VISUAL || currentMode == VISUAL_LINE ||
                         currentMode == VISUAL_BLOCK);
                    bool isCursor =
                        showCursor && (fileRow == *cursorY && col == *cursorX);
                    if(isCursor)
                    {
                        row += theme.cursor();
                        highlighted = true;
                    }
                    else if(isInSelection(fileRow, col) ||
                            isInVisualBlock(fileRow, col))
                    {
                        row += theme.selection();
                        highlighted = true;
                    }
                    else if(isInSearchMatch(fileRow, col))
                    {
                        row += theme.searchMatch();
                        highlighted = true;
                    }

                    row.append(line, col, charLen);

                    if(highlighted)
                    {
                        row += theme.reset();
                    }
                    x += charLen;
                }
            }
        }

        if((currentMode == VISUAL || currentMode == VISUAL_LINE ||
            currentMode == VISUAL_BLOCK) &&
           fileRow == *cursorY)
        {
            int cursorCol =
                displayColumnFromByteRange(line, start, *cursorX, utf8Mode);
            if(*cursorX >= start + len && cursorCol < textCols)
            {
                row += theme.reset();
                int pad = cursorCol - visibleLen;
                if(pad > 0)
                    row.append(pad, ' ');
                row += theme.cursor();
                row += ' ';
                row += theme.reset();
                visibleLen = cursorCol + 1;
            }
        }

        if(visibleLen < textCols)
            row.append(textCols - visibleLen, ' ');
        return row;
    };

    std::string output;
    output.reserve((screenRows + 3) * screenCols * 3);

    output += Terminal::ESC_HIDE_CURSOR;

    output += theme.reset();
    // Full-screen clear causes visible flashing in tmux. Repaint in-place.
    output += Terminal::ESC_CURSOR_HOME;
    if(!Terminal::isTmux())
        output += Terminal::ESC_CLEAR_SCREEN;

    for(int row = 0; row < screenRows; row++)
    {
        if(row > 0)
            output += "\r\n";
        output += theme.reset();
        output += Terminal::ESC_CLEAR_LINE;

        std::vector<int> panesOnRow;
        panesOnRow.reserve(splitPanes.size());
        for(int pane = 0; pane < static_cast<int>(layouts.size()); ++pane)
        {
            const PaneLayout& layout = layouts[pane];
            if(row >= layout.y && row < layout.y + layout.rows)
                panesOnRow.push_back(pane);
        }
        std::sort(panesOnRow.begin(), panesOnRow.end(),
                  [&](int a, int b)
                  {
                      if(layouts[a].x != layouts[b].x)
                          return layouts[a].x < layouts[b].x;
                      return a < b;
                  });

        int col = 0;
        for(int pane : panesOnRow)
        {
            const PaneLayout& layout = layouts[pane];
            if(layout.x > col)
                output.append(layout.x - col, ' ');
            output += renderPaneRow(pane, row - layout.y, layout.cols);
            col = std::max(col, layout.x + layout.cols);
        }
        if(col < screenCols)
            output.append(screenCols - col, ' ');
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

    int lineCount = std::max(1, lines ? (int)lines->size() : 0);
    int lineNumber = std::clamp(*cursorY + 1, 1, lineCount);
    int progress = std::clamp((lineNumber * 100) / lineCount, 0, 100);

    char rightStatusBuf[48];
    snprintf(rightStatusBuf, sizeof(rightStatusBuf), " %d%%/%d/%d ", progress,
             lineNumber, *cursorX + 1);
    std::string rightStatus = rightStatusBuf;
    const int rightFieldWidth = 12;
    int rightStatusWidth = text_utils::displayWidth(rightStatus);
    if(rightStatusWidth < rightFieldWidth)
    {
        rightStatus.insert(0, rightFieldWidth - rightStatusWidth, ' ');
    }

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

    auto lspLabel = [&](const std::string& path,
                        std::string_view fallback) -> std::string
    {
        if(path.empty())
            return std::string(fallback);
        std::string_view base = text_utils::basename(path);
        if(base.empty())
            return std::string(fallback);
        return std::string(base);
    };

    std::string lspInfo;
    if(isFileType(FileType::Cpp) && clangdLspEnabled)
    {
        lspInfo = " " + theme.uiInfo() + "[" +
                  lspLabel(clangdLspPath, "clangd") + "]" + theme.statusBar();
    }
    else if(isFileType(FileType::Python) && pythonLspEnabled)
    {
        lspInfo = " " + theme.uiInfo() + "[" +
                  lspLabel(pythonLspPath, "python") + "]" + theme.statusBar();
    }
    else if(isFileType(FileType::Robot) && robotLspEnabled)
    {
        lspInfo = " " + theme.uiInfo() + "[" + lspLabel(robotLspPath, "robot") +
                  "]" + theme.statusBar();
    }
    else if(isFileType(FileType::Mla) && mlangLspEnabled)
    {
        lspInfo = " " + theme.uiInfo() + "[" + lspLabel(mlangLspPath, "mlang") +
                  "]" + theme.statusBar();
    }

    std::string rightBlock = searchInfo;
    rightBlock += lspInfo;
    if(!lspInfo.empty())
        rightBlock.append(5, ' ');
    rightBlock += rightStatus;

    int rightLen = text_utils::displayWidth(rightBlock);
    int availableForFile = screenCols - statusLeft.length() - rightLen - 1;

    std::string displayName = filename->empty() ? "[No Name]" : *filename;
    if(*dirty)
        displayName += " [+]";

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
        if(commandBuffer.empty() && currentMode == SEARCH_FORWARD)
            output += "/";
        else if(commandBuffer.empty() && currentMode == SEARCH_BACKWARD)
            output += "?";
        else if(commandBuffer.empty())
            output += ":";
        else if(commandBuffer.front() == ':' || commandBuffer.front() == '/' ||
                commandBuffer.front() == '?')
            output += commandBuffer;
        else
            output += ":" + commandBuffer;
    }
    else if(showGitBlame && showGitBlameInfo)
    {
        std::string blame = blameFullForLine(*cursorY);
        if(!blame.empty())
        {
            if(commandLineMessagePrefix)
                output += ": ";
            output += "blame: " + blame;
        }
    }
    else if(!statusMessage.empty())
    {
        if(commandLineMessagePrefix)
            output += ": ";
        int msglen = std::min(
            (int)statusMessage.length(),
            std::max(0, screenCols - (commandLineMessagePrefix ? 2 : 0)));
        output.append(statusMessage, 0, msglen);
    }
    else if(!locMessage.empty())
    {
        if(commandLineMessagePrefix)
            output += ": ";
        int msglen = std::min(
            (int)locMessage.length(),
            std::max(0, screenCols - (commandLineMessagePrefix ? 2 : 0)));
        output.append(locMessage, 0, msglen);
    }
    else
    {
        if(commandLineMessagePrefix)
            output += ":";
    }

#ifndef UVIM_MINIMAL
    drawCompletionPopup(output);
    drawEmojiPopup(output);
    drawDiagnosticPopup(output);
    drawSymbolPopup(output);
    drawCommandHistoryPopup(output);
    drawCommandPopup(output);
#endif

    const bool syncOutput = Terminal::useSynchronizedOutput();
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_BEGIN);
    Terminal::write(output);
    updateCursorPosition(false);
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_END);
    Terminal::flush();
}
