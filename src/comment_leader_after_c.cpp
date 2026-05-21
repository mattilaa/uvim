#include "comment_leader_pending.h"

#include "comment_leader_pending_actions.h"
#include "key_enums.h"
#include "mode_state_machine.h"
#include "terminal.h"

namespace editor::statemachine
{
std::optional<ModeState>
CommentLeaderAfterC::handle(CommentLeaderPendingMachine& machine,
                            ModeContext& ctx, int key)
{
    if(commentleader::isEscape(key))
    {
        machine.cancel(ctx);
        return std::nullopt;
    }

    if(key == keyCode(typed::TypedKey::KEY_I))
    {
        machine.rememberCursor(ctx);
        if(machine.origin() == CommentLeaderOrigin::Normal)
        {
            commentleader::applyLineComment(ctx, machine.origin());
            machine.setAfterCiApplied(ctx);
            return std::nullopt;
        }

        machine.setAfterCi(ctx);
        return std::nullopt;
    }

    machine.cancel(ctx);
    Terminal::unreadKey(key);
    return std::nullopt;
}
} // namespace editor::statemachine
