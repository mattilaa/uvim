#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <optional>

// ============================================================================
// NormalMode Implementation
// ============================================================================

namespace editor::statemachine
{
void NormalMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    ctx.repeatCount = 0;
    ctx.commandBuffer.clear();
    bufferCommandPending.reset();
    commentLeaderPending.reset();
    windowCommandPending.reset();
    ctx.pendingOperator = 0;
    ctx.pendingAwaitingObject = false;
    ctx.pendingObjectType = 0;
    ctx.pendingCount = 0;

    // Set block cursor for normal mode
    Terminal::setCursorBlock();

#ifdef UVIM_ENABLE_CLANGD_LSP
    ed->syncClangdDiagnosticsIfNeeded(true);
#endif

    ed->needsFullRedraw = true;
}

void NormalMode::on_exit(ModeContext& /* ctx */)
{
    bufferCommandPending.reset();
    commentLeaderPending.reset();
    windowCommandPending.reset();
}

std::optional<ModeState> NormalMode::handle(ModeContext& ctx,
                                            const ModeKeyEvent& event)
{
    const int key = event.key;
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    if(ed->handleRenamePopupKey(c))
    {
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    if(ed->symbolPopupActive && ed->symbolPopupModal)
    {
        const auto popupLineCount = [&]()
        {
            return 1 + (int)std::count(ed->symbolPopupText.begin(),
                                       ed->symbolPopupText.end(), '\n');
        };

        if(c == keyCode(typed::TypedKey::KEY_J))
        {
            const int maxRows = std::max(1, ed->screenRows - 2);
            const int maxScroll = std::max(0, popupLineCount() - maxRows);
            if(ed->symbolPopupScroll < maxScroll)
            {
                ++ed->symbolPopupScroll;
                ed->needsFullRedraw = true;
            }
            ctx.repeatCount = 0;
            return std::nullopt;
        }

        if(c == keyCode(typed::TypedKey::KEY_K))
        {
            if(ed->symbolPopupScroll > 0)
            {
                --ed->symbolPopupScroll;
                ed->needsFullRedraw = true;
            }
            ctx.repeatCount = 0;
            return std::nullopt;
        }

        if(c == keyCode(typed::TypedKey::KEY_Q))
        {
            ed->closeSymbolPopup();
            ctx.repeatCount = 0;
            return std::nullopt;
        }

        if(c == keyCode(typed::TypedKey::KEY_G))
        {
            int nextChar = Terminal::readKey();
            if(nextChar == keyCode(typed::TypedKey::KEY_S))
                ed->closeSymbolPopup();
            ctx.repeatCount = 0;
            return std::nullopt;
        }

        ctx.repeatCount = 0;
        return std::nullopt;
    }

    if(commentLeaderPending)
    {
        std::optional<ModeState> result =
            commentLeaderPending->handle(ctx, ModeKeyEvent{c});
        if(commentLeaderPending->done())
            commentLeaderPending.reset();
        return result;
    }

    if(bufferCommandPending)
    {
        std::optional<ModeState> result =
            bufferCommandPending->handle(ctx, ModeKeyEvent{c});
        if(bufferCommandPending->done())
            bufferCommandPending.reset();
        return result;
    }

    if(windowCommandPending)
    {
        std::optional<ModeState> result =
            windowCommandPending->handle(ctx, ModeKeyEvent{c});
        if(windowCommandPending->done())
            windowCommandPending.reset();
        return result;
    }

    if(c == keyCode(control::ControlKey::PASTE))
    {
        std::string text = Terminal::takeLastPasteText();
        if(text.empty())
            return std::nullopt;
        ed->yankBuffer = text;
        const bool useSystemClipboard = ed->useSystemClipboard;
        ed->useSystemClipboard = false;
        ed->beginChangeRecording(1);
        ed->pasteAfter();
        ed->commitChangeRecording();
        ed->useSystemClipboard = useSystemClipboard;
        ctx.repeatCount = 0;
        return std::nullopt;
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

    if(ed->emojiPopupActive)
    {
        if(c == keyCode(control::ControlKey::CTRL_J) ||
           c == keyCode(navigation::NavigationKey::ARROW_DOWN))
        {
            ed->emojiNext();
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::CTRL_K) ||
           c == keyCode(navigation::NavigationKey::ARROW_UP))
        {
            ed->emojiPrev();
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::ENTER))
        {
            ed->acceptEmoji();
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::ESC) ||
           c == keyCode(control::ControlKey::CTRL_C))
        {
            ed->cancelEmojiPopup();
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::BACKSPACE) ||
           c == keyCode(control::ControlKey::DEL) ||
           c == keyCode(control::ControlKey::CTRL_H))
        {
            if(!ed->emojiQuery.empty())
            {
                ed->emojiQuery.pop_back();
                ed->rebuildEmojiFilter();
            }
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(c >= 32 && c < 127)
        {
            ed->emojiQuery.push_back((char)c);
            ed->rebuildEmojiFilter();
            ctx.repeatCount = 0;
            return std::nullopt;
        }

        ctx.repeatCount = 0;
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::CTRL_E))
    {
        int nextChar = Terminal::readKeyTimeout(300);
        if(nextChar == keyCode(control::ControlKey::CTRL_M) ||
           nextChar == keyCode(control::ControlKey::ENTER))
        {
            ed->openEmojiPopup();
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(nextChar != -1)
            Terminal::unreadKey(nextChar);
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::CTRL_M))
    {
        int nextChar = Terminal::readKeyTimeout(300);
        if(nextChar == keyCode(typed::TypedKey::KEY_H) ||
           nextChar == keyCode(typed::TypedKey::KEY_J) ||
           nextChar == keyCode(typed::TypedKey::KEY_K) ||
           nextChar == keyCode(typed::TypedKey::KEY_L))
        {
            if(nextChar == keyCode(typed::TypedKey::KEY_H) ||
               nextChar == keyCode(typed::TypedKey::KEY_K))
                ed->previousBuffer();
            else
                ed->nextBuffer();
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(nextChar != -1)
            Terminal::unreadKey(nextChar);
        ctx.repeatCount = 0;
        return std::nullopt;
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
    // Escape Handling (double-ESC clears search)
    // ========================================================================

    if(c == keyCode(control::ControlKey::ESC))
    {
        auto now = std::chrono::steady_clock::now();
        auto timeSinceLastEsc =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - ed->lastEscTime)
                .count();

        if(timeSinceLastEsc <= ed->formatOnDoubleEscTimeoutMs)
        {
            if(!ed->searchMatches.empty() || !ed->searchQuery.empty())
            {
                ed->clearSearch();
                ed->needsFullRedraw = true;
            }
            else if(ed->formatOnDoubleEscPending && ed->formatOnInsertLeave)
            {
#ifdef UVIM_ENABLE_FORMATTERS
                ed->formatBuffer();
#endif
            }
            ctx.setStatusMessage("");
            ed->needsFullRedraw = true;
            ed->formatOnDoubleEscPending = false;
            ed->lastEscTime = std::chrono::steady_clock::time_point();
        }
        else
        {
            ed->formatOnDoubleEscPending = false;
            ed->lastEscTime = now;
            ctx.commandBuffer.clear();
            ctx.repeatCount = 0;
            ctx.pendingOperator = 0;
            ctx.pendingAwaitingObject = false;
            ctx.pendingObjectType = 0;
            ctx.pendingCount = 0;
        }
        return std::nullopt;
    }

    if(ed->diagnosticPopupActive && c == keyCode(typed::TypedKey::KEY_Q))
    {
        ed->closeDiagnosticPopup();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    if(ed->splitActive)
    {
        if(c == keyCode(control::ControlKey::CTRL_H))
        {
            ed->switchPaneDirection(-1, 0);
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::CTRL_L))
        {
            ed->switchPaneDirection(1, 0);
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::CTRL_K))
        {
            ed->switchPaneDirection(0, -1);
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::CTRL_J))
        {
            ed->switchPaneDirection(0, 1);
            ctx.repeatCount = 0;
            return std::nullopt;
        }
    }

    if(c == keyCode(control::ControlKey::CTRL_J) ||
       c == keyCode(control::ControlKey::CTRL_K))
    {
        const int direction =
            c == keyCode(control::ControlKey::CTRL_J) ? 1 : -1;
        for(int i = 0; i < count; ++i)
        {
            if(!ed->moveLineBlock(ctx.cursorY(), ctx.cursorY(), direction))
                break;
        }
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    if(ed->showTabs && (c == keyCode(control::ControlKey::CTRL_H) ||
                        c == keyCode(control::ControlKey::CTRL_L)))
    {
        if(c == keyCode(control::ControlKey::CTRL_H))
            ed->previousBuffer();
        else
            ed->nextBuffer();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::SHIFT_CTRL_H))
    {
        ed->moveBufferLeft();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(control::ControlKey::SHIFT_CTRL_L))
    {
        ed->moveBufferRight();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Leader Key (Space)
    // ========================================================================

    if(ctx.commandBuffer == " ")
    {
        std::optional<ModeState> result = handleLeaderKey(ctx, c);
        if(result.has_value())
        {
            return result;
        }
        if(ctx.commandBuffer != " ")
        {
            return std::nullopt;
        }
        ctx.commandBuffer.clear();
        ctx.setStatusMessage("");
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::SPACE))
    {
        ctx.commandBuffer = " ";
        ctx.setStatusMessage("Leader");
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Mode Switching
    // ========================================================================

    // Insert modes
    if(c == keyCode(typed::TypedKey::KEY_I))
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        ed->deferChangeRecordingCommit();
        ctx.repeatCount = 0;
        return InsertMode{};
    }
    if(c == keyCode(typed::TypedKey::KEY_CAP_I))
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        ed->deferChangeRecordingCommit();
        ed->moveToFirstNonBlank();
        ctx.repeatCount = 0;
        return InsertMode{};
    }
    if(c == keyCode(typed::TypedKey::KEY_A))
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        ed->deferChangeRecordingCommit();
        if(ctx.cursorX() < (int)ctx.lines()[ctx.cursorY()].length())
        {
            ctx.cursorX()++;
        }
        ctx.repeatCount = 0;
        return InsertMode{};
    }
    if(c == keyCode(typed::TypedKey::KEY_CAP_A))
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        ed->deferChangeRecordingCommit();
        if(ctx.cursorY() >= 0 && ctx.cursorY() < (int)ctx.lines().size())
        {
            int end = ctx.lines()[ctx.cursorY()].length();
            ctx.cursorX() = end;
            ctx.wantedX() = end;
        }
        ctx.repeatCount = 0;
        return InsertMode{};
    }
    if(c == keyCode(typed::TypedKey::KEY_O))
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        ed->deferChangeRecordingCommit();
        ed->insertLineBelow();
        ctx.repeatCount = 0;
        return InsertMode{};
    }
    if(c == keyCode(typed::TypedKey::KEY_CAP_O))
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        ed->deferChangeRecordingCommit();
        ed->insertLineAbove();
        ctx.repeatCount = 0;
        return InsertMode{};
    }

    // Visual modes
    if(c == keyCode(typed::TypedKey::KEY_V))
    {
        ctx.repeatCount = 0;
        return VisualMode{};
    }
    if(c == keyCode(typed::TypedKey::KEY_CAP_V))
    {
        ctx.repeatCount = 0;
        return VisualLineMode{};
    }
    if(c == keyCode(control::ControlKey::CTRL_V))
    {
        ctx.repeatCount = 0;
        return VisualBlockMode{};
    }

    // Command mode
    if(c == keyCode(command::CommandKey::KEY_COLON))
    {
        ctx.repeatCount = 0;
        return CommandMode{};
    }

    // Quick mode switching
#ifdef UVIM_ENABLE_SEARCH_TOOLS
    if(c == keyCode(control::ControlKey::CTRL_P))
    {
        ctx.repeatCount = 0;
        return FuzzyFindMode{};
    }
#endif
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
    if(c == keyCode(control::ControlKey::CTRL_W))
    {
        ctx.repeatCount = 0;
        return BufferBrowserMode{};
    }
#endif
#ifdef UVIM_ENABLE_SEARCH_TOOLS
    if(c == keyCode(control::ControlKey::CTRL_S))
    {
        ctx.repeatCount = 0;
        return GrepSearchMode{};
    }
    if(c == keyCode(control::ControlKey::CTRL_X))
    {
        ctx.repeatCount = 0;
        return RegexSearchMode{};
    }
#endif

    // Search modes
    if(c == keyCode(command::CommandKey::KEY_SLASH))
    {
        ctx.repeatCount = 0;
        return SearchForwardMode{};
    }
    if(c == keyCode(command::CommandKey::KEY_QUESTION))
    {
        ctx.repeatCount = 0;
        return SearchBackwardMode{};
    }

    // ========================================================================
    // Operators (d, c, y, >, <, =)
    // ========================================================================

    if(c == keyCode(typed::TypedKey::KEY_D) ||
       c == keyCode(typed::TypedKey::KEY_C) ||
       c == keyCode(typed::TypedKey::KEY_Y) ||
       c == keyCode(command::CommandKey::KEY_GREATER) ||
       c == keyCode(command::CommandKey::KEY_LESS) ||
       c == keyCode(command::CommandKey::KEY_EQUAL))
    {
        if(c != keyCode(typed::TypedKey::KEY_Y))
        {
            ed->beginChangeRecording(count);
            ed->recordChangeKey(c);
        }
        return OperatorPendingMode{static_cast<char>(c), count};
    }

    // ========================================================================
    // Basic Movement
    // ========================================================================

    if(c == keyCode(typed::TypedKey::KEY_H) ||
       c == keyCode(navigation::NavigationKey::ARROW_LEFT))
    {
        ed->moveLeft(count);
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(typed::TypedKey::KEY_J) ||
       c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        ed->moveDown(count);
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(typed::TypedKey::KEY_K) ||
       c == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        ed->moveUp(count);
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(typed::TypedKey::KEY_L) ||
       c == keyCode(navigation::NavigationKey::ARROW_RIGHT))
    {
        ed->moveRight(count);
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Word Movement
    // ========================================================================

    if(c == keyCode(typed::TypedKey::KEY_W))
    {
        windowCommandPending.emplace(count);
        ctx.commandBuffer = "w";
        ctx.setStatusMessage("w");
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(typed::TypedKey::KEY_CAP_W))
    {
        for(int i = 0; i < count; i++)
            ed->moveWordForwardBig();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(typed::TypedKey::KEY_B))
    {
        bufferCommandPending.emplace(count);
        ctx.commandBuffer = "b";
        ctx.setStatusMessage("b");
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(typed::TypedKey::KEY_CAP_B))
    {
        for(int i = 0; i < count; i++)
            ed->moveWordBackwardBig();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(typed::TypedKey::KEY_E))
    {
        for(int i = 0; i < count; i++)
            ed->moveToEndOfWord();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(typed::TypedKey::KEY_CAP_E))
    {
        for(int i = 0; i < count; i++)
            ed->moveToEndOfWordBig();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Line Movement
    // ========================================================================

    if(c == keyCode(typed::TypedKey::KEY_0))
    {
        ed->moveToLineStart();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(command::CommandKey::KEY_CARET))
    {
        ed->moveToFirstNonBlank();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(command::CommandKey::KEY_DOLLAR))
    {
        ed->moveToLineEnd();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // File/Screen Movement
    // ========================================================================

    if(c == keyCode(typed::TypedKey::KEY_CAP_G))
    {
        if(ctx.repeatCount > 0)
        {
            ed->moveToLine(ctx.repeatCount - 1);
        }
        else
        {
            ed->moveToLastLine();
        }
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    if(c == keyCode(typed::TypedKey::KEY_G))
    {
        int nextChar = Terminal::readKey();
        return handleGCommand(ctx, nextChar);
    }

    if(c == keyCode(typed::TypedKey::KEY_CAP_H))
    {
        ed->moveToScreenTop();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(typed::TypedKey::KEY_CAP_M))
    {
        ed->moveToScreenMiddle();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(typed::TypedKey::KEY_CAP_L))
    {
        ed->moveToScreenBottom();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Paragraph Movement
    // ========================================================================

    if(c == keyCode(command::CommandKey::KEY_LEFT_BRACE))
    {
        for(int i = 0; i < count; i++)
            ed->moveParagraphBackward();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(command::CommandKey::KEY_RIGHT_BRACE))
    {
        for(int i = 0; i < count; i++)
            ed->moveParagraphForward();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Scrolling
    // ========================================================================

    if(c == keyCode(control::ControlKey::CTRL_D))
    {
        ed->scrollHalfPageDown(false);
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(control::ControlKey::CTRL_U))
    {
        ed->scrollHalfPageUp(false);
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(control::ControlKey::CTRL_F) ||
       c == keyCode(navigation::NavigationKey::PAGE_DOWN))
    {
        ed->scrollPageDown();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(control::ControlKey::CTRL_B) ||
       c == keyCode(navigation::NavigationKey::PAGE_UP))
    {
        ed->scrollPageUp();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Character Search (f, F, t, T)
    // ========================================================================

    if(c == keyCode(typed::TypedKey::KEY_F) ||
       c == keyCode(typed::TypedKey::KEY_CAP_F) ||
       c == keyCode(typed::TypedKey::KEY_T) ||
       c == keyCode(typed::TypedKey::KEY_CAP_T))
    {
        int targetChar = Terminal::readKey();
        if(targetChar != keyCode(control::ControlKey::ESC))
        {
            for(int i = 0; i < count; i++)
            {
                if(c == keyCode(typed::TypedKey::KEY_F))
                    ed->findCharForward(static_cast<char>(targetChar));
                else if(c == keyCode(typed::TypedKey::KEY_CAP_F))
                    ed->findCharBackward(static_cast<char>(targetChar));
                else if(c == keyCode(typed::TypedKey::KEY_T))
                    ed->findCharForwardBefore(static_cast<char>(targetChar));
                else if(c == keyCode(typed::TypedKey::KEY_CAP_T))
                    ed->findCharBackwardAfter(static_cast<char>(targetChar));
            }
        }
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Matching Bracket
    // ========================================================================

    if(c == keyCode(command::CommandKey::KEY_PERCENT))
    {
        ed->moveToMatchingBracket();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Search Navigation
    // ========================================================================

    if(c == keyCode(typed::TypedKey::KEY_N))
    {
        for(int i = 0; i < count; i++)
            ed->searchNext();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(typed::TypedKey::KEY_CAP_N))
    {
        for(int i = 0; i < count; i++)
            ed->searchPrevious();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(command::CommandKey::KEY_ASTERISK))
    {
        ed->searchWordUnderCursor(true);
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(command::CommandKey::KEY_HASH))
    {
        ed->searchWordUnderCursor(false);
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Editing Commands
    // ========================================================================

    if(c == keyCode(typed::TypedKey::KEY_X))
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        for(int i = 0; i < count; i++)
            ed->deleteCharAtCursor();
        ed->needsFullRedraw = true;
        ed->commitChangeRecording();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(typed::TypedKey::KEY_CAP_X))
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        for(int i = 0; i < count; i++)
            ed->deleteCharBeforeCursor();
        ed->needsFullRedraw = true;
        ed->commitChangeRecording();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(typed::TypedKey::KEY_R))
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);

        int replaceChar = Terminal::readKeyTimeout(250);
        if(replaceChar == keyCode(typed::TypedKey::KEY_N))
        {
            ed->recordChangeKey(replaceChar);
            ed->cancelChangeRecording();
            ed->openRenamePopupForCursor();
        }
        else
        {
            if(replaceChar == -1)
                replaceChar = ed->readKeyRecorded();
            else
                ed->recordChangeKey(replaceChar);

            if(replaceChar != keyCode(control::ControlKey::ESC) &&
               replaceChar >= 32)
            {
                ed->replaceCharAtCursor(static_cast<char>(replaceChar));
                ed->commitChangeRecording();
            }
            else
            {
                ed->cancelChangeRecording();
            }
        }
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(typed::TypedKey::KEY_CAP_R))
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        ed->deferChangeRecordingCommit();
        ctx.repeatCount = 0;
        return ReplaceMode{};
    }
    if(c == keyCode(typed::TypedKey::KEY_S))
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        ed->deferChangeRecordingCommit();
        ed->deleteCharAtCursor();
        ctx.repeatCount = 0;
        return InsertMode{};
    }
    if(c == keyCode(typed::TypedKey::KEY_CAP_S))
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        ed->deferChangeRecordingCommit();
        ed->deleteCurrentLine();
        ed->insertLineAbove();
        ctx.repeatCount = 0;
        return InsertMode{};
    }
    if(c == keyCode(typed::TypedKey::KEY_CAP_C))
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        ed->deferChangeRecordingCommit();
        ed->deleteToEndOfLine();
        ed->saveState();
        ed->needsFullRedraw = true;
        ctx.repeatCount = 0;
        return InsertMode{};
    }
    if(c == keyCode(typed::TypedKey::KEY_CAP_D))
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        ed->deleteToEndOfLine();
        ed->saveState();
        ed->needsFullRedraw = true;
        ed->commitChangeRecording();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(typed::TypedKey::KEY_CAP_J))
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        for(int i = 0; i < count; i++)
            ed->joinLines();
        ed->commitChangeRecording();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(command::CommandKey::KEY_TILDE))
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        for(int i = 0; i < count; i++)
            ed->toggleCase();
        ed->commitChangeRecording();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Yank/Put
    // ========================================================================

    if(c == keyCode(typed::TypedKey::KEY_P))
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        for(int i = 0; i < count; i++)
            ed->pasteAfter();
        ed->commitChangeRecording();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(typed::TypedKey::KEY_CAP_P))
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        for(int i = 0; i < count; i++)
            ed->pasteBefore();
        ed->commitChangeRecording();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(typed::TypedKey::KEY_CAP_Y))
    {
        ed->yankLine();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Undo/Redo
    // ========================================================================

    if(c == keyCode(typed::TypedKey::KEY_U))
    {
        ed->undo();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(control::ControlKey::CTRL_R))
    {
        ed->redo();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Marks
    // ========================================================================

    if(c == keyCode(typed::TypedKey::KEY_M))
    {
        int markChar = Terminal::readKey();
        if(markChar >= keyCode(typed::TypedKey::KEY_A) &&
           markChar <= keyCode(typed::TypedKey::KEY_Z))
        {
            ed->setMark(static_cast<char>(markChar));
        }
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(command::CommandKey::KEY_APOSTROPHE) ||
       c == keyCode(command::CommandKey::KEY_BACKTICK))
    {
        int markChar = Terminal::readKey();
        if(markChar >= keyCode(typed::TypedKey::KEY_A) &&
           markChar <= keyCode(typed::TypedKey::KEY_Z))
        {
            ed->jumpToMark(static_cast<char>(markChar));
        }
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Jump List
    // ========================================================================

    if(c == keyCode(control::ControlKey::CTRL_O))
    {
        ed->jumpBack();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(control::ControlKey::CTRL_I))
    {
        ed->jumpForward();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Misc Commands
    // ========================================================================

    if(c == keyCode(command::CommandKey::KEY_DOT))
    {
        ed->repeatLastChange(ctx.repeatCount);
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(control::ControlKey::CTRL_G))
    {
        ed->showFileInfo();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == keyCode(control::ControlKey::CTRL_L))
    {
        ed->forceFullRedraw();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == 30) // Ctrl+6 / Ctrl+^
    {
        ed->switchToAlternateFile();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Z Commands (Scrolling)
    // ========================================================================

    if(c == keyCode(typed::TypedKey::KEY_Z))
    {
        int nextChar = Terminal::readKey();
        return handleZCommand(ctx, nextChar);
    }

    ctx.repeatCount = 0;
    return std::nullopt;
}

std::optional<ModeState> NormalMode::handleLeaderKey(ModeContext& ctx, int c)
{
    Editor* ed = ctx.editor;
#ifdef UVIM_ENABLE_BROWSER_TOOLS
    auto openFileBrowser =
        [&](bool focusCurrentFile) -> std::optional<ModeState>
    {
        std::string dir = ".";
        if(ed->filename && !ed->filename->empty())
        {
            std::string_view parent = text_utils::dirname(*ed->filename);
            if(!parent.empty())
                dir = parent;
        }
        std::string prev;
        if(ed->currentBuffer != nullptr && ed->filename)
        {
            prev = *ed->filename;
        }
        return FileBrowserMode{dir, prev, focusCurrentFile};
    };
#endif

    switch(c)
    {
    case keyCode(typed::TypedKey::KEY_F):
#ifdef UVIM_ENABLE_FORMATTERS
        ed->formatBuffer();
#else
        ed->setStatusMessage("formatters are not compiled in");
#endif
        return std::nullopt;

    case keyCode(typed::TypedKey::KEY_G):
    {
        // <leader>g prefix - wait for next char
        // <leader>g alone = grep search (legacy)
        // <leader>ga = show assembly instruction docs popup
        // <leader>gr = find references
        // <leader>gd = go to definition
        int nextChar = Terminal::readKeyTimeout(500);
        if(nextChar == -1 || nextChar == keyCode(control::ControlKey::ESC))
        {
            // Timeout or cancel - default to grep search
#ifdef UVIM_ENABLE_SEARCH_TOOLS
            return GrepSearchMode{};
#else
            return std::nullopt;
#endif
        }
        else if(nextChar == keyCode(typed::TypedKey::KEY_R))
        {
            // <leader>gr - Find all references
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
            ed->findReferences();
            if(ed->hasReferences())
            {
                return ReferencesMode{};
            }
#endif
            // No references found, stay in normal mode
            return std::nullopt;
        }
        else if(nextChar == keyCode(typed::TypedKey::KEY_D))
        {
            // <leader>gd - Go to definition
            ed->goToDefinition();
            return std::nullopt;
        }
        else if(nextChar == keyCode(typed::TypedKey::KEY_A))
        {
            // <leader>ga - Show assembly instruction documentation popup
#ifdef UVIM_ENABLE_ASM_DOCS
            ed->openAsmDocumentationPopupForCursor();
#else
            ed->setStatusMessage("assembly docs are not compiled in");
#endif
            return std::nullopt;
        }
        else
        {
            // Unknown <leader>g command - default to grep
#ifdef UVIM_ENABLE_SEARCH_TOOLS
            return GrepSearchMode{};
#else
            return std::nullopt;
#endif
        }
    }

    case keyCode(typed::TypedKey::KEY_R):
    {
        // <leader>r prefix - LSP commands
        // <leader>rr = find references (alternative binding)
        int nextChar = Terminal::readKeyTimeout(500);
        if(nextChar == keyCode(typed::TypedKey::KEY_R))
        {
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
            ed->findReferences();
            if(ed->hasReferences())
            {
                return ReferencesMode{};
            }
#endif
        }
        return std::nullopt;
    }

    case keyCode(typed::TypedKey::KEY_C):
    {
        commentLeaderPending.emplace(CommentLeaderOrigin::Normal);
        ctx.commandBuffer = " c";
        ctx.setStatusMessage("Leader-c");
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    case keyCode(typed::TypedKey::KEY_X):
    {
#ifdef UVIM_ENABLE_BROWSER_TOOLS
        const int nextChar = Terminal::readKeyTimeout(300);
        if(nextChar == keyCode(typed::TypedKey::KEY_X))
            return openFileBrowser(false);
        if(nextChar != -1)
            Terminal::unreadKey(nextChar);
        return openFileBrowser(true);
#else
        return std::nullopt;
#endif
    }

    case keyCode(typed::TypedKey::KEY_E):
    {
#if defined(UVIM_ENABLE_BROWSER_TOOLS) || defined(UVIM_ENABLE_AUXILIARY_VIEWS)
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
        int nextChar = Terminal::readKeyTimeout(300);
        if(nextChar == keyCode(typed::TypedKey::KEY_M))
        {
            ed->openEmojiPopup();
            return std::nullopt;
        }
        if(nextChar != -1)
            Terminal::unreadKey(nextChar);

        // Diagnostics popup for current line
        if(!ed->emitLspDiagnostics)
        {
            ed->setStatusMessage("emitlsp=false");
            return std::nullopt;
        }
        ed->syncClangdDiagnosticsIfNeeded(true);
        ed->syncMlangSemanticTokensIfNeeded(true);
        if(ed->getClangdDiagnosticForLine(*ed->cursorY))
        {
            ed->openDiagnosticPopupForCursor();
            return std::nullopt;
        }
#endif
#ifdef UVIM_ENABLE_BROWSER_TOOLS
        return openFileBrowser(true);
#else
        return std::nullopt;
#endif
#else
        return std::nullopt;
#endif
    }

    case keyCode(typed::TypedKey::KEY_H):
        // Move current buffer left in the tab bar.
        ed->moveBufferLeft();
        break;

    case keyCode(typed::TypedKey::KEY_Y):
        // Yank to system clipboard
        ed->yankToSystemClipboard();
        break;

    case keyCode(typed::TypedKey::KEY_P):
        // Paste from system clipboard
#ifdef UVIM_ENABLE_SYSTEM_CLIPBOARD
        ed->pasteFromSystemClipboard();
#else
        ed->setStatusMessage("system clipboard is not compiled in");
#endif
        break;

    case keyCode(typed::TypedKey::KEY_L):
        // Move current buffer right in the tab bar.
        ed->moveBufferRight();
        break;

    case keyCode(typed::TypedKey::KEY_W):
        // Save file
        ed->saveFile();
        break;

    case keyCode(typed::TypedKey::KEY_S):
        // Show signature popup for symbol under cursor
        ed->openSymbolPopupForCursor();
        break;

    case keyCode(typed::TypedKey::KEY_Q):
        // Quit
        ed->forceQuit();
        break;

    case keyCode(typed::TypedKey::KEY_N):
        // Clear search highlight
        ed->clearSearch();
        break;

    case keyCode(command::CommandKey::KEY_SLASH):
        // Project-wide search
#ifdef UVIM_ENABLE_SEARCH_TOOLS
        return GrepSearchMode{};
#else
        return std::nullopt;
#endif

    case keyCode(typed::TypedKey::KEY_D):
        // <leader>d - Go to definition (alternative)
        ed->goToDefinition();
        break;

    case keyCode(typed::TypedKey::KEY_1):
    case keyCode(typed::TypedKey::KEY_2):
    case keyCode(typed::TypedKey::KEY_3):
    case keyCode(typed::TypedKey::KEY_4):
    case keyCode(typed::TypedKey::KEY_5):
    case keyCode(typed::TypedKey::KEY_6):
    case keyCode(typed::TypedKey::KEY_7):
    case keyCode(typed::TypedKey::KEY_8):
    case keyCode(typed::TypedKey::KEY_9):
        // Switch to buffer by number
        ed->switchToBuffer(c - keyCode(typed::TypedKey::KEY_1));
        break;

    case keyCode(control::ControlKey::SPACE):
        // Double space - do nothing
        break;

    default:
        // Unknown leader command
        ctx.setStatusMessage("Unknown leader command");
        break;
    }

    return std::nullopt;
}

std::optional<ModeState> NormalMode::handleGCommand(ModeContext& ctx, int c)
{
    Editor* ed = ctx.editor;

    switch(c)
    {
    case keyCode(typed::TypedKey::KEY_G):
        // gg - go to first line
        ed->moveToFirstLine();
        break;

    case keyCode(typed::TypedKey::KEY_D):
        // gd - go to definition
        ed->goToDefinition();
        break;

    case keyCode(typed::TypedKey::KEY_F):
        // gf - go to file under cursor
        ed->goToFile();
        break;

    case keyCode(typed::TypedKey::KEY_S):
        // gs - show clang-computed size for symbol under cursor
        ed->openSizePopupForCursor();
        break;

    case keyCode(typed::TypedKey::KEY_H):
        // gh - alternate file (header/source)
        ed->jumpToAlternateFile();
        break;

    case keyCode(typed::TypedKey::KEY_A):
        // ga - git stage view
        ed->openGitStageMode();
        break;

    case keyCode(typed::TypedKey::KEY_J):
        if(ed->showGitBlame)
        {
            // gj - show commit diff for line under cursor when blame is visible
            ed->openGitShowCommitMode();
        }
        else
        {
            ctx.setStatusMessage("gj requires git blame gutter");
        }
        break;

    case keyCode(typed::TypedKey::KEY_B):
    {
        int nextChar = Terminal::readKeyTimeout(500);
        if(nextChar == keyCode(typed::TypedKey::KEY_B))
        {
            // gbb - extended git blame gutter with date/time
            ed->toggleGitBlame(true);
            return std::nullopt;
        }
        if(nextChar == keyCode(typed::TypedKey::KEY_L))
        {
            // gbl - open git log at the commit blamed for the cursor line
            ed->openGitLogModeForBlameLine();
            return std::nullopt;
        }
        if(nextChar == keyCode(typed::TypedKey::KEY_V))
        {
            ed->openGitShowCommitMode();
            return std::nullopt;
        }
        // gb - toggle git blame gutter
        ed->toggleGitBlame();
        break;
    }

    case keyCode(typed::TypedKey::KEY_L):
    {
        int nextChar = Terminal::readKeyTimeout(500);
        if(nextChar == keyCode(typed::TypedKey::KEY_F))
        {
            // glf - git log for current file
            ed->openGitLogModeForFile();
            return std::nullopt;
        }
        // gl - git log view
        ed->openGitLogMode();
        break;
    }

    case keyCode(typed::TypedKey::KEY_R):
        // gr - find references
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
        ed->findReferences();
        if(ed->hasReferences())
        {
            return ReferencesMode{};
        }
#endif
        break;

    default:
        ctx.setStatusMessage("Unknown g command");
        break;
    }

    ctx.repeatCount = 0;
    return std::nullopt;
}

std::optional<ModeState> NormalMode::handleZCommand(ModeContext& ctx, int c)
{
    Editor* ed = ctx.editor;

    switch(c)
    {
    case keyCode(typed::TypedKey::KEY_Z):
        // zz - center cursor on screen
        ed->centerScreen();
        break;

    case keyCode(typed::TypedKey::KEY_T):
        // zt - scroll cursor to top
        ed->scrollToTop();
        break;

    case keyCode(typed::TypedKey::KEY_B):
        // zb - scroll cursor to bottom
        ed->scrollToBottom();
        break;

    default:
        ctx.setStatusMessage("Unknown z command");
        break;
    }

    ctx.repeatCount = 0;
    return std::nullopt;
}
} // namespace editor::statemachine
