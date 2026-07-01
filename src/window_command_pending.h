#pragma once

#include "mode_state.h"

#include <optional>
#include <variant>

namespace editor::statemachine
{
class WindowCommandPendingMachine;

struct WindowCommandAfterW
{
    std::optional<ModeState> handle(WindowCommandPendingMachine& machine,
                                    ModeContext& ctx,
                                    const ModeKeyEvent& event);
};

class WindowCommandPendingMachine
{
public:
    explicit WindowCommandPendingMachine(int count);

    std::optional<ModeState> handle(ModeContext& ctx,
                                    const ModeKeyEvent& event);

    bool done() const;
    int count() const;

    void finish();
    void cancel(ModeContext& ctx);

private:
    using PendingState = std::variant<WindowCommandAfterW>;

    PendingState state;
    int repeat = 1;
    bool finished = false;
};
} // namespace editor::statemachine
