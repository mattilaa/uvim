#include "comment_leader_pending.h"

#include "comment_leader_pending_actions.h"
#include "key_enums.h"
#include "mode_state_machine.h"
#include "terminal.h"

namespace editor::statemachine
{
std::optional<ModeState>
CommentLeaderAfterCiiApplied::handle(CommentLeaderPendingMachine& machine,
                                     ModeContext& ctx,
                                     const ModeKeyEvent& event)
{
    const int key = event.key;
    if(commentleader::isEscape(key))
    {
        machine.cancel(ctx);
        return std::nullopt;
    }

    if(key == keyCode(typed::TypedKey::KEY_T))
    {
        if(machine.origin() == CommentLeaderOrigin::Normal)
        {
            std::optional<ModeState> result =
                commentleader::replaceNormalAppliedBlockWithTodoComment(
                    ctx, machine.startY(), machine.startX());
            machine.cancel(ctx);
            return result;
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
