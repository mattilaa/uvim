#pragma once

#include "comment_leader_pending.h"
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
struct NormalMode
{
    static constexpr const char* name()
    {
        return "NORMAL";
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx,
                                    const ModeKeyEvent& event);

private:
    std::optional<CommentLeaderPendingMachine> commentLeaderPending;

    // Helper methods for complex key sequences
    std::optional<ModeState> handleLeaderKey(ModeContext& ctx, int c);
    std::optional<ModeState> handleGCommand(ModeContext& ctx, int c);
    std::optional<ModeState> handleZCommand(ModeContext& ctx, int c);
};
} // namespace editor::statemachine
