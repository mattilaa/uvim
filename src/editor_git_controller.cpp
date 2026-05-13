#include "editor_git_controller.h"
#include "editor.h"
#include "git_handler.h"
#include "text_utils.h"

#include <algorithm>

EditorGitController::EditorGitController(Editor& editor) : editor(editor) {}

int EditorGitController::lineNumberWidth() const
{
    return editor.lineNumberWidthImpl();
}

int EditorGitController::gitBlameWidth() const
{
    return editor.gitBlameWidthImpl();
}

int EditorGitController::gutterWidth() const
{
    return editor.gutterWidthImpl();
}

void EditorGitController::toggleGitBlame()
{
    editor.toggleGitBlameImpl();
}

void EditorGitController::updateGitBlameForVisibleRange()
{
    editor.updateGitBlameForVisibleRangeImpl();
}

std::string EditorGitController::blameDisplayForLine(int row) const
{
    return editor.blameDisplayForLineImpl(row);
}

std::string EditorGitController::blameFullForLine(int row) const
{
    return editor.blameFullForLineImpl(row);
}

void EditorGitController::openGitShowCommitMode()
{
    editor.openGitShowCommitModeImpl();
}

std::vector<std::string>
EditorGitController::loadGitShowLines(const std::string& hash)
{
    return editor.loadGitShowLinesImpl(hash);
}

void EditorGitController::openGitLogMode()
{
    editor.openGitLogModeImpl();
}

void EditorGitController::openGitPrettyLogMode()
{
    editor.openGitPrettyLogModeImpl();
}

void EditorGitController::openGitLogModeForFile()
{
    editor.openGitLogModeForFileImpl();
}

void EditorGitController::openGitStageMode()
{
    editor.openGitStageModeImpl();
}

int Editor::lineNumberWidthImpl() const
{
    if(!showRelativeLineNumbers)
        return 0;
    int maxLine = 1;
    if(!buffers.empty())
    {
        for(const auto& buf : buffers)
        {
            int count = (int)buf->lines.size();
            if(count > maxLine)
                maxLine = count;
        }
    }
    if(maxLine > maxLineCountSeen)
        maxLineCountSeen = maxLine;
    return (int)std::to_string(maxLineCountSeen).length();
}

int Editor::gitBlameWidthImpl() const
{
    if(!showGitBlame || !currentBuffer)
        return 0;

    int width = 0;
    for(int row = 0; row < (int)currentBuffer->blameEntries.size(); ++row)
    {
        std::string blame = blameDisplayForLine(row);
        int blameWidth = text_utils::utf8DisplayWidth(blame);
        if(blameWidth > width)
            width = blameWidth;
    }
    return std::min(width, kGitBlameMaxWidth);
}

int Editor::gutterWidthImpl() const
{
    int width = showGitBlame ? gitBlameWidth() + 1 : kDiagnosticGutterWidth;
    int numbers = lineNumberWidth();
    if(numbers > 0)
        width += numbers + 1;
    return width;
}

void Editor::toggleGitBlameImpl()
{
    if(gitHandler)
        gitHandler->toggleGitBlame();
}

void Editor::updateGitBlameForVisibleRangeImpl()
{
    if(gitHandler)
        gitHandler->updateGitBlameForVisibleRange();
}

std::string Editor::blameDisplayForLineImpl(int row) const
{
    if(gitHandler)
        return gitHandler->blameDisplayForLine(row);
    return "";
}

std::string Editor::blameFullForLineImpl(int row) const
{
    if(gitHandler)
        return gitHandler->blameFullForLine(row);
    return "";
}

void Editor::openGitShowCommitModeImpl()
{
    if(gitHandler)
        gitHandler->openGitShowCommitMode();
}

std::vector<std::string> Editor::loadGitShowLinesImpl(const std::string& hash)
{
    if(gitHandler)
        return gitHandler->loadGitShowLines(hash);
    return {};
}

void Editor::openGitLogModeImpl()
{
    if(gitHandler)
        gitHandler->openGitLogMode();
}

void Editor::openGitPrettyLogModeImpl()
{
    if(gitHandler)
        gitHandler->openGitPrettyLogMode();
}

void Editor::openGitLogModeForFileImpl()
{
    if(gitHandler)
        gitHandler->openGitLogModeForFile();
}

void Editor::openGitStageModeImpl()
{
    if(gitHandler)
        gitHandler->openGitStageMode();
}
