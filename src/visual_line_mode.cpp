#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include <algorithm>
#include <utility>

namespace
{
void replaceVisualLineSelection(Editor* ed, std::string pasteBuffer)
{
    if(!ed || !ed->lines || !ed->currentBuffer || pasteBuffer.empty())
        return;

    const int visualStartY = ed->currentBuffer->visualStartY;
    const int visualStartX = ed->currentBuffer->visualStartX;
    const int startY =
        std::min(ed->currentBuffer->visualStartY,
                 ed->currentBuffer->visualEndY);
    const int endY =
        std::max(ed->currentBuffer->visualStartY,
                 ed->currentBuffer->visualEndY);

    *ed->cursorY = std::clamp(visualStartY, 0, (int)ed->lines->size() - 1);
    *ed->cursorX =
        std::clamp(visualStartX, 0, (int)(*ed->lines)[*ed->cursorY].size());
    ed->saveState();

    for(int y = endY; y >= startY; --y)
    {
        if(y >= 0 && y < (int)ed->lines->size())
        {
            ed->lines->erase(ed->lines->begin() + y);
            if(ed->currentBuffer->blameValid &&
               y < (int)ed->currentBuffer->blameEntries.size())
            {
                ed->currentBuffer->blameEntries.erase(
                    ed->currentBuffer->blameEntries.begin() + y);
            }
        }
    }

    if(ed->lines->empty())
        ed->lines->push_back("");

    *ed->cursorY = std::min(startY, (int)ed->lines->size() - 1);
    *ed->cursorX = 0;
    *ed->dirty = true;
    if(ed->currentBuffer->blameValid)
    {
        ed->currentBuffer->blameStart = 0;
        ed->currentBuffer->blameEnd =
            (int)ed->currentBuffer->blameEntries.size() - 1;
    }

    ed->yankBuffer = std::move(pasteBuffer);
    const bool useSystemClipboard = ed->useSystemClipboard;
    ed->useSystemClipboard = false;
    ed->pasteBefore();
    ed->useSystemClipboard = useSystemClipboard;
}
} // namespace

// ============================================================================
// VisualLineMode Implementation
// ============================================================================

void VisualLineMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;

    // Initialize visual line selection
    ed->currentBuffer->visualStartX = ctx.cursorX();
    ed->currentBuffer->visualStartY = ctx.cursorY();
    ed->currentBuffer->visualEndX = ctx.cursorX();
    ed->currentBuffer->visualEndY = ctx.cursorY();

    ed->needsFullRedraw = true;
}

void VisualLineMode::on_exit(ModeContext& ctx)
{
    ctx.editor->needsFullRedraw = true;
}

std::optional<ModeState> VisualLineMode::handle(ModeContext& ctx, int key)
{
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    if(c == keyCode(control::ControlKey::PASTE))
    {
        std::string text = Terminal::takeLastPasteText();
        if(text.empty())
            return std::nullopt;
        replaceVisualLineSelection(ed, std::move(text));
        return NormalMode{};
    }

    // ========================================================================
    // Count Prefix Accumulation
    // ========================================================================

    if(ctx.repeatCount == 0 && c >= keyCode(typed::TypedKey::KEY_1) &&
       c <= keyCode(typed::TypedKey::KEY_9))
    {
        ctx.repeatCount = c - keyCode(typed::TypedKey::KEY_0);
        return std::nullopt;
    }
    if(ctx.repeatCount > 0 && c >= keyCode(typed::TypedKey::KEY_0) &&
       c <= keyCode(typed::TypedKey::KEY_9))
    {
        ctx.repeatCount =
            ctx.repeatCount * 10 + (c - keyCode(typed::TypedKey::KEY_0));
        return std::nullopt;
    }
    int count = std::max(1, ctx.repeatCount);

    // ========================================================================
    // Leader Key (Space)
    // ========================================================================

    if(ctx.commandBuffer == " ")
    {
        if(c == keyCode(typed::TypedKey::KEY_F))
        {
            ctx.commandBuffer.clear();
            ctx.repeatCount = 0;
            ed->formatBuffer();
            return NormalMode{};
        }
        if(c == keyCode(typed::TypedKey::KEY_P))
        {
            ctx.commandBuffer.clear();
            ctx.repeatCount = 0;
            std::string clipboard = ed->getSystemClipboard();
            if(clipboard.empty())
            {
                ctx.setStatusMessage("System clipboard is empty");
                return std::nullopt;
            }
            ed->deleteLineSelection();
            ed->yankBuffer = clipboard;
            ed->pasteBefore();
            return NormalMode{};
        }
        if(c == keyCode(control::ControlKey::SPACE))
        {
            ctx.commandBuffer.clear();
            ctx.setStatusMessage("");
            ctx.repeatCount = 0;
            return std::nullopt;
        }

        ctx.commandBuffer.clear();
        ctx.setStatusMessage("");
        ctx.repeatCount = 0;
    }

    if(c == keyCode(control::ControlKey::SPACE))
    {
        ctx.commandBuffer = " ";
        ctx.setStatusMessage("Leader");
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Exit / Switch Modes
    // ========================================================================

    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(typed::TypedKey::KEY_CAP_V))
    {
        if(c == keyCode(control::ControlKey::ESC))
            ed->noteDoubleEscStatusClear();
        return NormalMode{};
    }

    if(c == keyCode(typed::TypedKey::KEY_V))
    {
        return VisualMode{};
    }

    if(c == keyCode(control::ControlKey::CTRL_V))
    {
        return VisualBlockMode{};
    }

    // ========================================================================
    // Movement
    // ========================================================================

    bool didMove = false;
    switch(c)
    {
    case keyCode(typed::TypedKey::KEY_J):
    case keyCode(navigation::NavigationKey::ARROW_DOWN):
        ed->moveDown(count);
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_K):
    case keyCode(navigation::NavigationKey::ARROW_UP):
        ed->moveUp(count);
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_CAP_G):
        ed->moveToLastLine();
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_G):
    {
        int nextChar = Terminal::readKey();
        if(nextChar == keyCode(typed::TypedKey::KEY_G))
        {
            ed->moveToFirstLine();
            didMove = true;
        }
    }
    break;
    case keyCode(command::CommandKey::KEY_LEFT_BRACE):
        for(int i = 0; i < count; i++)
            ed->moveParagraphBackward();
        didMove = true;
        break;
    case keyCode(command::CommandKey::KEY_RIGHT_BRACE):
        for(int i = 0; i < count; i++)
            ed->moveParagraphForward();
        didMove = true;
        break;
    case keyCode(control::ControlKey::CTRL_D):
        ed->scrollHalfPageDown(false);
        didMove = true;
        break;
    case keyCode(control::ControlKey::CTRL_U):
        ed->scrollHalfPageUp(false);
        didMove = true;
        break;
    case keyCode(command::CommandKey::KEY_PERCENT):
        ed->moveToMatchingBracket();
        didMove = true;
        break;

        // ========================================================================
        // Line Operations
        // ========================================================================

    case keyCode(typed::TypedKey::KEY_D):
    case keyCode(typed::TypedKey::KEY_X):
        ed->yankLineSelection();
        ed->deleteLineSelection();
        ed->saveState();
        return NormalMode{};

    case keyCode(typed::TypedKey::KEY_Y):
        ed->yankLineSelection();
        return NormalMode{};

    case keyCode(typed::TypedKey::KEY_P):
    {
        std::string pasteBuffer = ed->yankBuffer;
        if(ed->useSystemClipboard)
        {
            std::string clipboard = ed->getSystemClipboard();
            if(!clipboard.empty())
                pasteBuffer = clipboard;
        }
        if(pasteBuffer.empty())
            return std::nullopt;
        replaceVisualLineSelection(ed, std::move(pasteBuffer));
        return NormalMode{};
    }

    case keyCode(typed::TypedKey::KEY_C):
    {
        int nextChar = Terminal::readKeyTimeout(300);
        if(nextChar == keyCode(typed::TypedKey::KEY_I))
        {
            int startY = std::min(ed->currentBuffer->visualStartY,
                                  ed->currentBuffer->visualEndY);
            int endY = std::max(ed->currentBuffer->visualStartY,
                                ed->currentBuffer->visualEndY);
            ed->commentLines(startY, endY);
            return NormalMode{};
        }
        ed->yankLineSelection();
        ed->deleteLineSelection();
        ed->saveState();
        return InsertMode{};
    }

    case keyCode(command::CommandKey::KEY_GREATER):
        ed->indentLineSelection();
        ed->saveState();
        return NormalMode{};

    case keyCode(command::CommandKey::KEY_LESS):
        ed->dedentLineSelection();
        ed->saveState();
        return NormalMode{};

    case keyCode(command::CommandKey::KEY_EQUAL):
        ed->autoIndentLineSelection();
        ed->saveState();
        return NormalMode{};

    case keyCode(typed::TypedKey::KEY_CAP_J):
    {
        int startLine = std::min(ed->currentBuffer->visualStartY,
                                 ed->currentBuffer->visualEndY);
        int endLine = std::max(ed->currentBuffer->visualStartY,
                               ed->currentBuffer->visualEndY);
        ctx.cursorY() = startLine;
        for(int i = startLine;
            i < endLine && ctx.cursorY() < (int)ctx.lines().size() - 1; i++)
        {
            ed->joinLines();
        }
        ed->saveState();
        return NormalMode{};
    }

    // Swap selection ends
    case keyCode(typed::TypedKey::KEY_O):
        std::swap(ctx.cursorY(), ed->currentBuffer->visualStartY);
        didMove = true;
        break;
    }

    if(didMove)
        ctx.repeatCount = 0;

    // Update visual end
    ed->currentBuffer->visualEndY = ctx.cursorY();
    ed->needsFullRedraw = true;

    return std::nullopt;
}
