#include "comment_leader_pending.h"
#include "editor.h"
#include "mode_context.h"
#include "mode_state_machine.h"

namespace editor::statemachine
{
CommentLeaderPendingMachine::CommentLeaderPendingMachine(
    CommentLeaderOrigin origin)
    : state(CommentLeaderAfterC{}), source(origin)
{
}

std::optional<ModeState>
CommentLeaderPendingMachine::handle(ModeContext& ctx, const ModeKeyEvent& event)
{
    return std::visit([&](auto& pending) -> std::optional<ModeState>
                      { return pending.handle(*this, ctx, event); }, state);
}

bool CommentLeaderPendingMachine::done() const
{
    return finished;
}

CommentLeaderOrigin CommentLeaderPendingMachine::origin() const
{
    return source;
}

int CommentLeaderPendingMachine::startY() const
{
    return originalY;
}

int CommentLeaderPendingMachine::startX() const
{
    return originalX;
}

void CommentLeaderPendingMachine::finish()
{
    finished = true;
}

void CommentLeaderPendingMachine::cancel(ModeContext& ctx)
{
    ctx.commandBuffer.clear();
    ctx.setStatusMessage("");
    ctx.repeatCount = 0;
    finish();
}

void CommentLeaderPendingMachine::rememberCursor(ModeContext& ctx)
{
    if(ctx.editor && ctx.editor->cursorY)
        originalY = *ctx.editor->cursorY;
    if(ctx.editor && ctx.editor->cursorX)
        originalX = *ctx.editor->cursorX;
}

void CommentLeaderPendingMachine::setAfterCi(ModeContext& ctx)
{
    state = CommentLeaderAfterCi{};
    ctx.commandBuffer = " ci";
    ctx.setStatusMessage("Leader-ci");
}

void CommentLeaderPendingMachine::setAfterCiApplied(ModeContext& ctx)
{
    state = CommentLeaderAfterCiApplied{};
    ctx.commandBuffer = " ci";
    ctx.setStatusMessage("Leader-ci");
}

void CommentLeaderPendingMachine::setAfterCii(ModeContext& ctx)
{
    state = CommentLeaderAfterCii{};
    ctx.commandBuffer = " cii";
    ctx.setStatusMessage("Leader-cii");
}

void CommentLeaderPendingMachine::setAfterCiiApplied(ModeContext& ctx)
{
    state = CommentLeaderAfterCiiApplied{};
    ctx.commandBuffer = " cii";
    ctx.setStatusMessage("Leader-cii");
}

void CommentLeaderPendingMachine::setAfterCiiAwaitingSuffix(ModeContext& ctx)
{
    state = CommentLeaderAfterCiiAwaitingSuffix{};
    ctx.commandBuffer = " cii";
    ctx.setStatusMessage("Leader-cii");
}
} // namespace editor::statemachine
