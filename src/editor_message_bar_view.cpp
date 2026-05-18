#include "editor_message_bar_view.h"
#include "editor.h"
#include "terminal.h"
#include "widgets/status_bar.h"

namespace
{
void appendPopups(Editor& editor, std::string& output)
{
    editor.drawCompletionPopup(output);
    editor.drawEmojiPopup(output);
    editor.drawDiagnosticPopup(output);
    editor.drawSymbolPopup(output);
    editor.drawRenamePopup(output);
    editor.drawCommandHistoryPopup(output);
    editor.drawCommandPopup(output);
}
} // namespace

void EditorMessageBarView::draw()
{
    std::string output;
    output += Terminal::NEWLINE_CLEAR;
    append(output, false);
    Terminal::write(output);
}

void EditorMessageBarView::drawQuick()
{
    std::string output;
    output += Terminal::cursorPos(editor.screenRows + 2, 1);
    output += Terminal::ESC_CLEAR_LINE;
    append(output, true);
    Terminal::write(output);
}

void EditorMessageBarView::append(std::string& output, bool includePopups)
{
    std::string blame;
    if(editor.showGitBlame && editor.showGitBlameInfo)
        blame = editor.blameFullForLine(*editor.cursorY);

    widgets::MessageBarView view{
        .currentMode = editor.currentMode,
        .screenCols = editor.screenCols,
        .commandBuffer = editor.commandBuffer,
        .searchQuery = editor.searchQuery,
        .searchMatchIndex = editor.currentMatchIndex,
        .searchMatchCount = (int)editor.searchMatches.size(),
        .commandLineMessagePrefix = editor.commandLineMessagePrefix,
        .showGitBlame = editor.showGitBlame,
        .showGitBlameInfo = editor.showGitBlameInfo,
        .blameLine = blame,
        .locMessage = editor.locMessage,
        .statusMessage = editor.statusMessage,
    };
    widgets::appendMessageBar(output, view);
    if(includePopups)
        appendPopups(editor, output);
}
