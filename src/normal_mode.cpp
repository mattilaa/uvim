#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include <chrono>
#include <cctype>

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

        if(timeSinceLastEsc <= Editor::DOUBLE_ESC_TIMEOUT_MS &&
           (!ed->searchMatches.empty() || !ed->searchQuery.empty()))
        {
            ed->clearSearch();
            ctx.setStatusMessage("Search cleared");
            ed->needsFullRedraw = true;
            ed->lastEscTime = std::chrono::steady_clock::time_point();
        }
        else
        {
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
        ctx.repeatCount = 0;
        return InsertMode{};
    }
    if(c == 'I')
    {
        ed->moveToFirstNonBlank();
        ctx.repeatCount = 0;
        return InsertMode{};
    }
    if(c == 'a')
    {
        if(ctx.cursorX() < (int)ctx.lines()[ctx.cursorY()].length())
        {
            ctx.cursorX()++;
        }
        ctx.repeatCount = 0;
        return InsertMode{};
    }
    if(c == 'A')
    {
        if(ctx.cursorY() >= 0 &&
           ctx.cursorY() < (int)ctx.lines().size())
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
        ed->insertLineBelow();
        ctx.repeatCount = 0;
        return InsertMode{};
    }
    if(c == 'O')
    {
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
        for(int i = 0; i < count; i++)
            ed->deleteCharAtCursor();
        ed->needsFullRedraw = true;
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == 'X')
    {
        for(int i = 0; i < count; i++)
            ed->deleteCharBeforeCursor();
        ed->needsFullRedraw = true;
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == 'r')
    {
        int replaceChar = Terminal::readKey();
        if(replaceChar != Terminal::ESC && replaceChar >= 32)
        {
            ed->replaceCharAtCursor(static_cast<char>(replaceChar));
        }
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == 'R')
    {
        ctx.repeatCount = 0;
        return ReplaceMode{};
    }
    if(c == 's')
    {
        ed->deleteCharAtCursor();
        ctx.repeatCount = 0;
        return InsertMode{};
    }
    if(c == 'S')
    {
        ed->deleteCurrentLine();
        ed->insertLineAbove();
        ctx.repeatCount = 0;
        return InsertMode{};
    }
    if(c == 'C')
    {
        ed->deleteToEndOfLine();
        ed->saveState();
        ed->needsFullRedraw = true;
        ctx.repeatCount = 0;
        return InsertMode{};
    }
    if(c == 'D')
    {
        ed->deleteToEndOfLine();
        ed->saveState();
        ed->needsFullRedraw = true;
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == 'J')
    {
        for(int i = 0; i < count; i++)
            ed->joinLines();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == '~')
    {
        for(int i = 0; i < count; i++)
            ed->toggleCase();
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Yank/Put
    // ========================================================================

    if(c == 'p')
    {
        for(int i = 0; i < count; i++)
            ed->pasteAfter();
        ctx.repeatCount = 0;
        return std::nullopt;
    }
    if(c == 'P')
    {
        for(int i = 0; i < count; i++)
            ed->pasteBefore();
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
        ed->repeatLastChange();
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

    switch(c)
    {
    case 'f':
        ed->clangFormatWithArgs("", "clang-format: formatted file");
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
    }

    case 'e':
        // File browser / explorer
        return FileBrowserMode{"."};

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

    case 'w':
        // Save file
        ed->saveFile();
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

void ReplaceMode::on_exit(ModeContext& /* ctx */)
{
    // Restore block cursor
    Terminal::setCursorBlock();
}

std::optional<ModeState> ReplaceMode::handle(ModeContext& ctx,
                                             const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

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
