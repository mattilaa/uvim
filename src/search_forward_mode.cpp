#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include <algorithm>

// ============================================================================
// SearchForwardMode Implementation
// ============================================================================

namespace
{
std::string singleLinePasteText(std::string text)
{
    text.erase(std::remove_if(text.begin(), text.end(),
                              [](char ch)
                              { return ch == '\n' || ch == '\r'; }),
               text.end());
    return text;
}

void previewForwardSearch(Editor* ed, ModeContext& ctx)
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

std::optional<ModeState> SearchForwardMode::handle(ModeContext& ctx, int key)
{
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
            ctx.commandBuffer = "/" + ed->searchQuery;
            previewForwardSearch(ed, ctx);
        }
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::ESC))
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
            ctx.commandBuffer = "/" + ed->searchQuery;
            previewForwardSearch(ed, ctx);
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

    if(c == keyCode(navigation::NavigationKey::ARROW_UP) ||
       c == keyCode(control::ControlKey::CTRL_P))
    {
        std::string prevSearch = ed->getPreviousSearch();
        if(!prevSearch.empty())
        {
            ed->searchQuery = prevSearch;
            ctx.commandBuffer = "/" + ed->searchQuery;
            previewForwardSearch(ed, ctx);
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
            ctx.commandBuffer = "/" + ed->searchQuery;
            previewForwardSearch(ed, ctx);
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
        ctx.commandBuffer = "/";

        previewForwardSearch(ed, ctx);
        return std::nullopt;
    }

    // ========================================================================
    // Regular Character Input
    // ========================================================================

    if(c >= 32 && c < 127)
    {
        ed->searchQuery += static_cast<char>(c);
        ctx.commandBuffer = "/" + ed->searchQuery;
        previewForwardSearch(ed, ctx);
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}

void SearchForwardMode::deleteWordBackward(ModeContext& ctx)
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
    ctx.commandBuffer = "/" + ed->searchQuery;
    previewForwardSearch(ed, ctx);
}
