#pragma once

#include "mode.h"
#include "mode_commands.h"
#include "mode_context.h"
#include "mode_state.h"
#include "task_queue.h"

#include <optional>

namespace editor::statemachine
{
// `:tasks` — live list of queued/running background jobs. Waiting jobs can
// be reordered (K/J) or cancelled (x/dd) while older ones still run.
struct TaskListMode
{
    static constexpr const char* name()
    {
        return "TASKS";
    }

    int cursor = 0;
    // Follows the selected job when rows above it retire or move.
    TaskQueue::TaskId selected = 0;
    bool pendingDelete = false;

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(
        ModeContext& ctx,
        const ModeKeyEvent& event);
};
} // namespace editor::statemachine
