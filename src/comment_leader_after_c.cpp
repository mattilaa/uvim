#include "comment_leader_pending.h"

#ifdef UVIM_ENABLE_COLOR_TOOLS
#include "color_picker_mode.h"
#include "color_selector_mode.h"
#endif
#include "comment_leader_pending_actions.h"
#include "editor.h"
#include "key_enums.h"
#include "mode_state_machine.h"
#include "terminal.h"

namespace editor::statemachine
{
namespace
{
#ifdef UVIM_ENABLE_COLOR_TOOLS
std::optional<VisualColorRange> visualColorRangeFor(CommentLeaderOrigin origin,
                                                    ModeContext& ctx)
{
    if(origin == CommentLeaderOrigin::Normal || !ctx.editor)
        return std::nullopt;
    ctx.editor->rememberVisualColorRange();
    return ctx.editor->takeVisualColorRange();
}
#endif
} // namespace

std::optional<ModeState>
CommentLeaderAfterC::handle(CommentLeaderPendingMachine& machine,
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
        if(machine.origin() == CommentLeaderOrigin::Normal)
        {
            commentleader::applyLineComment(ctx, machine.origin());
            machine.setAfterCiApplied(ctx);
            return std::nullopt;
        }

        machine.setAfterCi(ctx);
        return std::nullopt;
    }

    if(key == keyCode(typed::TypedKey::KEY_P))
    {
#ifdef UVIM_ENABLE_COLOR_TOOLS
        const int nextKey = Terminal::readKeyTimeout(300);
        const bool background = nextKey == keyCode(typed::TypedKey::KEY_B);
        if(nextKey != -1 && !background)
            Terminal::unreadKey(nextKey);
        std::optional<VisualColorRange> range =
            visualColorRangeFor(machine.origin(), ctx);
        machine.cancel(ctx);
        if(range)
            return ColorPickerMode::forVisualRange(*range, background);
        return ColorPickerMode{background};
#else
        machine.cancel(ctx);
        Terminal::unreadKey(key);
        return std::nullopt;
#endif
    }

    if(key == keyCode(typed::TypedKey::KEY_S))
    {
#ifdef UVIM_ENABLE_COLOR_TOOLS
        const int nextKey = Terminal::readKeyTimeout(300);
        const bool background = nextKey == keyCode(typed::TypedKey::KEY_B);
        if(nextKey != -1 && !background)
            Terminal::unreadKey(nextKey);
        std::optional<VisualColorRange> range =
            visualColorRangeFor(machine.origin(), ctx);
        machine.cancel(ctx);
        if(range)
            return ColorSelectorMode::forVisualRange(*range, background);
        return ColorSelectorMode{background};
#else
        machine.cancel(ctx);
        Terminal::unreadKey(key);
        return std::nullopt;
#endif
    }

    machine.cancel(ctx);
    Terminal::unreadKey(key);
    return std::nullopt;
}
} // namespace editor::statemachine
