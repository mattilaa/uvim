#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#include <cctype>
#include <chrono>
#include <optional>

namespace {

std::optional<std::string> promptReplaceWordInput(Editor* ed,
                                                  const std::string& target)
{
    std::string input;
    while(true)
    {
        ed->setStatusMessage("rn: replace '" + target + "' with: " + input);
        ed->needsFullRedraw = true;
        ed->refreshScreen();

        int key = Terminal::readKey();
        if(key == Terminal::ESC || key == Terminal::CTRL_C)
            return std::nullopt;
        if(key == Terminal::ENTER)
            return input;
        if(key == Terminal::BACKSPACE || key == Terminal::DEL ||
           key == Terminal::CTRL_H || key == 127)
        {
            if(!input.empty())
                input.pop_back();
            continue;
        }
        if(key >= 32 && key < 127)
            input.push_back(static_cast<char>(key));
    }
}

int replaceWholeWordInCurrentBuffer(Editor* ed, const std::string& from,
                                    const std::string& to)
{
    if(!ed || !ed->lines || from.empty())
        return 0;

    int replaced = 0;
    for(std::string& line : *ed->lines)
    {
        size_t pos = 0;
        while(pos < line.size())
        {
            pos = line.find(from, pos);
            if(pos == std::string::npos)
                break;

            size_t end = pos + from.size();
            bool leftOk = (pos == 0) || !text_utils::isIdent(line[pos - 1]);
            bool rightOk =
                (end >= line.size()) || !text_utils::isIdent(line[end]);
            if(leftOk && rightOk)
            {
                line.replace(pos, from.size(), to);
                ++replaced;
                pos += to.size();
            }
            else
            {
                pos += from.size();
            }
        }
    }

    return replaced;
}

} // namespace

// ============================================================================
// NormalMode Implementation
// ============================================================================

void NormalMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    ctx.repeatCount = 0;
    ctx.commandBuffer.clear();
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
    // Nothing specific to do on exit
}

std::optional<ModeState> NormalMode::handle(ModeContext& ctx,
                                            const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    if(ed->diagnosticPopupActive)
    {
        if(c == 'q')
        {
            ed->closeDiagnosticPopup();
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(c == Terminal::CTRL_J || c == Terminal::ARROW_DOWN)
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
        if(c == Terminal::CTRL_K || c == Terminal::ARROW_UP)
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
        if(c == Terminal::ENTER)
        {
            ed->applyDiagnosticFix(ed->diagnosticPopupFixIndex);
            ctx.repeatCount = 0;
            return std::nullopt;
        }
    }

    if(ed->emojiPopupActive)
    {
        if(c == Terminal::CTRL_J || c == Terminal::ARROW_DOWN || c == 'j')
        {
            ed->emojiNext();
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(c == Terminal::CTRL_K || c == Terminal::ARROW_UP || c == 'k')
        {
            ed->emojiPrev();
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(c == Terminal::ENTER)
        {
            ed->acceptEmoji();
            ctx.repeatCount = 0;
            return InsertMode{};
        }
        if(c == Terminal::ESC || c == Terminal::CTRL_C)
        {
            ed->cancelEmojiPopup();
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(c == Terminal::BACKSPACE || c == Terminal::DEL ||
           c == Terminal::CTRL_H)
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

    if(c == Terminal::CTRL_E)
    {
        int nextChar = Terminal::readKeyTimeout(300);
        if(nextChar == Terminal::CTRL_M || nextChar == Terminal::ENTER)
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

    if(c == Terminal::CTRL_M)
    {
        int nextChar = Terminal::readKeyTimeout(300);
        if(nextChar == 'h' || nextChar == 'j' || nextChar == 'k' ||
           nextChar == 'l')
        {
            if(nextChar == 'h' || nextChar == 'k')
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

    if(ctx.repeatCount == 0 && c >= '1' && c <= '9')
    {
        ctx.repeatCount = c - '0';
        return std::nullopt;
    }
    if(ctx.repeatCount > 0 && c >= '0' && c <= '9')
    {
        ctx.repeatCount = ctx.repeatCount * 10 + (c - '0');
        return std::nullopt;
    }

    int count = std::max(1, ctx.repeatCount);

    // ========================================================================
    // Escape Handling (double-ESC clears search)
    // ========================================================================

    if(c == Terminal::ESC)
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
            else if(ed->formatOnDoubleEscPending && ed->formatOnInsertLeave &&
                    ed->isFileType<FileType::Cpp>() &&
                    !ed->isFileType<FileType::Mla>())
            {
                ed->clangFormatWithArgs("", "clang-format: formatted file");
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

    if(ed->diagnosticPopupActive && c == 'q')
    {
        ed->closeDiagnosticPopup();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    if(ed->splitActive)
    {
        if(c == Terminal::CTRL_J || c == Terminal::CTRL_K)
        {
            ed->switchPane();
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(c == Terminal::CTRL_H)
        {
            ed->previousBuffer();
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(c == Terminal::CTRL_L)
        {
            ed->nextBuffer();
            ctx.repeatCount = 0;
            return std::nullopt;
        }
    }

    if(ed->showTabs && (c == Terminal::CTRL_H || c == Terminal::CTRL_L))
    {
        if(c == Terminal::CTRL_H)
            ed->previousBuffer();
        else
            ed->nextBuffer();
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

    // ========================================================================
    // Leader-b Buffer Commands
    // ========================================================================

    if(ctx.commandBuffer == " b")
    {
        if(c == 'd')
        {
            ctx.commandBuffer.clear();
            ctx.setStatusMessage("");
            ctx.repeatCount = 0;
            ed->closeCurrentBuffer();
            return std::nullopt;
        }

        ctx.commandBuffer.clear();
        ctx.setStatusMessage("");
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    if(c == ' ')
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
    if(c == 'i')
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        ed->deferChangeRecordingCommit();
        ctx.repeatCount = 0;
        return InsertMode{};
    }
    if(c == 'I')
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        ed->deferChangeRecordingCommit();
        ed->moveToFirstNonBlank();
        ctx.repeatCount = 0;
        return InsertMode{};
    }
    if(c == 'a')
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
    if(c == 'A')
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
    if(c == 'o')
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        ed->deferChangeRecordingCommit();
        ed->insertLineBelow();
        ctx.repeatCount = 0;
        return InsertMode{};
    }
    if(c == 'O')
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        ed->deferChangeRecordingCommit();
        ed->insertLineAbove();
        ctx.repeatCount = 0;
        return InsertMode{};
    }

    // Visual modes
    if(c == 'v')
    {
        ctx.repeatCount = 0;
        return VisualMode{};
    }
    if(c == 'V')
    {
        ctx.repeatCount = 0;
        return VisualLineMode{};
    }
    if(c == Terminal::CTRL_V)
    {
        ctx.repeatCount = 0;
        return VisualBlockMode{};
    }

    // Command mode
    if(c == ':')
    {
        ctx.repeatCount = 0;
        return CommandMode{};
    }

    // Quick mode switching
    if(c == Terminal::CTRL_P)
    {
        ctx.repeatCount = 0;
        return FuzzyFindMode{};
    }
    if(c == Terminal::CTRL_W)
    {
        ctx.repeatCount = 0;
        return BufferBrowserMode{};
    }
    if(c == Terminal::CTRL_S)
    {
        ctx.repeatCount = 0;
        return GrepSearchMode{};
    }

    // Search modes
    if(c == '/')
    {
        ctx.repeatCount = 0;
        return SearchForwardMode{};
    }
    if(c == '?')
    {
        ctx.repeatCount = 0;
        return SearchBackwardMode{};
    }

    // ========================================================================
    // Operators (d, c, y, >, <, =)
    // ========================================================================

    if(c == 'd' || c == 'c' || c == 'y' || c == '>' || c == '<' || c == '=')
    {
        if(c != 'y')
        {
            ed->beginChangeRecording(count);
            ed->recordChangeKey(c);
        }
        return OperatorPendingMode{static_cast<char>(c), count};
    }

    // ========================================================================
    // Basic Movement
    // ========================================================================

    if(c == 'h' || c == Terminal::ARROW_LEFT)
    {
        ed->moveLeft(count);
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == 'j' || c == Terminal::ARROW_DOWN)
    {
        ed->moveDown(count);
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == 'k' || c == Terminal::ARROW_UP)
    {
        ed->moveUp(count);
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == 'l' || c == Terminal::ARROW_RIGHT)
    {
        ed->moveRight(count);
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Word Movement
    // ========================================================================

    if(c == 'w')
    {
        for(int i = 0; i < count; i++)
            ed->moveWordForward();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == 'W')
    {
        for(int i = 0; i < count; i++)
            ed->moveWordForwardBig();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == 'b')
    {
        for(int i = 0; i < count; i++)
            ed->moveWordBackward();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == 'B')
    {
        for(int i = 0; i < count; i++)
            ed->moveWordBackwardBig();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == 'e')
    {
        for(int i = 0; i < count; i++)
            ed->moveToEndOfWord();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == 'E')
    {
        for(int i = 0; i < count; i++)
            ed->moveToEndOfWordBig();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Line Movement
    // ========================================================================

    if(c == '0')
    {
        ed->moveToLineStart();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == '^')
    {
        ed->moveToFirstNonBlank();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == '$')
    {
        ed->moveToLineEnd();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // File/Screen Movement
    // ========================================================================

    if(c == 'G')
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

    if(c == 'g')
    {
        int nextChar = Terminal::readKey();
        return handleGCommand(ctx, nextChar);
    }

    if(c == 'H')
    {
        ed->moveToScreenTop();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == 'M')
    {
        ed->moveToScreenMiddle();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == 'L')
    {
        ed->moveToScreenBottom();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Paragraph Movement
    // ========================================================================

    if(c == '{')
    {
        for(int i = 0; i < count; i++)
            ed->moveParagraphBackward();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == '}')
    {
        for(int i = 0; i < count; i++)
            ed->moveParagraphForward();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Scrolling
    // ========================================================================

    if(c == Terminal::CTRL_D)
    {
        ed->scrollHalfPageDown(false);
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == Terminal::CTRL_U)
    {
        ed->scrollHalfPageUp(false);
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == Terminal::CTRL_F || c == Terminal::PAGE_DOWN)
    {
        ed->scrollPageDown();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == Terminal::CTRL_B || c == Terminal::PAGE_UP)
    {
        ed->scrollPageUp();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Character Search (f, F, t, T)
    // ========================================================================

    if(c == 'f' || c == 'F' || c == 't' || c == 'T')
    {
        int targetChar = Terminal::readKey();
        if(targetChar != Terminal::ESC)
        {
            for(int i = 0; i < count; i++)
            {
                if(c == 'f')
                    ed->findCharForward(static_cast<char>(targetChar));
                else if(c == 'F')
                    ed->findCharBackward(static_cast<char>(targetChar));
                else if(c == 't')
                    ed->findCharForwardBefore(static_cast<char>(targetChar));
                else if(c == 'T')
                    ed->findCharBackwardAfter(static_cast<char>(targetChar));
            }
        }
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Matching Bracket
    // ========================================================================

    if(c == '%')
    {
        ed->moveToMatchingBracket();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Search Navigation
    // ========================================================================

    if(c == 'n')
    {
        for(int i = 0; i < count; i++)
            ed->searchNext();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == 'N')
    {
        for(int i = 0; i < count; i++)
            ed->searchPrevious();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == '*')
    {
        ed->searchWordUnderCursor(true);
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == '#')
    {
        ed->searchWordUnderCursor(false);
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Editing Commands
    // ========================================================================

    if(c == 'x')
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
    if(c == 'X')
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
    if(c == 'r')
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);

        int replaceChar = Terminal::readKeyTimeout(250);
        if(replaceChar == 'n')
        {
            ed->recordChangeKey(replaceChar);
            std::string target = ed->getSymbolUnderCursor();
            if(target.empty())
            {
                ed->setStatusMessage("rn: no word under cursor");
                ed->cancelChangeRecording();
                ctx.repeatCount = 0;
                return std::nullopt;
            }

            auto replacement = promptReplaceWordInput(ed, target);
            if(!replacement)
            {
                ed->setStatusMessage("rn: cancelled");
                ed->cancelChangeRecording();
                ctx.repeatCount = 0;
                return std::nullopt;
            }

            if(*replacement == target)
            {
                ed->setStatusMessage("rn: no changes");
                ed->cancelChangeRecording();
                ctx.repeatCount = 0;
                return std::nullopt;
            }

            int replaced = replaceWholeWordInCurrentBuffer(ed, target, *replacement);
            if(replaced <= 0)
            {
                ed->setStatusMessage("rn: no matches for '" + target + "'");
                ed->cancelChangeRecording();
                ctx.repeatCount = 0;
                return std::nullopt;
            }

            *ed->dirty = true;
            ed->saveState();
            ed->needsFullRedraw = true;
            ed->setStatusMessage("rn: replaced " + std::to_string(replaced) +
                                 " occurrence(s) of '" + target + "'");
            ed->commitChangeRecording();
        }
        else
        {
            if(replaceChar == -1)
                replaceChar = ed->readKeyRecorded();
            else
                ed->recordChangeKey(replaceChar);

            if(replaceChar != Terminal::ESC && replaceChar >= 32)
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
    if(c == 'R')
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        ed->deferChangeRecordingCommit();
        ctx.repeatCount = 0;
        return ReplaceMode{};
    }
    if(c == 's')
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        ed->deferChangeRecordingCommit();
        ed->deleteCharAtCursor();
        ctx.repeatCount = 0;
        return InsertMode{};
    }
    if(c == 'S')
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        ed->deferChangeRecordingCommit();
        ed->deleteCurrentLine();
        ed->insertLineAbove();
        ctx.repeatCount = 0;
        return InsertMode{};
    }
    if(c == 'C')
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
    if(c == 'D')
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
    if(c == 'J')
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        for(int i = 0; i < count; i++)
            ed->joinLines();
        ed->commitChangeRecording();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == '~')
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

    if(c == 'p')
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        for(int i = 0; i < count; i++)
            ed->pasteAfter();
        ed->commitChangeRecording();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == 'P')
    {
        ed->beginChangeRecording(count);
        ed->recordChangeKey(c);
        for(int i = 0; i < count; i++)
            ed->pasteBefore();
        ed->commitChangeRecording();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == 'Y')
    {
        ed->yankLine();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Undo/Redo
    // ========================================================================

    if(c == 'u')
    {
        ed->undo();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == Terminal::CTRL_R)
    {
        ed->redo();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Marks
    // ========================================================================

    if(c == 'm')
    {
        int markChar = Terminal::readKey();
        if(markChar >= 'a' && markChar <= 'z')
        {
            ed->setMark(static_cast<char>(markChar));
        }
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == '\'' || c == '`')
    {
        int markChar = Terminal::readKey();
        if(markChar >= 'a' && markChar <= 'z')
        {
            ed->jumpToMark(static_cast<char>(markChar));
        }
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Jump List
    // ========================================================================

    if(c == Terminal::CTRL_O)
    {
        ed->jumpBack();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == Terminal::CTRL_I)
    {
        ed->jumpForward();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Misc Commands
    // ========================================================================

    if(c == '.')
    {
        ed->repeatLastChange(ctx.repeatCount);
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == Terminal::CTRL_G)
    {
        ed->showFileInfo();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == Terminal::CTRL_L)
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

    if(c == 'z')
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
    auto openFileBrowser = [&]() -> std::optional<ModeState>
    {
        std::string dir = ".";
        if(!ed->filename->empty())
        {
            size_t lastSlash = ed->filename->find_last_of("/");
            if(lastSlash != std::string::npos)
            {
                dir = ed->filename->substr(0, lastSlash);
                if(dir.empty())
                    dir = "/";
            }
        }
        std::string prev;
        if(ed->currentBuffer != nullptr && ed->filename)
        {
            prev = *ed->filename;
        }
        return FileBrowserMode{dir, prev};
    };

    switch(c)
    {
    case 'f':
        if(ed->isFileType<FileType::Python>())
        {
            ed->pythonFormatBuffer();
        }
        else if(ed->isFileType<FileType::Robot>())
        {
            ed->robotFormatBuffer();
        }
        else if(ed->isFileType<FileType::Json>())
        {
            ed->jsonFormatBuffer();
        }
        else if(ed->isFileType<FileType::Yaml>())
        {
            ed->yamlFormatBuffer();
        }
        else if(ed->isFileType<FileType::Mla>())
        {
            ed->mlangFormatBuffer();
        }
        else
        {
            ed->clangFormatWithArgs("", "clang-format: formatted file");
        }
        return std::nullopt;

    case 'b':
        // Buffer browser
        ctx.commandBuffer = " b";
        ctx.setStatusMessage("Leader-b");
        ctx.repeatCount = 0;
        return std::nullopt;

    case 'g':
    {
        // <leader>g prefix - wait for next char
        // <leader>g alone = grep search (legacy)
        // <leader>gr = find references
        // <leader>gd = go to definition
        int nextChar = Terminal::readKeyTimeout(500);
        if(nextChar == -1 || nextChar == Terminal::ESC)
        {
            // Timeout or cancel - default to grep search
            return GrepSearchMode{};
        }
        else if(nextChar == 'r')
        {
            // <leader>gr - Find all references
            ed->findReferences();
            if(ed->hasReferences())
            {
                return ReferencesMode{};
            }
            // No references found, stay in normal mode
            return std::nullopt;
        }
        else if(nextChar == 'd')
        {
            // <leader>gd - Go to definition
            ed->goToDefinition();
            return std::nullopt;
        }
        else
        {
            // Unknown <leader>g command - default to grep
            return GrepSearchMode{};
        }
    }

    case 'r':
    {
        // <leader>r prefix - LSP commands
        // <leader>rr = find references (alternative binding)
        int nextChar = Terminal::readKeyTimeout(500);
        if(nextChar == 'r')
        {
            ed->findReferences();
            if(ed->hasReferences())
            {
                return ReferencesMode{};
            }
        }
        return std::nullopt;
    }

    case 'x':
        return openFileBrowser();

    case 'e':
    {
        int nextChar = Terminal::readKeyTimeout(300);
        if(nextChar == 'm')
        {
            ed->openEmojiPopup();
            return std::nullopt;
        }
        if(nextChar != -1)
            Terminal::unreadKey(nextChar);

        // Diagnostics popup for current line
        if(ed->getClangdDiagnosticForLine(*ed->cursorY))
        {
            ed->openDiagnosticPopupForCursor();
            return std::nullopt;
        }
        return openFileBrowser();
    }

    case 'h':
        // Jump to alternate file (header/source)
        ed->jumpToAlternateFile();
        break;

    case 'y':
        // Yank to system clipboard
        ed->yankToSystemClipboard();
        break;

    case 'p':
        // Paste from system clipboard
        ed->pasteFromSystemClipboard();
        break;

    case 'l':
    {
        int nextChar = Terminal::readKeyTimeout(500);
        if(nextChar == 'i')
        {
            ed->showLspInfo();
            return LspInfoMode{};
        }
        if(ed->isFileType<FileType::Python>())
        {
            ed->pythonLintBuffer();
        }
        else
        {
            ctx.setStatusMessage("lint: unsupported filetype");
        }
        break;
    }

    case 'w':
        // Save file
        ed->saveFile();
        break;

    case 's':
        // Show signature popup for symbol under cursor
        ed->openSymbolPopupForCursor();
        break;

    case 'q':
        // Quit
        ed->forceQuit();
        break;

    case 'n':
        // Clear search highlight
        ed->clearSearch();
        break;

    case '/':
        // Project-wide search
        return GrepSearchMode{};

    case 'd':
        // <leader>d - Go to definition (alternative)
        ed->goToDefinition();
        break;

    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        // Switch to buffer by number
        ed->switchToBuffer(c - '1');
        break;

    case ' ':
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
    case 'g':
        // gg - go to first line
        ed->moveToFirstLine();
        break;

    case 'd':
        // gd - go to definition
        ed->goToDefinition();
        break;

    case 'f':
        // gf - go to file under cursor
        ed->goToFile();
        break;

    case 'b':
    {
        int nextChar = Terminal::readKeyTimeout(500);
        if(nextChar == 'v')
        {
            ed->openGitShowCommitMode();
            return std::nullopt;
        }
        // gb - toggle git blame gutter
        ed->toggleGitBlame();
        break;
    }

    case 'l':
    {
        int nextChar = Terminal::readKeyTimeout(500);
        if(nextChar == 'f')
        {
            // glf - git log for current file
            ed->openGitLogModeForFile();
            return std::nullopt;
        }
        // gl - git log view
        ed->openGitLogMode();
        break;
    }

    case 'r':
        // gr - find references
        ed->findReferences();
        if(ed->hasReferences())
        {
            return ReferencesMode{};
        }
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
    case 'z':
        // zz - center cursor on screen
        ed->centerScreen();
        break;

    case 't':
        // zt - scroll cursor to top
        ed->scrollToTop();
        break;

    case 'b':
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

// ============================================================================
// ReplaceMode Implementation
// ============================================================================

void ReplaceMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    ed->needsFullRedraw = true;

    // Set cursor to bar for replace mode (no underline available)
    Terminal::setCursorBarBlinking();
}

void ReplaceMode::on_exit(ModeContext& ctx)
{
    ctx.editor->finishChangeRecordingIfDeferred();

    // Restore block cursor
    Terminal::setCursorBlock();
}

std::optional<ModeState> ReplaceMode::handle(ModeContext& ctx,
                                             const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;
    if(ed->isRecordingChange() && !ed->isReplayingChange())
    {
        ed->recordChangeKey(c);
    }

    // ========================================================================
    // Exit Replace Mode
    // ========================================================================

    if(c == Terminal::ESC || c == Terminal::CTRL_C)
    {
        if(ctx.cursorX() > 0)
        {
            ctx.cursorX()--;
        }
        ed->saveState();
        return NormalMode{};
    }

    // ========================================================================
    // Backspace
    // ========================================================================

    if(c == Terminal::BACKSPACE || c == 127 || c == Terminal::CTRL_H)
    {
        if(ctx.cursorX() > 0)
        {
            ctx.cursorX()--;
            // In replace mode, backspace doesn't delete, just moves back
        }
        return std::nullopt;
    }

    // ========================================================================
    // Character Replacement
    // ========================================================================

    if(c >= 32 && c < 127)
    {
        auto& lines = ctx.lines();
        int& cursorX = ctx.cursorX();
        int cursorY = ctx.cursorY();

        if(cursorY < (int)lines.size())
        {
            std::string& line = lines[cursorY];
            if(cursorX < (int)line.length())
            {
                line[cursorX] = static_cast<char>(c);
            }
            else
            {
                line += static_cast<char>(c);
            }
            cursorX++;
            *ed->dirty = true;
        }
        return std::nullopt;
    }

    // ========================================================================
    // Enter - Insert Newline
    // ========================================================================

    if(c == Terminal::ENTER)
    {
        ed->insertNewline();
        return std::nullopt;
    }

    return std::nullopt;
}
