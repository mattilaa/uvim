#include "comment_leader_pending.h"

#include "comment_leader_pending_actions.h"
#include "key_enums.h"
#include "mode_state_machine.h"
#include "terminal.h"

namespace editor::statemachine
{
std::optional<ModeState> CommentLeaderAfterCiiAwaitingSuffix::handle(
    CommentLeaderPendingMachine& machine, ModeContext& ctx, int key)
{
    if(commentleader::isEscape(key))
    {
        machine.cancel(ctx);
        return std::nullopt;
    }

    if(key == keyCode(typed::TypedKey::KEY_T))
    {
        return CommentLeaderAfterCiiApplied{}.handle(machine, ctx, key);
    }

    machine.cancel(ctx);
    if(!commentleader::isEnter(key))
        Terminal::unreadKey(key);
    return std::nullopt;
}
} // namespace editor::statemachine
