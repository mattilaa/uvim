#include "window_command_pending.h"

#include "editor.h"
#include "mode_context.h"
#include "mode_state_machine.h"
#include "terminal.h"

namespace editor::statemachine
{
WindowCommandPendingMachine::WindowCommandPendingMachine(int count)
    : state(WindowCommandAfterW{}), repeat(count > 0 ? count : 1)
{
}

std::optional<ModeState>
WindowCommandPendingMachine::handle(ModeContext& ctx,
                                    const ModeKeyEvent& event)
{
    return std::visit([&](auto& pending) -> std::optional<ModeState>
                      { return pending.handle(*this, ctx, event); }, state);
}

bool WindowCommandPendingMachine::done() const
{
    return finished;
}

int WindowCommandPendingMachine::count() const
{
    return repeat;
}

void WindowCommandPendingMachine::finish()
{
    finished = true;
}

void WindowCommandPendingMachine::cancel(ModeContext& ctx)
{
    ctx.commandBuffer.clear();
    ctx.setStatusMessage("");
    ctx.repeatCount = 0;
    finish();
}

std::optional<ModeState>
WindowCommandAfterW::handle(WindowCommandPendingMachine& machine,
                            ModeContext& ctx, const ModeKeyEvent& event)
{
    const int c = keyCode(event.key);

    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(control::ControlKey::CTRL_C))
    {
        machine.cancel(ctx);
        return std::nullopt;
    }

    if(c == keyCode(typed::TypedKey::KEY_C) && ctx.editor->splitActive)
    {
        ctx.editor->closeSplit();
        ctx.commandBuffer.clear();
        ctx.setStatusMessage("");
        ctx.repeatCount = 0;
        machine.finish();
        return std::nullopt;
    }

    Terminal::unreadKey(c);

    for(int i = 0; i < machine.count(); ++i)
        ctx.editor->moveWordForward();
    ctx.commandBuffer.clear();
    ctx.setStatusMessage("");
    ctx.repeatCount = 0;
    machine.finish();
    return std::nullopt;
}
} // namespace editor::statemachine
