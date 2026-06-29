#pragma once

#include "mode_state.h"

#include <optional>
#include <variant>

namespace editor::statemachine
{
class BufferCommandPendingMachine;

struct BufferCommandAfterB
{
    std::optional<ModeState> handle(BufferCommandPendingMachine& machine,
                                    ModeContext& ctx,
                                    const ModeKeyEvent& event);
};

class BufferCommandPendingMachine
{
public:
    explicit BufferCommandPendingMachine(int count);

    std::optional<ModeState> handle(ModeContext& ctx,
                                    const ModeKeyEvent& event);

    bool done() const;
    int count() const;

    void finish();
    void cancel(ModeContext& ctx);

private:
    using PendingState = std::variant<BufferCommandAfterB>;

    PendingState state;
    int repeat = 1;
    bool finished = false;
};
} // namespace editor::statemachine
