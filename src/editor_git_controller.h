#pragma once

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
    void toggleGitBlame();
    void updateGitBlameForVisibleRange();
    std::string blameDisplayForLine(int row) const;
    std::string blameFullForLine(int row) const;
    void openGitShowCommitMode();
    std::vector<std::string> loadGitShowLines(const std::string& hash);
    void openGitLogMode();
    void openGitPrettyLogMode();
    void openGitLogModeForFile();
    void openGitStageMode();

private:
    Editor& editor;
};
