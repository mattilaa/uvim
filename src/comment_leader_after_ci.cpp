#include "comment_leader_pending.h"

#include "comment_leader_pending_actions.h"
#include "key_enums.h"
#include "mode_state_machine.h"
#include "terminal.h"

namespace editor::statemachine
{
std::optional<ModeState>
CommentLeaderAfterCi::handle(CommentLeaderPendingMachine& machine,
                             ModeContext& ctx, const ModeKeyEvent& event)
{
    const int key = event.key;
    if(commentleader::isEscape(key))
    {
        machine.cancel(ctx);
        return std::nullopt;
    }

    if(key == keyCode(typed::TypedKey::KEY_I))
    {
        machine.rememberCursor(ctx);
        if(machine.origin() == CommentLeaderOrigin::VisualLine)
        {
            machine.setAfterCii(ctx);
            return std::nullopt;
        }

        std::optional<ModeState> result =
            commentleader::applyBlockComment(ctx, machine.origin());
        machine.cancel(ctx);
        return result;
    }

    std::optional<ModeState> result;
    if(key == keyCode(typed::TypedKey::KEY_T))
    {
        result = commentleader::applyTodoLineComment(ctx, machine.origin());
    }
    else
    {
        result = commentleader::applyLineComment(ctx, machine.origin());
        if(!commentleader::isEnter(key))
            Terminal::unreadKey(key);
    }

    machine.cancel(ctx);
    return result;
}
} // namespace editor::statemachine
