#include "editor_git_controller.h"

#include "editor.h"

#include <algorithm>

EditorGitController::EditorGitController(Editor& editor) : editor(editor) {}

int EditorGitController::lineNumberWidth() const
{
    if(!editor.showRelativeLineNumbers)
        return 0;

    int maxLine = 1;
    for(const auto& buffer : editor.buffers)
        maxLine = std::max(maxLine, static_cast<int>(buffer->lines.size()));

    if(maxLine > editor.maxLineCountSeen)
        editor.maxLineCountSeen = maxLine;
    return static_cast<int>(std::to_string(editor.maxLineCountSeen).length());
}

int EditorGitController::gitBlameWidth() const
{
    return 0;
}

int EditorGitController::gutterWidth() const
{
    int width = Editor::kDiagnosticGutterWidth;
    int numbers = lineNumberWidth();
    if(numbers > 0)
        width += numbers + 1;
    return width;
}

void EditorGitController::toggleGitBlame(bool)
{
    editor.showGitBlame = false;
    editor.showGitBlameDateTime = false;
    editor.setStatusMessage("git tools disabled in this build");
}

void EditorGitController::updateGitBlameForVisibleRange() {}

std::string EditorGitController::blameDisplayForLine(int) const
{
    return {};
}

std::string EditorGitController::blameFullForLine(int) const
{
    return {};
}

void EditorGitController::openGitShowCommitMode()
{
    editor.setStatusMessage("git tools disabled in this build");
}

std::vector<std::string>
EditorGitController::loadGitShowLines(const std::string&)
{
    return {};
}

void EditorGitController::openGitLogMode()
{
    editor.setStatusMessage("git tools disabled in this build");
}

void EditorGitController::openGitLogModeForBlameLine()
{
    editor.setStatusMessage("git tools disabled in this build");
}

void EditorGitController::openGitPrettyLogMode()
{
    editor.setStatusMessage("git tools disabled in this build");
}

void EditorGitController::openGitLogModeForFile()
{
    editor.setStatusMessage("git tools disabled in this build");
}

void EditorGitController::openGitStageMode()
{
    editor.setStatusMessage("git tools disabled in this build");
}

void EditorGitController::openGitDiffMode()
{
    editor.setStatusMessage("git tools disabled in this build");
}

void EditorGitController::openGitCommitMode()
{
    editor.setStatusMessage("git tools disabled in this build");
}

void EditorGitController::openGitFixupMode()
{
    editor.setStatusMessage("git tools disabled in this build");
}

void EditorGitController::addCurrentBuffer()
{
    editor.setStatusMessage("git tools disabled in this build");
}

std::optional<bool> EditorGitController::currentBufferHasChanges()
{
    return std::nullopt;
}

bool EditorGitController::runGitStash(std::string& outMessage)
{
    outMessage = "git tools disabled in this build";
    return false;
}

bool EditorGitController::runGitStashPop(std::string& outMessage)
{
    outMessage = "git tools disabled in this build";
    return false;
}

bool EditorGitController::ensureGitAvailable()
{
    return false;
}
