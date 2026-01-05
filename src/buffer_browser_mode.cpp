#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"

// ============================================================================
// BufferBrowserMode Implementation
// ============================================================================

void BufferBrowserMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    ed->bufferBrowser.initialize(*ed);
    ed->needsFullRedraw = true;
}

void BufferBrowserMode::on_exit(ModeContext& /* ctx */)
{
    // Nothing specific to do on exit
}

std::optional<ModeState> BufferBrowserMode::handle(ModeContext& ctx,
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
        if(ed->bufferBrowser.selectEntry(*ed))
        {
            return NormalMode{};
        }
        return std::nullopt;
    }

    // ========================================================================
    // Navigation
    // ========================================================================

    if(c == Terminal::CTRL_J || c == Terminal::ARROW_DOWN ||
       c == Terminal::CTRL_N)
    {
        ed->bufferBrowser.down(ed->screenRows);
    }
    else if(c == Terminal::CTRL_K || c == Terminal::ARROW_UP)
    {
        ed->bufferBrowser.up(ed->screenRows);
    }
    else if(c == Terminal::CTRL_D)
    {
        // Half page down
        ed->bufferBrowser.halfPageDown(ed->screenRows);
    }
    else if(c == Terminal::PAGE_UP)
    {
        ed->bufferBrowser.halfPageUp(ed->screenRows);
    }

    // ========================================================================
    // Input Editing
    // ========================================================================

    else if(c == Terminal::BACKSPACE || c == Terminal::DEL ||
            c == Terminal::CTRL_H)
    {
        ed->bufferBrowser.backspace(*ed);
    }
    else if(c == Terminal::CTRL_U)
    {
        ed->bufferBrowser.clear(*ed);
    }

    // ========================================================================
    // Mode Switching
    // ========================================================================

    // Switch to fuzzy find
    else if(c == Terminal::CTRL_P)
    {
        return FuzzyFindMode{};
    }

    // Switch to grep search
    else if(c == Terminal::CTRL_S || c == '/')
    {
        return GrepSearchMode{};
    }

    // ========================================================================
    // Character Input
    // ========================================================================

    else if(c >= 32 && c < 127)
    {
        ed->bufferBrowser.addChar(*ed, static_cast<char>(c));
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}
