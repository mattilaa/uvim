#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"

// ============================================================================
// FuzzyFindMode Implementation
// ============================================================================

void FuzzyFindMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    ed->initializeFuzzyFind();
    ed->needsFullRedraw = true;

    // Set cursor to bar for input
    Terminal::setCursorBarBlinking();
}

void FuzzyFindMode::on_exit(ModeContext& ctx)
{
    // Restore block cursor
    Terminal::setCursorBlock();
}

std::optional<ModeState> FuzzyFindMode::handle(ModeContext& ctx,
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
        if(ed->selectFuzzyFindEntry())
        {
            return NormalMode{};
        }
        return std::nullopt;
    }

    // ========================================================================
    // Navigation
    // ========================================================================

    if(c == Terminal::CTRL_N || c == Terminal::CTRL_J ||
       c == Terminal::ARROW_DOWN)
    {
        ed->fuzzyFindDown();
    }
    else if(c == Terminal::CTRL_P || c == Terminal::CTRL_K ||
            c == Terminal::ARROW_UP)
    {
        ed->fuzzyFindUp();
    }
    else if(c == Terminal::CTRL_D)
    {
        ed->fuzzyFindHalfPageDown();
    }
    else if(c == Terminal::CTRL_U)
    {
        ed->fuzzyFindHalfPageUp();
    }

    // ========================================================================
    // Input Editing
    // ========================================================================

    else if(c == Terminal::BACKSPACE || c == 127 || c == Terminal::CTRL_H)
    {
        ed->fuzzyFindBackspace();
    }
    else if(c == Terminal::CTRL_W)
    {
        // Delete word backward
        ed->fuzzyFindDeleteWord();
    }
    else if(c == Terminal::CTRL_U)
    {
        // Clear the entire query - note: this conflicts with half page up
        // In practice, Ctrl+U in fuzzy find clears the query
        ed->fuzzyFindClear();
    }

    // ========================================================================
    // Preview Toggle
    // ========================================================================

    else if(c == Terminal::TAB)
    {
        ed->toggleFuzzyPreview();
    }

    // ========================================================================
    // Mode Switching
    // ========================================================================

    // Switch to buffer browser
    else if(c == Terminal::CTRL_B)
    {
        return BufferBrowserMode{};
    }

    // Switch to grep search
    else if(c == Terminal::CTRL_S)
    {
        return GrepSearchMode{};
    }

    // ========================================================================
    // Character Input
    // ========================================================================

    else if(c >= 32 && c < 127)
    {
        ed->fuzzyFindAddChar(static_cast<char>(c));
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}
