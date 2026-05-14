#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"

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

std::optional<ModeState> SearchBackwardMode::handle(ModeContext& ctx, int key)
{
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    // ========================================================================
    // Cancel Search
    // ========================================================================

    if(c == keyCode(control::ControlKey::ESC))
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

    if(c == keyCode(control::ControlKey::ENTER))
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

    if(c == keyCode(control::ControlKey::BACKSPACE) || c == 127 ||
       c == keyCode(control::ControlKey::CTRL_H))
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

    if(c == keyCode(navigation::NavigationKey::ARROW_UP) ||
       c == keyCode(control::ControlKey::CTRL_P))
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

    if(c == keyCode(navigation::NavigationKey::ARROW_DOWN) ||
       c == keyCode(control::ControlKey::CTRL_N))
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

    if(c == keyCode(control::ControlKey::CTRL_W))
    {
        deleteWordBackward(ctx);
        return std::nullopt;
    }

    // ========================================================================
    // Ctrl+U - Clear Search
    // ========================================================================

    if(c == keyCode(control::ControlKey::CTRL_U))
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

    while(!ed->searchQuery.empty() &&
          ed->searchQuery.back() == keyCode(control::ControlKey::SPACE))
    {
        ed->searchQuery.pop_back();
    }
    while(!ed->searchQuery.empty() &&
          ed->searchQuery.back() != keyCode(control::ControlKey::SPACE))
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

void Editor::performSearch(const std::string& query, bool forward)
{
    searchQuery = query;
    searchForward = forward;
    performSearch();
}

void Editor::performIncrementalSearch(const std::string& query, bool forward)
{
    searchQuery = query;
    searchForward = forward;
    findAllMatches();

    if(!searchMatches.empty())
    {
        // Find closest match
        int bestIndex = 0;
        for(int i = 0; i < (int)searchMatches.size(); i++)
        {
            const auto& match = searchMatches[i];
            if(forward)
            {
                if(match.row > savedCursorY ||
                   (match.row == savedCursorY && match.col >= savedCursorX))
                {
                    bestIndex = i;
                    break;
                }
            }
            else
            {
                if(match.row < savedCursorY ||
                   (match.row == savedCursorY && match.col <= savedCursorX))
                {
                    bestIndex = i;
                }
            }
        }
        jumpToMatch(bestIndex);
    }
    needsFullRedraw = true;
}

void Editor::searchWordUnderCursor(bool forward)
{
    std::string word = getSymbolUnderCursor();
    if(!word.empty())
    {
        searchQuery = word;
        searchForward = forward;
        savedCursorX = *cursorX;
        savedCursorY = *cursorY;
        performSearch();
    }
}

void Editor::addSearchToHistory(const std::string& query)
{
    if(query.empty())
        return;

    auto it = std::find(searchHistory.begin(), searchHistory.end(), query);
    if(it != searchHistory.end())
    {
        searchHistory.erase(it);
    }

    searchHistory.push_back(query);
    searchHistoryIndex = -1;

    while(searchHistory.size() > 100)
    {
        searchHistory.erase(searchHistory.begin());
    }
}

std::string Editor::getPreviousSearch()
{
    if(searchHistory.empty())
        return "";

    if(searchHistoryIndex < 0)
    {
        searchHistoryIndex = searchHistory.size() - 1;
    }
    else if(searchHistoryIndex > 0)
    {
        searchHistoryIndex--;
    }
    return searchHistory[searchHistoryIndex];
}

std::string Editor::getNextSearch()
{
    if(searchHistory.empty() || searchHistoryIndex < 0)
        return "";

    if(searchHistoryIndex < (int)searchHistory.size() - 1)
    {
        searchHistoryIndex++;
        return searchHistory[searchHistoryIndex];
    }
    return "";
}
