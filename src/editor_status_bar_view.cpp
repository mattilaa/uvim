#include "editor_status_bar_view.h"
#include "editor.h"
#include "terminal.h"
#include "text_utils.h"
#include "widgets/status_bar.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace
{
std::string lspLabel(const std::string& path, std::string_view fallback)
{
    if(path.empty())
        return std::string(fallback);
    std::string_view base = text_utils::basename(path);
    if(base.empty())
        return std::string(fallback);
    return std::string(base);
}

std::string currentLspLabel(Editor& editor)
{
    if(editor.isFileType(FileType::Cpp) && editor.clangdLspEnabled)
        return lspLabel(editor.clangdLspPath, "clangd");
    if(editor.isFileType(FileType::Python) && editor.pythonLspEnabled)
        return lspLabel(editor.pythonLspPath, "python");
    if(editor.isFileType(FileType::Robot) && editor.robotLspEnabled)
        return lspLabel(editor.robotLspPath, "robot");
    if(editor.isFileType(FileType::Mla) && editor.mlangLspEnabled)
        return lspLabel(editor.mlangLspPath, "mlang");
    return {};
}

// Screen row (1-based) the next character of `output` lands on: the last
// cursor-position escape ("\x1b[H" or "\x1b[r;cH") plus the newlines after it.
// Lets self-drawn views host progress without knowing their own row math.
int rowAtEndOf(std::string_view output, int fallback)
{
    int newlines = 0;
    for(size_t i = output.size(); i-- > 0;)
    {
        if(output[i] == '\n')
        {
            ++newlines;
            continue;
        }
        if(output[i] != 'H')
            continue;
        // Walk back over "row;col" digits to the CSI introducer.
        size_t j = i;
        while(j > 0 &&
              (std::isdigit(static_cast<unsigned char>(output[j - 1])) ||
               output[j - 1] == ';'))
            --j;
        if(j < 2 || output[j - 1] != '[' || output[j - 2] != '\x1b')
            continue;
        const std::string_view params = output.substr(j, i - j);
        int row = 1;
        if(!params.empty() && params.front() != ';')
        {
            row = 0;
            for(char c : params)
            {
                if(c == ';')
                    break;
                row = row * 10 + (c - '0');
            }
        }
        return std::max(1, row) + newlines;
    }
    return fallback;
}
} // namespace

void Editor::appendViewStatusBar(
    std::string& output,
    widgets::ViewStatusBarView view)
{
    std::string progress = taskStatusText();
    int progressColumn = 0;
    view.screenCols = screenCols;
    view.toolGap = lspStatusGap;
    view.progress = progress;
    view.progressColumn = &progressColumn;
    view.progressReserve = taskStatusWidth();

    output += Terminal::cursorPos(screenRows + 1, 1);
    output += Terminal::ESC_CLEAR_LINE;
    widgets::appendViewStatusBar(output, view);
    noteTaskStatusSlot(screenRows + 1, progressColumn);

    output += Terminal::cursorPos(screenRows + 2, 1);
    output += theme.reset();
    output += Terminal::ESC_CLEAR_LINE;
    if(!statusMessage.empty())
        output.append(statusMessage, 0, std::max(0, screenCols));
}

void Editor::appendStatusLine(
    std::string& output,
    std::string left,
    const std::string& right,
    int row)
{
    if(row < 0)
        row = rowAtEndOf(output, screenRows + 1);
    output += theme.statusBar();

    std::string progress = taskStatusText();
    const int rightWidth = text_utils::displayWidth(right);
    int progressWidth =
        progress.empty() ? 0 : text_utils::displayWidth(progress) + 1;
    // Keep at least a short label visible on the left; drop the progress
    // before squeezing the view's own text.
    if(progressWidth > 0 && screenCols - rightWidth - progressWidth < 16)
    {
        progress.clear();
        progressWidth = 0;
    }

    const int leftBudget = std::max(0, screenCols - rightWidth - progressWidth);
    if(text_utils::displayWidth(left) > leftBudget)
    {
        std::string fitted;
        int width = 0;
        for(size_t i = 0; i < left.size();)
        {
            const unsigned char lead = static_cast<unsigned char>(left[i]);
            const size_t len = lead < 0x80   ? 1
                               : lead < 0xE0 ? 2
                               : lead < 0xF0 ? 3
                                             : 4;
            if(width + 1 > leftBudget)
                break;
            fitted.append(left, i, len);
            ++width;
            i += len;
        }
        left = std::move(fitted);
    }
    output += left;

    const int padding = screenCols - text_utils::displayWidth(left) -
                        progressWidth - rightWidth;
    if(padding > 0)
        output.append(padding, ' ');
    if(!progress.empty())
    {
        noteTaskStatusSlot(row, screenCols - rightWidth - progressWidth + 1);
        output += progress;
        output += ' ';
    }
    else
    {
        // Nothing running: remember where progress would go (in the
        // padding) so the first tick paints in place; -1 when no room.
        const int reserve = taskStatusWidth() + 1;
        const bool room = reserve > 1 && padding >= reserve &&
                          screenCols - rightWidth - reserve >= 16;
        noteTaskStatusSlot(
            row,
            room ? screenCols - rightWidth - reserve + 1 : -1);
    }
    if(rightWidth <= screenCols)
        output += right;
    output += theme.reset();
}

void EditorStatusBarView::draw()
{
    std::string output;
    output += Terminal::NEWLINE_CLEAR;
    append(output);
    Terminal::write(output);
}

void EditorStatusBarView::drawQuick()
{
    std::string output;
    output += Terminal::cursorPos(editor.screenRows + 1, 1);
    output += Terminal::ESC_CLEAR_LINE;
    append(output);
    Terminal::write(output);
}

void EditorStatusBarView::append(std::string& output)
{
    std::string displayName = (editor.filename && !editor.filename->empty())
                                  ? *editor.filename
                                  : "[No Name]";
    std::string modeLabel = editor.getModeString();
    std::string lspInfo = currentLspLabel(editor);
    std::string progress = editor.taskStatusText();
    int progressColumn = 0;

    widgets::StatusBarView view{
        .theme = editor.theme,
        .screenCols = editor.screenCols,
        .modeLabel = modeLabel,
        .currentBufferIndex = editor.currentBufferIndex,
        .bufferCount = (int)editor.buffers.size(),
        .cursorY = *editor.cursorY,
        .cursorX = *editor.cursorX,
        .lineCount = editor.lines ? (int)editor.lines->size() : 0,
        .filename = displayName,
        .dirty = editor.dirty && *editor.dirty,
        .searchQuery = editor.searchQuery,
        .searchMatchIndex = editor.currentMatchIndex,
        .searchMatchCount = (int)editor.searchMatches.size(),
        .lspLabel = lspInfo,
        .lspGap = editor.lspStatusGap,
        .progress = progress,
        .progressColumn = &progressColumn,
        .progressReserve = editor.taskStatusWidth(),
    };
    widgets::appendStatusBar(output, view);
    editor.noteTaskStatusSlot(editor.screenRows + 1, progressColumn);
}
