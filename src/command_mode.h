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

struct CommandMode
{
    static constexpr const char* name()
    {
        return "COMMAND";
    }

    // Tab completion state
    std::vector<std::string> completions;
    int completionIndex = -1;
    std::string originalInput;
    bool locCompletion = false;
    std::string locCommand;

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, int key);

private:
    // Helper methods
    void handleTabCompletion(ModeContext& ctx);
    void handleReverseTabCompletion(ModeContext& ctx);
    void deleteWordBackward(ModeContext& ctx);
};
