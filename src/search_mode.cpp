#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"

// ============================================================================
// SearchForwardMode Implementation
// ============================================================================

void SearchForwardMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;

    // Save cursor position for restoration on cancel
    ed->savedCursorX = ctx.cursorX();
    ed->savedCursorY = ctx.cursorY();

    // Initialize search
    ed->searchQuery.clear();
    ed->searchForward = true;
    ctx.commandBuffer = "/";

    ed->needsFullRedraw = true;

    // Set cursor to bar for search input
    Terminal::setCursorBarBlinking();
}

void SearchForwardMode::on_exit(ModeContext& /* ctx */)
{
    // Restore block cursor
    Terminal::setCursorBlock();
}

std::optional<ModeState> SearchForwardMode::handle(ModeContext& ctx,
                                                   const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    // ========================================================================
    // Cancel Search
    // ========================================================================

    if(c == Terminal::ESC)
    {
        // Restore cursor position
        ctx.cursorX() = ed->savedCursorX;
        ctx.cursorY() = ed->savedCursorY;
        ed->searchQuery.clear();
        ctx.commandBuffer.clear();
        return NormalMode{};
    }

    // ========================================================================
    // Execute Search
    // ========================================================================

    if(c == Terminal::ENTER)
    {
        if(!ed->searchQuery.empty())
        {
            ed->addSearchToHistory(ed->searchQuery);
            ed->performSearch();
        }
        ctx.commandBuffer.clear();
        return NormalMode{};
    }

    // ========================================================================
    // Backspace
    // ========================================================================

    if(c == Terminal::BACKSPACE || c == 127 || c == Terminal::CTRL_H)
    {
        if(!ed->searchQuery.empty())
        {
            ed->searchQuery.pop_back();
            ctx.commandBuffer = "/" + ed->searchQuery;

            // Restore original position and re-search
            ctx.cursorX() = ed->savedCursorX;
            ctx.cursorY() = ed->savedCursorY;

            if(!ed->searchQuery.empty())
            {
                ed->findAllMatches();
                if(!ed->searchMatches.empty())
                {
                    ed->jumpToMatch(0);
                }
            }
        }
        else
        {
            // Empty query, cancel search
            ctx.cursorX() = ed->savedCursorX;
            ctx.cursorY() = ed->savedCursorY;
            ctx.commandBuffer.clear();
            return NormalMode{};
        }
        return std::nullopt;
    }

    // ========================================================================
    // History Navigation
    // ========================================================================

    if(c == Terminal::ARROW_UP || c == Terminal::CTRL_P)
    {
        std::string prevSearch = ed->getPreviousSearch();
        if(!prevSearch.empty())
        {
            ed->searchQuery = prevSearch;
            ctx.commandBuffer = "/" + ed->searchQuery;
            ed->findAllMatches();
            if(!ed->searchMatches.empty())
            {
                ed->jumpToMatch(0);
            }
        }
        return std::nullopt;
    }

    if(c == Terminal::ARROW_DOWN || c == Terminal::CTRL_N)
    {
        std::string nextSearch = ed->getNextSearch();
        if(!nextSearch.empty())
        {
            ed->searchQuery = nextSearch;
            ctx.commandBuffer = "/" + ed->searchQuery;
            ed->findAllMatches();
            if(!ed->searchMatches.empty())
            {
                ed->jumpToMatch(0);
            }
        }
        return std::nullopt;
    }

    // ========================================================================
    // Ctrl+W - Delete Word Backward
    // ========================================================================

    if(c == Terminal::CTRL_W)
    {
        deleteWordBackward(ctx);
        return std::nullopt;
    }

    // ========================================================================
    // Ctrl+U - Clear Search
    // ========================================================================

    if(c == Terminal::CTRL_U)
    {
        ed->searchQuery.clear();
        ctx.commandBuffer = "/";

        ctx.cursorX() = ed->savedCursorX;
        ctx.cursorY() = ed->savedCursorY;
        return std::nullopt;
    }

    // ========================================================================
    // Regular Character Input
    // ========================================================================

    if(c >= 32 && c < 127)
    {
        ed->searchQuery += static_cast<char>(c);
        ctx.commandBuffer = "/" + ed->searchQuery;

        // Incremental search
        ed->findAllMatches();
        if(!ed->searchMatches.empty())
        {
            ed->jumpToMatch(0);
        }
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}

void SearchForwardMode::deleteWordBackward(ModeContext& ctx)
{
    Editor* ed = ctx.editor;

    while(!ed->searchQuery.empty() && ed->searchQuery.back() == ' ')
    {
        ed->searchQuery.pop_back();
    }
    while(!ed->searchQuery.empty() && ed->searchQuery.back() != ' ')
    {
        ed->searchQuery.pop_back();
    }
    ctx.commandBuffer = "/" + ed->searchQuery;

    ctx.cursorX() = ed->savedCursorX;
    ctx.cursorY() = ed->savedCursorY;

    if(!ed->searchQuery.empty())
    {
        ed->findAllMatches();
        if(!ed->searchMatches.empty())
        {
            ed->jumpToMatch(0);
        }
    }
}

// ============================================================================
// SearchBackwardMode Implementation
// ============================================================================

void SearchBackwardMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;

    // Save cursor position for restoration on cancel
    ed->savedCursorX = ctx.cursorX();
    ed->savedCursorY = ctx.cursorY();

    // Initialize search
    ed->searchQuery.clear();
    ed->searchForward = false;
    ctx.commandBuffer = "?";

    ed->needsFullRedraw = true;

    // Set cursor to bar for search input
    Terminal::setCursorBarBlinking();
}

void SearchBackwardMode::on_exit(ModeContext& /* ctx */)
{
    // Restore block cursor
    Terminal::setCursorBlock();
}

std::optional<ModeState> SearchBackwardMode::handle(ModeContext& ctx,
                                                    const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    // ========================================================================
    // Cancel Search
    // ========================================================================

    if(c == Terminal::ESC)
    {
        ctx.cursorX() = ed->savedCursorX;
        ctx.cursorY() = ed->savedCursorY;
        ed->searchQuery.clear();
        ctx.commandBuffer.clear();
        return NormalMode{};
    }

    // ========================================================================
    // Execute Search
    // ========================================================================

    if(c == Terminal::ENTER)
    {
        if(!ed->searchQuery.empty())
        {
            ed->addSearchToHistory(ed->searchQuery);
            ed->performSearch();
        }
        ctx.commandBuffer.clear();
        return NormalMode{};
    }

    // ========================================================================
    // Backspace
    // ========================================================================

    if(c == Terminal::BACKSPACE || c == 127 || c == Terminal::CTRL_H)
    {
        if(!ed->searchQuery.empty())
        {
            ed->searchQuery.pop_back();
            ctx.commandBuffer = "?" + ed->searchQuery;

            ctx.cursorX() = ed->savedCursorX;
            ctx.cursorY() = ed->savedCursorY;

            if(!ed->searchQuery.empty())
            {
                ed->findAllMatches();
                if(!ed->searchMatches.empty())
                {
                    // For backward search, find last match before cursor
                    int bestIndex = ed->searchMatches.size() - 1;
                    for(int i = ed->searchMatches.size() - 1; i >= 0; i--)
                    {
                        const auto& match = ed->searchMatches[i];
                        if(match.row < ed->savedCursorY ||
                           (match.row == ed->savedCursorY &&
                            match.col < ed->savedCursorX))
                        {
                            bestIndex = i;
                            break;
                        }
                    }
                    ed->jumpToMatch(bestIndex);
                }
            }
        }
        else
        {
            ctx.cursorX() = ed->savedCursorX;
            ctx.cursorY() = ed->savedCursorY;
            ctx.commandBuffer.clear();
            return NormalMode{};
        }
        return std::nullopt;
    }

    // ========================================================================
    // History Navigation
    // ========================================================================

    if(c == Terminal::ARROW_UP || c == Terminal::CTRL_P)
    {
        std::string prevSearch = ed->getPreviousSearch();
        if(!prevSearch.empty())
        {
            ed->searchQuery = prevSearch;
            ctx.commandBuffer = "?" + ed->searchQuery;
            ed->findAllMatches();
            if(!ed->searchMatches.empty())
            {
                ed->jumpToMatch(ed->searchMatches.size() - 1);
            }
        }
        return std::nullopt;
    }

    if(c == Terminal::ARROW_DOWN || c == Terminal::CTRL_N)
    {
        std::string nextSearch = ed->getNextSearch();
        if(!nextSearch.empty())
        {
            ed->searchQuery = nextSearch;
            ctx.commandBuffer = "?" + ed->searchQuery;
            ed->findAllMatches();
            if(!ed->searchMatches.empty())
            {
                ed->jumpToMatch(ed->searchMatches.size() - 1);
            }
        }
        return std::nullopt;
    }

    // ========================================================================
    // Ctrl+W - Delete Word Backward
    // ========================================================================

    if(c == Terminal::CTRL_W)
    {
        deleteWordBackward(ctx);
        return std::nullopt;
    }

    // ========================================================================
    // Ctrl+U - Clear Search
    // ========================================================================

    if(c == Terminal::CTRL_U)
    {
        ed->searchQuery.clear();
        ctx.commandBuffer = "?";

        ctx.cursorX() = ed->savedCursorX;
        ctx.cursorY() = ed->savedCursorY;
        return std::nullopt;
    }

    // ========================================================================
    // Regular Character Input
    // ========================================================================

    if(c >= 32 && c < 127)
    {
        ed->searchQuery += static_cast<char>(c);
        ctx.commandBuffer = "?" + ed->searchQuery;

        // Incremental search (backward)
        ed->findAllMatches();
        if(!ed->searchMatches.empty())
        {
            // Find last match before saved cursor position
            int bestIndex = ed->searchMatches.size() - 1;
            for(int i = ed->searchMatches.size() - 1; i >= 0; i--)
            {
                const auto& match = ed->searchMatches[i];
                if(match.row < ed->savedCursorY ||
                   (match.row == ed->savedCursorY &&
                    match.col < ed->savedCursorX))
                {
                    bestIndex = i;
                    break;
                }
            }
            ed->jumpToMatch(bestIndex);
        }
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}

void SearchBackwardMode::deleteWordBackward(ModeContext& ctx)
{
    Editor* ed = ctx.editor;

    while(!ed->searchQuery.empty() && ed->searchQuery.back() == ' ')
    {
        ed->searchQuery.pop_back();
    }
    while(!ed->searchQuery.empty() && ed->searchQuery.back() != ' ')
    {
        ed->searchQuery.pop_back();
    }
    ctx.commandBuffer = "?" + ed->searchQuery;

    ctx.cursorX() = ed->savedCursorX;
    ctx.cursorY() = ed->savedCursorY;

    if(!ed->searchQuery.empty())
    {
        ed->findAllMatches();
        if(!ed->searchMatches.empty())
        {
            ed->jumpToMatch(ed->searchMatches.size() - 1);
        }
    }
}
