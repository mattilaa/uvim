#pragma once

#include "mode.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Editor;

class EditorCommandController
{
public:
    explicit EditorCommandController(Editor& editor);

    std::optional<std::string> commandHistoryUp();
    std::optional<std::string> commandHistoryDown();
    void startCommandPopup();
    void cancelCommandPopup();
    void updateCommandPopup(std::string_view query);
    void moveCommandPopupCursor(int delta);
    bool isCommandPopupActive() const;
    std::optional<std::string> commandPopupSelection() const;
    void startCommandHistorySearch(std::string_view seed);
    std::string cancelCommandHistorySearch();
    std::string acceptCommandHistorySearch();
    void updateCommandHistorySearchQuery(std::string_view query);
    void moveCommandHistorySearchCursor(int delta);
    bool isCommandHistorySearchActive() const;
    const std::string& commandHistorySearchQuery() const;
    void drawCommandPopup(std::string& output) const;
    void drawCommandHistoryPopup(std::string& output) const;
    void executeCommand(std::string_view cmd);
    std::vector<std::string> getCommandCompletions(std::string_view prefix);
    std::vector<std::string> getCommandCompletions(std::string_view prefix,
                                                   Mode mode);
    std::vector<std::string> getHelpCompletions(std::string_view prefix);
    std::vector<std::string> getSetCompletions(std::string_view prefix);
    std::vector<std::string> getPathCompletions(std::string_view path);
    std::vector<std::string> getPathCompletionsRecursive(std::string_view path);
    std::vector<std::string> getLocPathCompletions(std::string_view path);

private:
    Editor& editor;
};
