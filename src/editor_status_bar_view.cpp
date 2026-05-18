#include "editor_status_bar_view.h"
#include "editor.h"
#include "terminal.h"
#include "widgets/status_bar.h"

namespace
{
std::string lspLabel(const std::string& path, std::string_view fallback)
{
    if(path.empty())
        return std::string(fallback);
    size_t slash = path.find_last_of("/\\");
    if(slash == std::string::npos)
        return path;
    if(slash + 1 >= path.size())
        return std::string(fallback);
    return path.substr(slash + 1);
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

} // namespace

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
    std::string displayName =
        (editor.filename && !editor.filename->empty()) ? *editor.filename
                                                       : "[No Name]";
    std::string modeLabel = editor.getModeString();
    std::string lspInfo = currentLspLabel(editor);

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
    };
    widgets::appendStatusBar(output, view);
}
