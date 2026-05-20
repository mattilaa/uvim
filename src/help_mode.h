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
    std::string previousFile;
    std::shared_ptr<CommandPrompt> commandPrompt;

    HelpMode() = default;

    explicit HelpMode(std::string helpTopic, std::string prevFile = {})
        : topic(std::move(helpTopic)), previousFile(std::move(prevFile))
    {
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, int key);

    void draw(Editor& editor) const;

private:
    void loadHelpContent(const std::string& helpTopic);
    std::optional<ModeState> executeCommand(ModeContext& ctx,
                                            std::string_view commandLine);
};
} // namespace editor::statemachine
