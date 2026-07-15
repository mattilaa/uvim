#pragma once

#include "file_entry.h"
#include "mode.h"
#include "mode_commands.h"
#include "mode_context.h"
#include "mode_state.h"
#include "search_types.h"

#include <chrono>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class Editor;

namespace editor::statemachine
{
struct HelpMode
{
    static constexpr const char* name()
    {
        return "HELP";
    }

    std::string topic;
    std::vector<std::string> lines;
    int scrollOffset = 0;
    int selectedLine = 0;
    bool jumpHighlight = false;
    std::string previousFile;
    std::shared_ptr<CommandPrompt> commandPrompt;
    struct HelpSearchMatch
    {
        std::string topic;
        int line = 0;
        int score = 0;
        std::string content;
        std::vector<int> matchPositions;
        bool topicOnly = false;
    };

    bool searchActive = false;
    bool searchDocumentation = true;
    std::string searchQuery;
    std::vector<HelpSearchMatch> searchMatches;
    int searchCursor = 0;
    int searchOffset = 0;

    HelpMode() = default;

    explicit HelpMode(std::string helpTopic, std::string prevFile = {})
        : topic(std::move(helpTopic)), previousFile(std::move(prevFile))
    {
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx,
                                    const ModeKeyEvent& event);

    void draw(Editor& editor) const;

private:
    void loadHelpContent(const std::string& helpTopic);
    std::optional<std::string> topicForLine(int lineIndex) const;
    bool moveSelection(Editor& editor, int delta);
    bool acceptSelection(Editor& editor);
    void startSearch(Editor& editor);
    void updateSearchMatches(Editor& editor);
    void cancelSearch(Editor& editor);
    bool acceptSearch(Editor& editor);
    void searchMoveDown(Editor& editor);
    void searchMoveUp();
    void searchHalfPageDown(Editor& editor);
    void searchHalfPageUp(Editor& editor);
    void drawSearch(Editor& editor) const;
    std::optional<ModeState> executeCommand(ModeContext& ctx,
                                            std::string_view commandLine);
};
} // namespace editor::statemachine
