#pragma once

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
    void openGitPrettyLogMode();
    void openGitLogModeForFile();
    void openGitStageMode();
    void openGitDiffMode();
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
