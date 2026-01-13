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
                                             const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    if(c == Terminal::ESC || c == 'q')
    {
        return ctx.hasBuffer() ? ModeState{NormalMode{}}
                               : ModeState{WelcomeMode{}};
    }

    if(c == 'r')
    {
        ed->showLspInfo();
        return std::nullopt;
    }

    return std::nullopt;
}
