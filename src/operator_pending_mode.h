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
struct OperatorPendingMode
{
    static constexpr const char* name()
    {
        return "OP_PENDING";
    }

    char op = 0;
    int count = 1;
    bool awaitingObject = false;
    char objectType = 0;
    char pendingCharMotion = 0;

    explicit OperatorPendingMode(char pendingOp = 0, int cnt = 1)
        : op(pendingOp), count(cnt)
    {
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx,
                                    const ModeKeyEvent& event);
};
} // namespace editor::statemachine
