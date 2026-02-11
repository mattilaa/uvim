#pragma once

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
    std::vector<std::string> loadGitShowLines(const std::string& hash);
    void openGitLogMode();
    void openGitLogModeForFile();
    void openGitStageMode();
    bool runGitStash(std::string& outMessage);
    bool runGitStashPop(std::string& outMessage);

private:
    Editor* editor;
    bool gitAvailableKnown = false;
    bool gitAvailable = false;

    bool ensureGitAvailable();
};
