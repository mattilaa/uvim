#include "buffer_command_pending.h"

#include "editor.h"
#include "mode_context.h"
#include "mode_state_machine.h"

namespace editor::statemachine
{
BufferCommandPendingMachine::BufferCommandPendingMachine(int count)
    : state(BufferCommandAfterB{}), repeat(count > 0 ? count : 1)
{
}

std::optional<ModeState>
BufferCommandPendingMachine::handle(ModeContext& ctx,
                                    const ModeKeyEvent& event)
{
    return std::visit([&](auto& pending) -> std::optional<ModeState>
                      { return pending.handle(*this, ctx, event); }, state);
}

bool BufferCommandPendingMachine::done() const
{
    return finished;
}

int BufferCommandPendingMachine::count() const
{
    return repeat;
}

void BufferCommandPendingMachine::finish()
{
    finished = true;
}

void BufferCommandPendingMachine::cancel(ModeContext& ctx)
{
    ctx.commandBuffer.clear();
    ctx.setStatusMessage("");
    ctx.repeatCount = 0;
    finish();
}

std::optional<ModeState>
BufferCommandAfterB::handle(BufferCommandPendingMachine& machine,
                            ModeContext& ctx, const ModeKeyEvent& event)
{
    const int c = keyCode(event.key);

    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(control::ControlKey::CTRL_C))
    {
        machine.cancel(ctx);
        return std::nullopt;
    }

    if(c == keyCode(typed::TypedKey::KEY_D))
    {
        ctx.commandBuffer.clear();
        ctx.setStatusMessage("");
        ctx.editor->closeCurrentBuffer();
        ctx.repeatCount = 0;
        machine.finish();
        return std::nullopt;
    }

    for(int i = 0; i < machine.count(); ++i)
        ctx.editor->moveWordBackward();
    ctx.commandBuffer.clear();
    ctx.setStatusMessage("");
    ctx.repeatCount = 0;
    machine.finish();
    return std::nullopt;
}
} // namespace editor::statemachine
