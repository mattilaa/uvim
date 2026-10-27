#pragma once

#include "git_log_mode.h"

#include <optional>
#include <string>
#include <vector>

class Editor;

class EditorGitController
{
public:
    explicit EditorGitController(Editor& editor);

    int lineNumberWidth() const;
    int gitBlameWidth() const;
    int gutterWidth() const;
    void toggleGitBlame(bool includeDateTime = false);
    void updateGitBlameForVisibleRange();
    std::string blameDisplayForLine(int row) const;
    std::string blameFullForLine(int row) const;
    void openGitShowCommitMode();
    std::vector<std::string> loadGitShowLines(const std::string& hash);
    void openGitLogMode();
    void openGitLogModeForBlameLine();
    void openGitPrettyLogMode();
    void openGitLogModeForFile();
    void openGitStageMode();
    void openGitStatusMode();
    void openGitDiffMode();
    // :showchanges - unsaved buffer edits vs the file on disk, shown in
    // the git diff view.
    void openUnsavedChangesMode();
    // Fresh git log entries (after a commit rewrite changed the hashes).
    std::vector<editor::statemachine::GitLogMode::Entry> reloadGitLogEntries(
        const std::string& repoRoot,
        const std::string& filePath);
    // The diff behind :showchanges; empty with `message` set when there is
    // nothing to show.
    std::vector<std::string> unsavedChangesDiff(
        std::string& title,
        std::string& message);
    void openGitCommitMode();
    void openGitFixupMode();
    void addCurrentBuffer();
    std::optional<bool> currentBufferHasChanges();
    bool runGitStash(std::string& outMessage);
    bool runGitStashPop(std::string& outMessage);

private:
    Editor& editor;
    bool gitAvailableKnown = false;
    bool gitAvailable = false;

    bool ensureGitAvailable();
};
