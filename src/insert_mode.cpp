#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"

// ============================================================================
// InsertMode Implementation
// ============================================================================

void InsertMode::on_enter(ModeContext& ctx)
{
    Terminal::setCursorBarBlinking();
    ctx.editor->needsFullRedraw = true;
}

void InsertMode::on_exit(ModeContext& ctx)
{
    // Adjust cursor position (vim behavior: cursor moves left on exit)
    if(ctx.cursorX() > 0)
    {
        ctx.cursorX()--;
    }
    ctx.editor->saveState();
    Terminal::setCursorBlock();
}

std::optional<ModeState> InsertMode::handle(ModeContext& ctx,
                                            const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    // Escape -> return to normal mode
    if(c == Terminal::ESC)
    {
        return NormalMode{};
    }

    // Ctrl+C also exits insert mode (vim behavior)
    if(c == Terminal::CTRL_C)
    {
        return NormalMode{};
    }

    // Ctrl+[ is equivalent to Escape
    if(c == 27) // ESC/Ctrl+[
    {
        return NormalMode{};
    }

    // ========================================================================
    // Text insertion and editing
    // ========================================================================

    if(c == Terminal::BACKSPACE || c == 127 || c == Terminal::CTRL_H)
    {
        ed->handleBackspace();
    }
    else if(c == Terminal::DELETE_KEY)
    {
        ed->deleteCharAtCursor();
    }
    else if(c == Terminal::ENTER)
    {
        ed->insertNewline();
    }
    else if(c == Terminal::TAB)
    {
        // Check if we should do completion or insert spaces
        if(ed->shouldTriggerCompletion())
        {
            ed->triggerCompletion();
        }
        else
        {
            ed->insertTab();
        }
    }

    // ========================================================================
    // Cursor movement in insert mode
    // ========================================================================

    else if(c == Terminal::ARROW_LEFT)
    {
        ed->moveLeft(1);
    }
    else if(c == Terminal::ARROW_RIGHT)
    {
        ed->moveRight(1);
    }
    else if(c == Terminal::ARROW_UP)
    {
        ed->moveUp(1);
    }
    else if(c == Terminal::ARROW_DOWN)
    {
        ed->moveDown(1);
    }
    else if(c == Terminal::HOME)
    {
        ed->moveToLineStart();
    }
    else if(c == Terminal::END)
    {
        ed->moveToLineEnd();
    }
    else if(c == Terminal::PAGE_UP)
    {
        ed->scrollPageUp();
    }
    else if(c == Terminal::PAGE_DOWN)
    {
        ed->scrollPageDown();
    }

    // ========================================================================
    // Special insert mode commands
    // ========================================================================

    else if(c == Terminal::CTRL_W)
    {
        // Delete word before cursor
        ed->deleteWordBackward();
    }
    else if(c == Terminal::CTRL_U)
    {
        // Delete to beginning of line
        ed->deleteToLineStart();
    }
    else if(c == Terminal::CTRL_T)
    {
        // Indent current line
        ed->indentCurrentLine();
    }
    else if(c == Terminal::CTRL_D)
    {
        // Dedent current line
        ed->dedentCurrentLine();
    }
    else if(c == Terminal::CTRL_N)
    {
        // Next completion
        ed->nextCompletion();
    }
    else if(c == Terminal::CTRL_P)
    {
        // Previous completion
        ed->previousCompletion();
    }
    else if(c == Terminal::CTRL_O)
    {
        // Execute one normal mode command then return to insert
        // This is a simplified implementation - full vim does more
        int nextKey = Terminal::readKey();
        ed->executeOneNormalCommand(nextKey);
    }

    // ========================================================================
    // Regular character insertion
    // ========================================================================

    else if(c >= 32 && c < 127)
    {
        // Printable ASCII
        ed->insertChar(static_cast<char>(c));
    }
    else if(c >= 128)
    {
        // UTF-8 multi-byte character - handle appropriately
        ed->insertUtf8Char(c);
    }

    ctx.dirty() = true;
    return std::nullopt;
}
