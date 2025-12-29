#include "editor_lsp_query.h"
#include "mode_state_machine.h"
#include "terminal.h"

// ============================================================================
// BufferBrowserMode Implementation
// ============================================================================

void BufferBrowserMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    ed->initializeBufferBrowser();
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

    if(c == Terminal::ESC || c == 'q')
    {
        return NormalMode{};
    }

    // ========================================================================
    // Selection
    // ========================================================================

    if(c == Terminal::ENTER || c == 'l' || c == Terminal::ARROW_RIGHT)
    {
        if(ed->selectBufferBrowserEntry())
        {
            return NormalMode{};
        }
        return std::nullopt;
    }

    // ========================================================================
    // Navigation
    // ========================================================================

    if(c == 'j' || c == Terminal::ARROW_DOWN || c == Terminal::CTRL_N)
    {
        ed->bufferBrowserDown();
    }
    else if(c == 'k' || c == Terminal::ARROW_UP || c == Terminal::CTRL_P)
    {
        ed->bufferBrowserUp();
    }
    else if(c == 'G')
    {
        ed->bufferBrowserEnd();
    }
    else if(c == 'g')
    {
        int nextChar = Terminal::readKey();
        if(nextChar == 'g')
        {
            ed->bufferBrowserStart();
        }
    }
    else if(c == Terminal::CTRL_D)
    {
        // Half page down
        for(int i = 0; i < ed->screenRows / 2; i++)
        {
            ed->bufferBrowserDown();
        }
    }
    else if(c == Terminal::CTRL_U)
    {
        // Half page up
        for(int i = 0; i < ed->screenRows / 2; i++)
        {
            ed->bufferBrowserUp();
        }
    }

    // ========================================================================
    // Buffer Operations
    // ========================================================================

    else if(c == 'd' || c == 'D')
    {
        ed->deleteSelectedBuffer();
    }

    // Quick jump by number (1-9)
    else if(c >= '1' && c <= '9')
    {
        int bufNum = c - '1';
        if(ed->switchToBufferByNumber(bufNum))
        {
            return NormalMode{};
        }
    }

    // ========================================================================
    // Mode Switching
    // ========================================================================

    // Switch to fuzzy find
    else if(c == Terminal::CTRL_P || c == 'f')
    {
        return FuzzyFindMode{};
    }

    // Switch to grep search
    else if(c == Terminal::CTRL_S || c == '/')
    {
        return GrepSearchMode{};
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}
