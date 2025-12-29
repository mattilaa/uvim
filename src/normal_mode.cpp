#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"

// ============================================================================
// NormalMode Implementation
// ============================================================================

void NormalMode::on_enter(ModeContext& ctx)
{
    ctx.commandBuffer.clear();
    ctx.repeatCount = 0;
    ctx.pendingOperator = 0;
    ctx.pendingAwaitingObject = false;
    ctx.pendingObjectType = 0;
    ctx.pendingCount = 0;

    Terminal::setCursorBlock();
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

    // Escape always stays in normal mode, clears any pending state
    if(c == Terminal::ESC)
    {
        ctx.commandBuffer.clear();
        ctx.repeatCount = 0;
        ctx.setStatusMessage("");
        return std::nullopt;
    }

    // Build repeat count from digits (except 0 at start which is line-start)
    if(c >= '1' && c <= '9')
    {
        ctx.repeatCount = ctx.repeatCount * 10 + (c - '0');
        ctx.commandBuffer += static_cast<char>(c);
        return std::nullopt;
    }
    if(c == '0' && ctx.repeatCount > 0)
    {
        ctx.repeatCount = ctx.repeatCount * 10;
        ctx.commandBuffer += '0';
        return std::nullopt;
    }

    int count = std::max(1, ctx.repeatCount);

    // ========================================================================
    // Mode transitions
    // ========================================================================

    // Insert mode
    if(c == 'i')
    {
        return InsertMode{};
    }
    if(c == 'I')
    {
        ed->moveToFirstNonBlank();
        return InsertMode{};
    }
    if(c == 'a')
    {
        if(!ctx.lines().empty() && !ctx.lines()[ctx.cursorY()].empty())
        {
            ctx.cursorX()++;
            if(ctx.cursorX() >
               static_cast<int>(ctx.lines()[ctx.cursorY()].length()))
                ctx.cursorX() = ctx.lines()[ctx.cursorY()].length();
        }
        return InsertMode{};
    }
    if(c == 'A')
    {
        ed->moveToLineEnd();
        ctx.cursorX()++;
        return InsertMode{};
    }
    if(c == 'o')
    {
        ed->insertLineBelow();
        return InsertMode{};
    }
    if(c == 'O')
    {
        ed->insertLineAbove();
        return InsertMode{};
    }
    if(c == 's')
    {
        // Substitute: delete char and enter insert
        if(!ctx.lines().empty() && !ctx.lines()[ctx.cursorY()].empty())
        {
            ed->deleteCharAtCursor();
            ed->saveState();
        }
        return InsertMode{};
    }
    if(c == 'S')
    {
        // Substitute line
        ed->deleteCurrentLine();
        ed->insertLineAbove();
        return InsertMode{};
    }
    if(c == 'C')
    {
        // Change to end of line
        ed->deleteToEndOfLine();
        ed->saveState();
        return InsertMode{};
    }

    // Visual modes
    if(c == 'v')
    {
        return VisualMode{};
    }
    if(c == 'V')
    {
        return VisualLineMode{};
    }
    if(c == Terminal::CTRL_V)
    {
        return VisualBlockMode{};
    }

    // Command mode
    if(c == ':')
    {
        return CommandMode{};
    }

    // Search modes
    if(c == '/')
    {
        return SearchForwardMode{};
    }
    if(c == '?')
    {
        return SearchBackwardMode{};
    }

    // ========================================================================
    // Operator pending (d, c, y, =, etc.)
    // ========================================================================

    if(c == 'd' || c == 'c' || c == 'y' || c == '=' || c == '>')
    {
        // Check for double-tap (dd, cc, yy, ==, >>)
        int nextChar = Terminal::readKeyTimeout(200);
        if(nextChar == c)
        {
            // Line-wise operation
            ed->handleLinewiseOperator(static_cast<char>(c), count);
            ctx.repeatCount = 0;
            ctx.commandBuffer.clear();
            if(c == 'c')
            {
                return InsertMode{};
            }
            return std::nullopt;
        }
        else if(nextChar != -1)
        {
            // Put the key back for operator-pending mode to process
            Terminal::unreadKey(nextChar);
        }

        return OperatorPendingMode{static_cast<char>(c), count};
    }

    if(c == '<')
    {
        int nextChar = Terminal::readKeyTimeout(200);
        if(nextChar == '<')
        {
            ed->handleLinewiseOperator('<', count);
            ctx.repeatCount = 0;
            ctx.commandBuffer.clear();
            return std::nullopt;
        }
        else if(nextChar != -1)
        {
            Terminal::unreadKey(nextChar);
        }
        return OperatorPendingMode{'<', count};
    }

    // ========================================================================
    // Movement commands
    // ========================================================================

    if(c == 'h' || c == Terminal::ARROW_LEFT)
    {
        ed->moveLeft(count);
    }
    else if(c == 'j' || c == Terminal::ARROW_DOWN)
    {
        ed->moveDown(count);
    }
    else if(c == 'k' || c == Terminal::ARROW_UP)
    {
        ed->moveUp(count);
    }
    else if(c == 'l' || c == Terminal::ARROW_RIGHT)
    {
        ed->moveRight(count);
    }
    else if(c == 'w')
    {
        for(int i = 0; i < count; i++)
            ed->moveWordForward();
    }
    else if(c == 'W')
    {
        for(int i = 0; i < count; i++)
            ed->moveWordForwardBig();
    }
    else if(c == 'b')
    {
        for(int i = 0; i < count; i++)
            ed->moveWordBackward();
    }
    else if(c == 'B')
    {
        for(int i = 0; i < count; i++)
            ed->moveWordBackwardBig();
    }
    else if(c == 'e')
    {
        for(int i = 0; i < count; i++)
            ed->moveToEndOfWord();
    }
    else if(c == 'E')
    {
        for(int i = 0; i < count; i++)
            ed->moveToEndOfWordBig();
    }
    else if(c == '0')
    {
        ed->moveToLineStart();
    }
    else if(c == '^')
    {
        ed->moveToFirstNonBlank();
    }
    else if(c == '$')
    {
        ed->moveToLineEnd();
    }
    else if(c == '%')
    {
        ed->moveToMatchingBracket();
    }
    else if(c == '{')
    {
        ed->moveParagraphBackward();
    }
    else if(c == '}')
    {
        ed->moveParagraphForward();
    }
    else if(c == 'G')
    {
        if(ctx.repeatCount > 0)
            ed->moveToLine(ctx.repeatCount - 1);
        else
            ed->moveToLastLine();
    }
    else if(c == 'g')
    {
        int nextChar = Terminal::readKey();
        if(nextChar == 'g')
        {
            if(ctx.repeatCount > 0)
                ed->moveToLine(ctx.repeatCount - 1);
            else
                ed->moveToFirstLine();
        }
        else if(nextChar == 'd')
        {
            ed->goToDefinition();
        }
        else if(nextChar == 'f')
        {
            ed->goToFile();
        }
        else if(nextChar == 'a')
        {
            ed->switchToAlternateFile();
        }
    }
    else if(c == Terminal::CTRL_O)
    {
        ed->jumpBack();
    }
    else if(c == Terminal::CTRL_I)
    {
        ed->jumpForward();
    }
    else if(c == Terminal::CTRL_D)
    {
        ed->scrollHalfPageDown();
    }
    else if(c == Terminal::CTRL_U)
    {
        ed->scrollHalfPageUp();
    }
    else if(c == Terminal::CTRL_F || c == Terminal::PAGE_DOWN)
    {
        ed->scrollPageDown();
    }
    else if(c == Terminal::CTRL_B || c == Terminal::PAGE_UP)
    {
        ed->scrollPageUp();
    }
    else if(c == 'H')
    {
        ed->moveToScreenTop();
    }
    else if(c == 'M')
    {
        ed->moveToScreenMiddle();
    }
    else if(c == 'L')
    {
        ed->moveToScreenBottom();
    }
    else if(c == 'z')
    {
        int nextChar = Terminal::readKey();
        if(nextChar == 'z')
            ed->centerScreen();
        else if(nextChar == 't')
            ed->scrollToTop();
        else if(nextChar == 'b')
            ed->scrollToBottom();
    }

    // ========================================================================
    // Editing commands (single-key)
    // ========================================================================

    else if(c == 'x')
    {
        for(int i = 0; i < count; i++)
        {
            ed->deleteCharAtCursor();
        }
        ed->saveState();
    }
    else if(c == 'X')
    {
        for(int i = 0; i < count; i++)
        {
            ed->deleteCharBeforeCursor();
        }
        ed->saveState();
    }
    else if(c == 'r')
    {
        int replaceChar = Terminal::readKey();
        if(replaceChar != Terminal::ESC)
        {
            ed->replaceCharAtCursor(static_cast<char>(replaceChar));
            ed->saveState();
        }
    }
    else if(c == 'J')
    {
        ed->joinLines();
        ed->saveState();
    }
    else if(c == 'p')
    {
        ed->pasteAfter();
        ed->saveState();
    }
    else if(c == 'P')
    {
        ed->pasteBefore();
        ed->saveState();
    }
    else if(c == 'u')
    {
        ed->undo();
    }
    else if(c == Terminal::CTRL_R)
    {
        ed->redo();
    }
    else if(c == '.')
    {
        ed->repeatLastChange();
    }
    else if(c == '~')
    {
        ed->toggleCase();
        ed->saveState();
    }

    // ========================================================================
    // Search commands
    // ========================================================================

    else if(c == 'n')
    {
        ed->searchNext();
    }
    else if(c == 'N')
    {
        ed->searchPrevious();
    }
    else if(c == '*')
    {
        ed->searchWordUnderCursor(true);
    }
    else if(c == '#')
    {
        ed->searchWordUnderCursor(false);
    }

    // ========================================================================
    // Marks and jumps
    // ========================================================================

    else if(c == 'm')
    {
        int mark = Terminal::readKey();
        if((mark >= 'a' && mark <= 'z') || (mark >= 'A' && mark <= 'Z'))
        {
            ed->setMark(static_cast<char>(mark));
        }
    }
    else if(c == '\'' || c == '`')
    {
        int mark = Terminal::readKey();
        if((mark >= 'a' && mark <= 'z') || (mark >= 'A' && mark <= 'Z'))
        {
            ed->jumpToMark(static_cast<char>(mark));
        }
    }

    // ========================================================================
    // Leader key sequences (Space as leader)
    // ========================================================================

    else if(c == ' ')
    {
        int nextChar = Terminal::readKey();

        if(nextChar == 'f')
        {
            // <leader>f - Fuzzy find files
            return FuzzyFindMode{};
        }
        else if(nextChar == 'b')
        {
            // <leader>b - Buffer browser
            return BufferBrowserMode{};
        }
        else if(nextChar == 'g')
        {
            // <leader>g - Grep search
            return GrepSearchMode{};
        }
        else if(nextChar == 'e')
        {
            // <leader>e - File explorer
            return FileBrowserMode{};
        }
        else if(nextChar == 'w')
        {
            // <leader>w - Save file
            ed->saveFile();
        }
        else if(nextChar == 'q')
        {
            // <leader>q - Quit
            ed->executeCommand("q");
        }
    }

    // ========================================================================
    // Misc
    // ========================================================================

    else if(c == Terminal::CTRL_G)
    {
        ed->showFileInfo();
    }
    else if(c == Terminal::CTRL_L)
    {
        ed->forceFullRedraw();
    }
    else if(c == Terminal::CTRL_P)
    {
        // Ctrl+P - Fuzzy find files
        return FuzzyFindMode{};
    }
    else if(c == Terminal::CTRL_S)
    {
        // Ctrl+S - Grep search (ripgrep)
        return GrepSearchMode{};
    }
    else if(c == Terminal::CTRL_W)
    {
        // Ctrl+W - Buffer browser
        return BufferBrowserMode{};
    }

    // Reset count after command
    ctx.repeatCount = 0;
    ctx.commandBuffer.clear();

    return std::nullopt;
}
