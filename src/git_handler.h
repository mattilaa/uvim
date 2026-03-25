#pragma once

#include <optional>
#include <string>
#include <vector>

class Editor;

class GitHandler
{
public:
    explicit GitHandler(Editor* editor);

    void toggleGitBlame();
    void updateGitBlameForVisibleRange();
    std::string blameDisplayForLine(int row) const;
    std::string blameFullForLine(int row) const;
    void openGitShowCommitMode();
    void openGitDiffMode();
    void openGitCommitMode();
    std::vector<std::string> loadGitShowLines(const std::string& hash);
    void openGitLogMode();
    void openGitPrettyLogMode();
    void openGitLogModeForFile();
    void openGitStageMode();
    void openGitInteractiveRebaseMode();
    void openGitRawRebaseMode();
    void addCurrentBuffer();
    std::optional<bool> currentBufferHasChanges();
    bool runGitStash(std::string& outMessage);
    bool runGitStashPop(std::string& outMessage);

private:
    Editor* editor;
    bool gitAvailableKnown = false;
    bool gitAvailable = false;

    bool ensureGitAvailable();
};
