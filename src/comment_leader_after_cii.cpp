#include "comment_leader_pending.h"

#include "comment_leader_pending_actions.h"
#include "editor.h"
#include "key_enums.h"
#include "mode_state_machine.h"
#include "terminal.h"

namespace editor::statemachine
{
std::optional<ModeState>
CommentLeaderAfterCii::handle(CommentLeaderPendingMachine& machine,
                              ModeContext& ctx, int key)
{
    if(commentleader::isEscape(key))
    {
        machine.cancel(ctx);
        return std::nullopt;
    }

    std::optional<ModeState> result;
    if(key == keyCode(typed::TypedKey::KEY_T))
    {
        if(machine.origin() == CommentLeaderOrigin::Normal)
        {
            *ctx.editor->cursorY = machine.startY();
            *ctx.editor->cursorX = machine.startX();
        }
        result = commentleader::applyTodoBlockComment(ctx, machine.origin());
    }
    else
    {
        result = commentleader::applyBlockComment(ctx, machine.origin());
        if(!commentleader::isEnter(key))
            Terminal::unreadKey(key);
    }

    machine.cancel(ctx);
    return result;
}
} // namespace editor::statemachine
