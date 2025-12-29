#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"

// ============================================================================
// SearchForwardMode Implementation
// ============================================================================

void SearchForwardMode::on_enter(ModeContext& ctx)
{
    ctx.commandBuffer = "/";
    ctx.searchQuery.clear();
    ctx.savedCursorX = ctx.cursorX();
    ctx.savedCursorY = ctx.cursorY();
    ctx.searchForward = true;

    ctx.editor->needsFullRedraw = true;
}

void SearchForwardMode::on_exit(ModeContext& /* ctx */) {}

std::optional<ModeState> SearchForwardMode::handle(ModeContext& ctx,
                                                   const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    // Escape -> cancel search, restore cursor
    if(c == Terminal::ESC)
    {
        ctx.cursorX() = ctx.savedCursorX;
        ctx.cursorY() = ctx.savedCursorY;
        ctx.setStatusMessage("");
        return NormalMode{};
    }

    // Enter -> confirm search
    if(c == Terminal::ENTER)
    {
        if(!ctx.searchQuery.empty())
        {
            ed->addSearchToHistory(ctx.searchQuery);
            ed->performSearch(ctx.searchQuery, true);
        }
        return NormalMode{};
    }

    // Backspace
    if(c == Terminal::BACKSPACE || c == 127 || c == Terminal::CTRL_H)
    {
        if(!ctx.searchQuery.empty())
        {
            ctx.searchQuery.pop_back();
            ctx.commandBuffer = "/" + ctx.searchQuery;

            // Restore position and re-search incrementally
            ctx.cursorX() = ctx.savedCursorX;
            ctx.cursorY() = ctx.savedCursorY;
            if(!ctx.searchQuery.empty())
            {
                ed->performIncrementalSearch(ctx.searchQuery, true);
            }
        }
        else
        {
            // Backspace on empty search returns to normal
            ctx.cursorX() = ctx.savedCursorX;
            ctx.cursorY() = ctx.savedCursorY;
            return NormalMode{};
        }
        return std::nullopt;
    }

    // Ctrl+W - delete word
    if(c == Terminal::CTRL_W)
    {
        deleteWordBackward(ctx);
        return std::nullopt;
    }

    // Ctrl+U - clear search
    if(c == Terminal::CTRL_U)
    {
        ctx.searchQuery.clear();
        ctx.commandBuffer = "/";
        ctx.cursorX() = ctx.savedCursorX;
        ctx.cursorY() = ctx.savedCursorY;
        return std::nullopt;
    }

    // Arrow up - previous search in history
    if(c == Terminal::ARROW_UP)
    {
        std::string prev = ed->getPreviousSearch();
        if(!prev.empty())
        {
            ctx.searchQuery = prev;
            ctx.commandBuffer = "/" + ctx.searchQuery;
        }
        return std::nullopt;
    }

    // Arrow down - next search in history
    if(c == Terminal::ARROW_DOWN)
    {
        std::string next = ed->getNextSearch();
        ctx.searchQuery = next;
        ctx.commandBuffer = "/" + ctx.searchQuery;
        return std::nullopt;
    }

    // Ctrl+N / Ctrl+P - next/previous match during incremental search
    if(c == Terminal::CTRL_N)
    {
        ed->searchNext();
        return std::nullopt;
    }
    if(c == Terminal::CTRL_P)
    {
        ed->searchPrevious();
        return std::nullopt;
    }

    // Regular character input
    if(c >= 32 && c < 127)
    {
        ctx.searchQuery += static_cast<char>(c);
        ctx.commandBuffer = "/" + ctx.searchQuery;

        // Incremental search
        ed->performIncrementalSearch(ctx.searchQuery, true);
    }

    return std::nullopt;
}

void SearchForwardMode::deleteWordBackward(ModeContext& ctx)
{
    if(ctx.searchQuery.empty())
    {
        return;
    }

    size_t pos = ctx.searchQuery.length() - 1;

    // Skip trailing spaces
    while(pos > 0 && ctx.searchQuery[pos] == ' ')
    {
        pos--;
    }

    // Delete word characters
    while(pos > 0 && ctx.searchQuery[pos] != ' ')
    {
        pos--;
    }

    ctx.searchQuery = ctx.searchQuery.substr(0, pos > 0 ? pos + 1 : 0);
    ctx.commandBuffer = "/" + ctx.searchQuery;

    // Re-search incrementally
    ctx.cursorX() = ctx.savedCursorX;
    ctx.cursorY() = ctx.savedCursorY;
    if(!ctx.searchQuery.empty())
    {
        ctx.editor->performIncrementalSearch(ctx.searchQuery, true);
    }
}

// ============================================================================
// SearchBackwardMode Implementation
// ============================================================================

void SearchBackwardMode::on_enter(ModeContext& ctx)
{
    ctx.commandBuffer = "?";
    ctx.searchQuery.clear();
    ctx.savedCursorX = ctx.cursorX();
    ctx.savedCursorY = ctx.cursorY();
    ctx.searchForward = false;

    ctx.editor->needsFullRedraw = true;
}

void SearchBackwardMode::on_exit(ModeContext& /* ctx */) {}

std::optional<ModeState> SearchBackwardMode::handle(ModeContext& ctx,
                                                    const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    // Escape -> cancel search, restore cursor
    if(c == Terminal::ESC)
    {
        ctx.cursorX() = ctx.savedCursorX;
        ctx.cursorY() = ctx.savedCursorY;
        ctx.setStatusMessage("");
        return NormalMode{};
    }

    // Enter -> confirm search
    if(c == Terminal::ENTER)
    {
        if(!ctx.searchQuery.empty())
        {
            ed->addSearchToHistory(ctx.searchQuery);
            ed->performSearch(ctx.searchQuery, false);
        }
        return NormalMode{};
    }

    // Backspace
    if(c == Terminal::BACKSPACE || c == 127 || c == Terminal::CTRL_H)
    {
        if(!ctx.searchQuery.empty())
        {
            ctx.searchQuery.pop_back();
            ctx.commandBuffer = "?" + ctx.searchQuery;

            ctx.cursorX() = ctx.savedCursorX;
            ctx.cursorY() = ctx.savedCursorY;
            if(!ctx.searchQuery.empty())
            {
                ed->performIncrementalSearch(ctx.searchQuery, false);
            }
        }
        else
        {
            ctx.cursorX() = ctx.savedCursorX;
            ctx.cursorY() = ctx.savedCursorY;
            return NormalMode{};
        }
        return std::nullopt;
    }

    // Ctrl+W - delete word
    if(c == Terminal::CTRL_W)
    {
        deleteWordBackward(ctx);
        return std::nullopt;
    }

    // Ctrl+U - clear search
    if(c == Terminal::CTRL_U)
    {
        ctx.searchQuery.clear();
        ctx.commandBuffer = "?";
        ctx.cursorX() = ctx.savedCursorX;
        ctx.cursorY() = ctx.savedCursorY;
        return std::nullopt;
    }

    // Arrow up/down - search history
    if(c == Terminal::ARROW_UP)
    {
        std::string prev = ed->getPreviousSearch();
        if(!prev.empty())
        {
            ctx.searchQuery = prev;
            ctx.commandBuffer = "?" + ctx.searchQuery;
        }
        return std::nullopt;
    }
    if(c == Terminal::ARROW_DOWN)
    {
        std::string next = ed->getNextSearch();
        ctx.searchQuery = next;
        ctx.commandBuffer = "?" + ctx.searchQuery;
        return std::nullopt;
    }

    // Ctrl+N / Ctrl+P - next/previous match
    if(c == Terminal::CTRL_N)
    {
        ed->searchNext();
        return std::nullopt;
    }
    if(c == Terminal::CTRL_P)
    {
        ed->searchPrevious();
        return std::nullopt;
    }

    // Regular character input
    if(c >= 32 && c < 127)
    {
        ctx.searchQuery += static_cast<char>(c);
        ctx.commandBuffer = "?" + ctx.searchQuery;

        // Incremental search
        ed->performIncrementalSearch(ctx.searchQuery, false);
    }

    return std::nullopt;
}

void SearchBackwardMode::deleteWordBackward(ModeContext& ctx)
{
    if(ctx.searchQuery.empty())
    {
        return;
    }

    size_t pos = ctx.searchQuery.length() - 1;

    while(pos > 0 && ctx.searchQuery[pos] == ' ')
    {
        pos--;
    }

    while(pos > 0 && ctx.searchQuery[pos] != ' ')
    {
        pos--;
    }

    ctx.searchQuery = ctx.searchQuery.substr(0, pos > 0 ? pos + 1 : 0);
    ctx.commandBuffer = "?" + ctx.searchQuery;

    ctx.cursorX() = ctx.savedCursorX;
    ctx.cursorY() = ctx.savedCursorY;
    if(!ctx.searchQuery.empty())
    {
        ctx.editor->performIncrementalSearch(ctx.searchQuery, false);
    }
}
