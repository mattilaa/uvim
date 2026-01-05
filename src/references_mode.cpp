#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"

// ============================================================================
// ReferencesMode Implementation
// ============================================================================

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
                                                const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    // ========================================================================
    // Exit
    // ========================================================================

    if(c == Terminal::ESC || c == 'q')
    {
        ed->clearReferences();
        return NormalMode{};
    }

    // ========================================================================
    // Selection - Jump to reference
    // ========================================================================

    if(c == Terminal::ENTER || c == 'l' || c == Terminal::ARROW_RIGHT)
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

    if(c == 'j' || c == Terminal::CTRL_N || c == Terminal::ARROW_DOWN)
    {
        ed->referencesDown();
    }
    else if(c == 'k' || c == Terminal::CTRL_P || c == Terminal::ARROW_UP)
    {
        ed->referencesUp();
    }
    else if(c == Terminal::CTRL_D)
    {
        ed->referencesHalfPageDown();
    }
    else if(c == Terminal::CTRL_U)
    {
        ed->referencesHalfPageUp();
    }
    else if(c == 'g')
    {
        // gg - go to first
        int nextChar = Terminal::readKey();
        if(nextChar == 'g')
        {
            ed->referencesFirst();
        }
    }
    else if(c == 'G')
    {
        // G - go to last
        ed->referencesLast();
    }

    // ========================================================================
    // Preview Toggle
    // ========================================================================

    else if(c == Terminal::TAB || c == 'p')
    {
        ed->toggleReferencesPreview();
    }

    // ========================================================================
    // Open in split (like quickfix)
    // ========================================================================

    else if(c == 'o')
    {
        // Open reference but stay in references mode
        ed->openReferencePreview();
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}
