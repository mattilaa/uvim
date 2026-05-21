#include "comment_leader_pending.h"

#include "comment_leader_pending_actions.h"
#include "editor.h"
#include "key_enums.h"
#include "mode_state_machine.h"
#include "terminal.h"

#include <algorithm>

namespace editor::statemachine
{
std::optional<ModeState>
CommentLeaderAfterCiiApplied::handle(CommentLeaderPendingMachine& machine,
                                     ModeContext& ctx, int key)
{
    if(commentleader::isEscape(key))
    {
        machine.cancel(ctx);
        return std::nullopt;
    }

    if(key == keyCode(typed::TypedKey::KEY_T))
    {
        if(machine.origin() == CommentLeaderOrigin::Normal)
        {
            const int innerRow = std::min(
                machine.startY() + 1,
                static_cast<int>(ctx.editor->currentBuffer->lines.size()) - 1);
            *ctx.editor->cursorY = std::max(0, innerRow);
            *ctx.editor->cursorX = machine.startX();
        }
        commentleader::toggleNormalBlockComment(ctx);
        if(machine.origin() == CommentLeaderOrigin::Normal)
        {
            *ctx.editor->cursorY = machine.startY();
            *ctx.editor->cursorX = machine.startX();
        }
        std::optional<ModeState> result =
            commentleader::applyTodoBlockComment(ctx, machine.origin());
        machine.cancel(ctx);
        return result;
    }

    machine.cancel(ctx);
    if(!commentleader::isEnter(key))
        Terminal::unreadKey(key);
    return std::nullopt;
}
} // namespace editor::statemachine
