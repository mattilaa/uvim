#include "tool_info_mode.h"
#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"

namespace editor::statemachine
{
void ToolInfoMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    ed->needsFullRedraw = true;
}

void ToolInfoMode::on_exit(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    ed->clearToolInfo();
    Terminal::setCursorBlock();
}

std::optional<ModeState> ToolInfoMode::handle(ModeContext& ctx,
                                              const ModeKeyEvent& event)
{
    const int key = event.key;
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(typed::TypedKey::KEY_Q))
    {
        if(c == keyCode(control::ControlKey::ESC))
            ed->noteDoubleEscStatusClear();
        return ctx.hasBuffer() ? ModeState{NormalMode{}}
                               : ModeState{WelcomeMode{}};
    }

    if(c == keyCode(typed::TypedKey::KEY_R))
    {
        ed->showToolInfo();
        ed->needsFullRedraw = true;
        return std::nullopt;
    }

    if(c == keyCode(typed::TypedKey::KEY_J))
    {
        ed->scrollToolInfo(1);
        return std::nullopt;
    }

    if(c == keyCode(typed::TypedKey::KEY_K))
    {
        ed->scrollToolInfo(-1);
        return std::nullopt;
    }

    return std::nullopt;
}
} // namespace editor::statemachine
