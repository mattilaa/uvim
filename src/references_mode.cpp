#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"

// ============================================================================
// ReferencesMode Implementation
// ============================================================================

namespace editor::statemachine
{
void ReferencesMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;

    // References should already be populated before entering this mode
    // via ed->findReferences() called from normal mode

    ed->needsFullRedraw = true;
}

void ReferencesMode::on_exit(ModeContext& ctx)
{
    Editor* ed = ctx.editor;

    // Restore block cursor
    Terminal::setCursorBlock();
}

std::optional<ModeState> ReferencesMode::handle(ModeContext& ctx,
                                                const ModeKeyEvent& event)
{
    const int key = event.key;
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    // ========================================================================
    // Exit
    // ========================================================================

    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(typed::TypedKey::KEY_Q))
    {
        if(c == keyCode(control::ControlKey::ESC))
            ed->noteDoubleEscStatusClear();
        ed->clearReferences();
        return NormalMode{};
    }

    // ========================================================================
    // Selection - Jump to reference
    // ========================================================================

    if(c == keyCode(control::ControlKey::ENTER) ||
       c == keyCode(typed::TypedKey::KEY_L) ||
       c == keyCode(navigation::NavigationKey::ARROW_RIGHT))
    {
        if(ed->selectReference())
        {
            return NormalMode{};
        }
        return std::nullopt;
    }

    // ========================================================================
    // Navigation through results
    // ========================================================================

    if(c == keyCode(typed::TypedKey::KEY_J) ||
       c == keyCode(control::ControlKey::CTRL_N) ||
       c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        ed->referencesDown();
    }
    else if(c == keyCode(typed::TypedKey::KEY_K) ||
            c == keyCode(control::ControlKey::CTRL_P) ||
            c == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        ed->referencesUp();
    }
    else if(c == keyCode(control::ControlKey::CTRL_D))
    {
        ed->referencesHalfPageDown();
    }
    else if(c == keyCode(control::ControlKey::CTRL_U))
    {
        ed->referencesHalfPageUp();
    }
    else if(c == keyCode(typed::TypedKey::KEY_G))
    {
        // gg - go to first
        int nextChar = Terminal::readKey();
        if(nextChar == keyCode(typed::TypedKey::KEY_G))
        {
            ed->referencesFirst();
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_CAP_G))
    {
        // G - go to last
        ed->referencesLast();
    }

    // ========================================================================
    // Preview Toggle
    // ========================================================================

    else if(c == keyCode(control::ControlKey::TAB) ||
            c == keyCode(typed::TypedKey::KEY_P))
    {
        ed->toggleReferencesPreview();
    }

    // ========================================================================
    // Open in split (like quickfix)
    // ========================================================================

    else if(c == keyCode(typed::TypedKey::KEY_O))
    {
        // Open reference but stay in references mode
        ed->openReferencePreview();
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}
} // namespace editor::statemachine
