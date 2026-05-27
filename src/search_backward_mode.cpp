#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include <algorithm>

// ============================================================================
// SearchBackwardMode Implementation
// ============================================================================

namespace editor::statemachine
{
namespace
{
std::string singleLinePasteText(std::string text)
{
    text.erase(std::remove_if(text.begin(), text.end(),
                              [](char ch) { return ch == '\n' || ch == '\r'; }),
               text.end());
    return text;
}

void previewBackwardSearch(Editor* ed, ModeContext& ctx)
{
    ctx.cursorX() = ed->savedCursorX;
    ctx.cursorY() = ed->savedCursorY;

    if(ed->searchQuery.empty())
    {
        ed->searchMatches.clear();
        ed->currentMatchIndex = -1;
        ed->needsFullRedraw = true;
        return;
    }

    ed->performSearch();
    ed->needsFullRedraw = true;
}
} // namespace

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
                                                    const ModeKeyEvent& event)
{
    const int key = event.key;
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    // ========================================================================
    // Cancel Search
    // ========================================================================

    if(c == keyCode(control::ControlKey::PASTE))
    {
        std::string text = singleLinePasteText(Terminal::takeLastPasteText());
        if(!text.empty())
        {
            ed->searchQuery += text;
            ctx.commandBuffer = "?" + ed->searchQuery;
            previewBackwardSearch(ed, ctx);
        }
        return std::nullopt;
    }

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
            previewBackwardSearch(ed, ctx);
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
            previewBackwardSearch(ed, ctx);
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
            previewBackwardSearch(ed, ctx);
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

        previewBackwardSearch(ed, ctx);
        return std::nullopt;
    }

    // ========================================================================
    // Regular Character Input
    // ========================================================================

    if(c >= 32 && c < 127)
    {
        ed->searchQuery += static_cast<char>(c);
        ctx.commandBuffer = "?" + ed->searchQuery;
        previewBackwardSearch(ed, ctx);
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
    previewBackwardSearch(ed, ctx);
}

} // namespace editor::statemachine

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
