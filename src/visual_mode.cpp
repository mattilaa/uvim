#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include <algorithm>

// ============================================================================
// VisualMode Implementation
// ============================================================================

namespace editor::statemachine
{
void VisualMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    commentLeaderPending.reset();
    textObjectPending.reset();

    // Initialize visual selection start
    ed->currentBuffer->visualStartX = ctx.cursorX();
    ed->currentBuffer->visualStartY = ctx.cursorY();
    ed->currentBuffer->visualEndX = ctx.cursorX();
    ed->currentBuffer->visualEndY = ctx.cursorY();

    ed->needsFullRedraw = true;
}

void VisualMode::on_exit(ModeContext& ctx)
{
    commentLeaderPending.reset();
    textObjectPending.reset();
    ctx.editor->needsFullRedraw = true;
}

std::optional<ModeState> VisualMode::handle(ModeContext& ctx, int key)
{
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    if(ed->handleRenamePopupKey(c))
        return std::nullopt;

    if(commentLeaderPending)
    {
        std::optional<ModeState> result = commentLeaderPending->handle(ctx, c);
        if(commentLeaderPending->done())
            commentLeaderPending.reset();
        return result;
    }

    if(textObjectPending)
    {
        std::optional<ModeState> result = textObjectPending->handle(ctx, c);
        if(textObjectPending->done())
            textObjectPending.reset();
        return result;
    }

    if(c == keyCode(control::ControlKey::PASTE))
    {
        std::string text = Terminal::takeLastPasteText();
        if(text.empty())
            return std::nullopt;
        const bool useSystemClipboard = ed->useSystemClipboard;
        ed->useSystemClipboard = false;
        ed->deleteSelection();
        ed->yankBuffer = text;
        ed->pasteBefore();
        ed->useSystemClipboard = useSystemClipboard;
        return NormalMode{};
    }

    if(ed->diagnosticPopupActive)
    {
        if(c == keyCode(typed::TypedKey::KEY_Q))
        {
            ed->closeDiagnosticPopup();
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::CTRL_J) ||
           c == keyCode(navigation::NavigationKey::ARROW_DOWN))
        {
            if(!ed->diagnosticPopupFixes.empty())
            {
                ed->diagnosticPopupFixIndex =
                    std::min(ed->diagnosticPopupFixIndex + 1,
                             (int)ed->diagnosticPopupFixes.size() - 1);
                int window = std::min(6, (int)ed->diagnosticPopupFixes.size());
                if(ed->diagnosticPopupFixIndex < ed->diagnosticPopupFixScroll)
                    ed->diagnosticPopupFixScroll = ed->diagnosticPopupFixIndex;
                else if(ed->diagnosticPopupFixIndex >=
                        ed->diagnosticPopupFixScroll + window)
                    ed->diagnosticPopupFixScroll =
                        ed->diagnosticPopupFixIndex - window + 1;
                ed->needsFullRedraw = true;
            }
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::CTRL_K) ||
           c == keyCode(navigation::NavigationKey::ARROW_UP))
        {
            if(!ed->diagnosticPopupFixes.empty())
            {
                ed->diagnosticPopupFixIndex =
                    std::max(ed->diagnosticPopupFixIndex - 1, 0);
                int window = std::min(6, (int)ed->diagnosticPopupFixes.size());
                if(ed->diagnosticPopupFixIndex < ed->diagnosticPopupFixScroll)
                    ed->diagnosticPopupFixScroll = ed->diagnosticPopupFixIndex;
                else if(ed->diagnosticPopupFixIndex >=
                        ed->diagnosticPopupFixScroll + window)
                    ed->diagnosticPopupFixScroll =
                        ed->diagnosticPopupFixIndex - window + 1;
                ed->needsFullRedraw = true;
            }
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::ENTER))
        {
            ed->applyDiagnosticFix(ed->diagnosticPopupFixIndex);
            ctx.repeatCount = 0;
            return std::nullopt;
        }
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

    if(c == keyCode(typed::TypedKey::KEY_R))
    {
        int next = Terminal::readKeyTimeout(250);
        if(next == keyCode(typed::TypedKey::KEY_N))
        {
            ed->openRenamePopupForCursor();
            ctx.repeatCount = 0;
            return NormalMode{};
        }
        if(next != -1)
            Terminal::unreadKey(next);
    }

    // ========================================================================
    // Leader Key (Space)
    // ========================================================================

    if(ctx.commandBuffer == " ")
    {
        if(c == keyCode(typed::TypedKey::KEY_C))
        {
            commentLeaderPending.emplace(CommentLeaderOrigin::Visual);
            ctx.commandBuffer = " c";
            ctx.setStatusMessage("Leader-c");
            ctx.repeatCount = 0;
            return std::nullopt;
        }
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
            ed->deleteSelection();
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

        // Unknown leader command: cancel leader
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

    if(c == keyCode(typed::TypedKey::KEY_I) ||
       c == keyCode(typed::TypedKey::KEY_A))
    {
        const bool around = c == keyCode(typed::TypedKey::KEY_A);
        textObjectPending.emplace(around);
        ctx.commandBuffer = around ? "va" : "vi";
        ctx.setStatusMessage(around ? "Visual-a" : "Visual-i");
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Exit Visual Mode
    // ========================================================================

    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(typed::TypedKey::KEY_V))
    {
        if(c == keyCode(control::ControlKey::ESC))
            ed->noteDoubleEscStatusClear();
        return NormalMode{};
    }

    // ========================================================================
    // Switch to Visual Line Mode
    // ========================================================================

    if(c == keyCode(typed::TypedKey::KEY_CAP_V))
    {
        return VisualLineMode{};
    }

    // ========================================================================
    // Switch to Visual Block Mode
    // ========================================================================

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
    case keyCode(typed::TypedKey::KEY_H):
    case keyCode(navigation::NavigationKey::ARROW_LEFT):
        ed->moveLeft(count);
        didMove = true;
        break;
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
    case keyCode(typed::TypedKey::KEY_L):
    case keyCode(navigation::NavigationKey::ARROW_RIGHT):
        ed->moveRight(count);
        didMove = true;
        break;

    // Word movements
    case keyCode(typed::TypedKey::KEY_W):
        for(int i = 0; i < count; i++)
            ed->moveWordForward();
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_CAP_W):
        for(int i = 0; i < count; i++)
            ed->moveWordForwardBig();
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_B):
        for(int i = 0; i < count; i++)
            ed->moveWordBackward();
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_CAP_B):
        for(int i = 0; i < count; i++)
            ed->moveWordBackwardBig();
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_E):
        for(int i = 0; i < count; i++)
            ed->moveToEndOfWord();
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_CAP_E):
        for(int i = 0; i < count; i++)
            ed->moveToEndOfWordBig();
        didMove = true;
        break;

    // Line movements
    case keyCode(typed::TypedKey::KEY_0):
        ed->moveToLineStart();
        didMove = true;
        break;
    case keyCode(command::CommandKey::KEY_CARET):
        ed->moveToFirstNonBlank();
        didMove = true;
        break;
    case keyCode(command::CommandKey::KEY_DOLLAR):
        ed->moveToLineEnd();
        didMove = true;
        break;

    // Paragraph movements
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

    // File movements
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
    // Screen movements
    case keyCode(typed::TypedKey::KEY_CAP_H):
        ed->moveToScreenTop();
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_CAP_M):
        ed->moveToScreenMiddle();
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_CAP_L):
        ed->moveToScreenBottom();
        didMove = true;
        break;

    // Scrolling
    case keyCode(control::ControlKey::CTRL_D):
        ed->scrollHalfPageDown(false);
        didMove = true;
        break;
    case keyCode(control::ControlKey::CTRL_U):
        ed->scrollHalfPageUp(false);
        didMove = true;
        break;
    case keyCode(control::ControlKey::CTRL_F):
    case keyCode(navigation::NavigationKey::PAGE_DOWN):
        ed->scrollPageDown();
        didMove = true;
        break;
    case keyCode(control::ControlKey::CTRL_B):
    case keyCode(navigation::NavigationKey::PAGE_UP):
        ed->scrollPageUp();
        didMove = true;
        break;

        // ========================================================================
        // Operations on Selection
        // ========================================================================

    case keyCode(typed::TypedKey::KEY_D):
    case keyCode(typed::TypedKey::KEY_X):
        ed->yankSelection();
        ed->deleteSelection();
        ed->saveState();
        return NormalMode{};

    case keyCode(typed::TypedKey::KEY_Y):
        ed->yankSelection();
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
        const bool useSystemClipboard = ed->useSystemClipboard;
        ed->useSystemClipboard = false;
        ed->deleteSelection();
        ed->yankBuffer = pasteBuffer;
        ed->pasteBefore();
        ed->useSystemClipboard = useSystemClipboard;
        return NormalMode{};
    }

    case keyCode(typed::TypedKey::KEY_C):
        ed->yankSelection();
        ed->deleteSelection();
        ed->saveState();
        return InsertMode{};

    case keyCode(command::CommandKey::KEY_GREATER):
        ed->indentSelection();
        ed->saveState();
        return NormalMode{};

    case keyCode(command::CommandKey::KEY_LESS):
        ed->dedentSelection();
        ed->saveState();
        return NormalMode{};

    case keyCode(command::CommandKey::KEY_TILDE):
        ed->toggleCaseSelection();
        ed->saveState();
        return NormalMode{};

    case keyCode(typed::TypedKey::KEY_U):
        ed->lowercaseSelection();
        ed->saveState();
        return NormalMode{};

    case keyCode(typed::TypedKey::KEY_CAP_U):
        ed->uppercaseSelection();
        ed->saveState();
        return NormalMode{};

    case keyCode(typed::TypedKey::KEY_CAP_J):
    {
        // Join selected lines
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
    {
        std::swap(ctx.cursorX(), ed->currentBuffer->visualStartX);
        std::swap(ctx.cursorY(), ed->currentBuffer->visualStartY);
        didMove = true;
    }
    break;
    }

    if(didMove)
        ctx.repeatCount = 0;

    // Update visual end
    ed->currentBuffer->visualEndX = ctx.cursorX();
    ed->currentBuffer->visualEndY = ctx.cursorY();
    ed->needsFullRedraw = true;

    return std::nullopt;
}
} // namespace editor::statemachine
