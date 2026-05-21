#include "comment_leader_pending.h"
#include "editor.h"
#include "insert_mode.h"
#include "key_enums.h"
#include "mode_context.h"
#include "mode_state_machine.h"
#include "normal_mode.h"
#include "terminal.h"

#include <algorithm>

namespace editor::statemachine
{
namespace
{
bool isEscape(int key)
{
    return key == keyCode(control::ControlKey::ESC);
}

bool isEnter(int key)
{
    return key == keyCode(control::ControlKey::ENTER);
}

void cancelPending(ModeContext& ctx, CommentLeaderPendingMachine& machine)
{
    ctx.commandBuffer.clear();
    ctx.setStatusMessage("");
    ctx.repeatCount = 0;
    machine.finish();
}

std::optional<ModeState> applyLineComment(ModeContext& ctx,
                                          CommentLeaderOrigin origin)
{
    Editor* ed = ctx.editor;
    if(origin == CommentLeaderOrigin::Normal)
    {
        ed->commentLines(*ed->cursorY, *ed->cursorY);
        return std::nullopt;
    }

    const int startY = std::min(ed->currentBuffer->visualStartY,
                                ed->currentBuffer->visualEndY);
    const int endY = std::max(ed->currentBuffer->visualStartY,
                              ed->currentBuffer->visualEndY);
    ed->commentLines(startY, endY);
    return NormalMode{};
}

void toggleNormalLineComment(ModeContext& ctx)
{
    ctx.editor->commentLines(*ctx.editor->cursorY, *ctx.editor->cursorY);
}

std::optional<ModeState> applyBlockComment(ModeContext& ctx,
                                           CommentLeaderOrigin origin)
{
    Editor* ed = ctx.editor;
    if(origin == CommentLeaderOrigin::Normal)
    {
        ed->commentBlock(*ed->cursorY, *ed->cursorY);
        return std::nullopt;
    }

    if(origin == CommentLeaderOrigin::Visual)
    {
        int startY = 0;
        int startX = 0;
        int endY = 0;
        int endX = 0;
        ed->getSelectionBounds(startY, startX, endY, endX);
        ed->commentBlockRange(startY, startX, endY, endX);
        return NormalMode{};
    }

    const int startY = std::min(ed->currentBuffer->visualStartY,
                                ed->currentBuffer->visualEndY);
    const int endY = std::max(ed->currentBuffer->visualStartY,
                              ed->currentBuffer->visualEndY);
    ed->commentBlock(startY, endY);
    return NormalMode{};
}

void toggleNormalBlockComment(ModeContext& ctx)
{
    ctx.editor->commentBlock(*ctx.editor->cursorY, *ctx.editor->cursorY);
}

std::optional<ModeState> applyTodoLineComment(ModeContext& ctx,
                                              CommentLeaderOrigin origin)
{
    if(origin != CommentLeaderOrigin::Normal)
        return applyLineComment(ctx, origin);

    if(ctx.editor->insertTodoLineComment(*ctx.editor->cursorY))
        return InsertMode{};
    return std::nullopt;
}

std::optional<ModeState> applyTodoBlockComment(ModeContext& ctx,
                                               CommentLeaderOrigin origin)
{
    if(origin != CommentLeaderOrigin::Normal)
        return applyBlockComment(ctx, origin);

    if(ctx.editor->insertTodoBlockComment(*ctx.editor->cursorY))
        return InsertMode{};
    return std::nullopt;
}
} // namespace

CommentLeaderPendingMachine::CommentLeaderPendingMachine(
    CommentLeaderOrigin origin)
    : state(CommentLeaderAfterC{}), source(origin)
{
}

std::optional<ModeState> CommentLeaderPendingMachine::handle(ModeContext& ctx,
                                                             int key)
{
    return std::visit([&](auto& pending) -> std::optional<ModeState>
                      { return pending.handle(*this, ctx, key); }, state);
}

bool CommentLeaderPendingMachine::done() const
{
    return finished;
}

CommentLeaderOrigin CommentLeaderPendingMachine::origin() const
{
    return source;
}

void CommentLeaderPendingMachine::finish()
{
    finished = true;
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

std::optional<ModeState>
CommentLeaderAfterC::handle(CommentLeaderPendingMachine& machine,
                            ModeContext& ctx, int key)
{
    if(isEscape(key))
    {
        cancelPending(ctx, machine);
        return std::nullopt;
    }

    if(key == keyCode(typed::TypedKey::KEY_I))
    {
        if(machine.origin() == CommentLeaderOrigin::Normal)
        {
            std::optional<ModeState> result =
                applyLineComment(ctx, machine.origin());
            machine.setAfterCiApplied(ctx);
            return std::nullopt;
        }

        machine.setAfterCi(ctx);
        return std::nullopt;
    }

    cancelPending(ctx, machine);
    Terminal::unreadKey(key);
    return std::nullopt;
}

std::optional<ModeState>
CommentLeaderAfterCi::handle(CommentLeaderPendingMachine& machine,
                             ModeContext& ctx, int key)
{
    if(isEscape(key))
    {
        cancelPending(ctx, machine);
        return std::nullopt;
    }

    if(key == keyCode(typed::TypedKey::KEY_I))
    {
        std::optional<ModeState> result =
            applyBlockComment(ctx, machine.origin());
        cancelPending(ctx, machine);
        return result;
    }

    std::optional<ModeState> result;
    if(key == keyCode(typed::TypedKey::KEY_T))
    {
        result = applyTodoLineComment(ctx, machine.origin());
    }
    else
    {
        result = applyLineComment(ctx, machine.origin());
        if(!isEnter(key))
            Terminal::unreadKey(key);
    }

    cancelPending(ctx, machine);
    return result;
}

std::optional<ModeState>
CommentLeaderAfterCiApplied::handle(CommentLeaderPendingMachine& machine,
                                    ModeContext& ctx, int key)
{
    if(isEscape(key))
    {
        cancelPending(ctx, machine);
        return std::nullopt;
    }

    if(key == keyCode(typed::TypedKey::KEY_I))
    {
        toggleNormalLineComment(ctx);
        toggleNormalBlockComment(ctx);
        machine.setAfterCiiApplied(ctx);
        return std::nullopt;
    }

    if(key == keyCode(typed::TypedKey::KEY_T))
    {
        toggleNormalLineComment(ctx);
        std::optional<ModeState> result =
            applyTodoLineComment(ctx, machine.origin());
        cancelPending(ctx, machine);
        return result;
    }

    cancelPending(ctx, machine);
    if(!isEnter(key))
        Terminal::unreadKey(key);
    return std::nullopt;
}

std::optional<ModeState>
CommentLeaderAfterCii::handle(CommentLeaderPendingMachine& machine,
                              ModeContext& ctx, int key)
{
    if(isEscape(key))
    {
        cancelPending(ctx, machine);
        return std::nullopt;
    }

    std::optional<ModeState> result;
    if(key == keyCode(typed::TypedKey::KEY_T))
    {
        result = applyTodoBlockComment(ctx, machine.origin());
    }
    else
    {
        result = applyBlockComment(ctx, machine.origin());
        if(!isEnter(key))
            Terminal::unreadKey(key);
    }

    cancelPending(ctx, machine);
    return result;
}

std::optional<ModeState>
CommentLeaderAfterCiiApplied::handle(CommentLeaderPendingMachine& machine,
                                     ModeContext& ctx, int key)
{
    if(isEscape(key))
    {
        cancelPending(ctx, machine);
        return std::nullopt;
    }

    if(key == keyCode(typed::TypedKey::KEY_T))
    {
        toggleNormalBlockComment(ctx);
        std::optional<ModeState> result =
            applyTodoBlockComment(ctx, machine.origin());
        cancelPending(ctx, machine);
        return result;
    }

    cancelPending(ctx, machine);
    if(!isEnter(key))
        Terminal::unreadKey(key);
    return std::nullopt;
}
} // namespace editor::statemachine
