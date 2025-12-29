#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"

// ============================================================================
// GrepSearchMode Implementation
// ============================================================================

void GrepSearchMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    ed->initializeGrepSearch();
    ed->needsFullRedraw = true;

    // Set cursor to bar for input
    Terminal::setCursorBarBlinking();
}

void GrepSearchMode::on_exit(ModeContext& ctx)
{
    // Restore block cursor
    Terminal::setCursorBlock();
}

std::optional<ModeState> GrepSearchMode::handle(ModeContext& ctx,
                                                const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    // ========================================================================
    // Exit
    // ========================================================================

    if(c == Terminal::ESC)
    {
        return NormalMode{};
    }

    // ========================================================================
    // Selection
    // ========================================================================

    if(c == Terminal::ENTER)
    {
        if(ed->selectGrepResult())
        {
            return NormalMode{};
        }
        return std::nullopt;
    }

    // ========================================================================
    // Navigation through results
    // ========================================================================

    if(c == Terminal::CTRL_N || c == Terminal::ARROW_DOWN)
    {
        ed->grepResultDown();
    }
    else if(c == Terminal::CTRL_P || c == Terminal::ARROW_UP)
    {
        ed->grepResultUp();
    }
    else if(c == Terminal::CTRL_D)
    {
        ed->grepResultHalfPageDown();
    }
    else if(c == Terminal::CTRL_U)
    {
        ed->grepResultHalfPageUp();
    }

    // ========================================================================
    // Input Editing
    // ========================================================================

    else if(c == Terminal::BACKSPACE || c == 127 || c == Terminal::CTRL_H)
    {
        ed->grepSearchBackspace();
    }
    else if(c == Terminal::CTRL_W)
    {
        // Delete word backward
        ed->grepSearchDeleteWord();
    }

    // ========================================================================
    // Preview Toggle
    // ========================================================================

    else if(c == Terminal::TAB)
    {
        ed->toggleGrepPreview();
    }

    // ========================================================================
    // Mode Switching
    // ========================================================================

    // Switch to fuzzy find
    else if(c == Terminal::CTRL_P)
    {
        return FuzzyFindMode{};
    }

    // Switch to buffer browser
    else if(c == Terminal::CTRL_B)
    {
        return BufferBrowserMode{};
    }

    // ========================================================================
    // Character Input
    // ========================================================================

    else if(c >= 32 && c < 127)
    {
        ed->grepSearchAddChar(static_cast<char>(c));
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}
