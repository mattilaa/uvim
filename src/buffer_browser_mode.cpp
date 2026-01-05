#include "editor.h"
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

    if(c == Terminal::ESC)
    {
        return NormalMode{};
    }

    // ========================================================================
    // Selection
    // ========================================================================

    if(c == Terminal::ENTER)
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

    if(c == Terminal::CTRL_J || c == Terminal::ARROW_DOWN ||
       c == Terminal::CTRL_N)
    {
        ed->bufferBrowserDown();
    }
    else if(c == Terminal::CTRL_K || c == Terminal::ARROW_UP)
    {
        ed->bufferBrowserUp();
    }
    else if(c == Terminal::CTRL_D)
    {
        // Half page down
        for(int i = 0; i < ed->screenRows / 2; i++)
        {
            ed->bufferBrowserDown();
        }
    }
    else if(c == Terminal::PAGE_UP)
    {
        // Half page up
        for(int i = 0; i < ed->screenRows / 2; i++)
        {
            ed->bufferBrowserUp();
        }
    }

    // ========================================================================
    // Input Editing
    // ========================================================================

    else if(c == Terminal::BACKSPACE || c == Terminal::DEL ||
            c == Terminal::CTRL_H)
    {
        if(!ed->bufferQuery.empty())
        {
            ed->bufferQuery.pop_back();
            ed->updateBufferMatches();
            ed->bufferCursor = 0;
            ed->bufferOffset = 0;
        }
    }
    else if(c == Terminal::CTRL_U)
    {
        ed->bufferQuery.clear();
        ed->updateBufferMatches();
        ed->bufferCursor = 0;
        ed->bufferOffset = 0;
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
        ed->bufferQuery += static_cast<char>(c);
        ed->updateBufferMatches();
        ed->bufferCursor = 0;
        ed->bufferOffset = 0;
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}
