#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"

// ============================================================================
// LspInfoMode Implementation
// ============================================================================

void LspInfoMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    ed->needsFullRedraw = true;
}

void LspInfoMode::on_exit(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    ed->clearLspInfo();
    Terminal::setCursorBlock();
}

std::optional<ModeState> LspInfoMode::handle(ModeContext& ctx,
                                             int key)
{
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    if(c == keyCode(control::ControlKey::ESC) || c == keyCode(typed::TypedKey::KEY_Q))
    {
        if(c == keyCode(control::ControlKey::ESC))
            ed->noteDoubleEscStatusClear();
        return ctx.hasBuffer() ? ModeState{NormalMode{}}
                               : ModeState{WelcomeMode{}};
    }

    if(c == keyCode(typed::TypedKey::KEY_R))
    {
        ed->showLspInfo();
        return std::nullopt;
    }

    return std::nullopt;
}
