#include "editor_lsp_query.h"
#include "mode_state_machine.h"
#include "terminal.h"

// ============================================================================
// VisualMode Implementation
// ============================================================================

void VisualMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;

    ed->currentBuffer->visualStartX = ctx.cursorX();
    ed->currentBuffer->visualStartY = ctx.cursorY();
    ed->currentBuffer->visualEndX = ctx.cursorX();
    ed->currentBuffer->visualEndY = ctx.cursorY();

    ed->needsFullRedraw = true;
}

void VisualMode::on_exit(ModeContext& /* ctx */)
{
    // Selection is cleared when exiting visual mode
}

std::optional<ModeState> VisualMode::handle(ModeContext& ctx,
                                            const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    // Escape -> return to normal mode
    if(c == Terminal::ESC)
    {
        return NormalMode{};
    }

    // Switch to other visual modes
    if(c == 'V')
    {
        return VisualLineMode{};
    }
    if(c == Terminal::CTRL_V)
    {
        return VisualBlockMode{};
    }
    if(c == 'v')
    {
        // v in visual mode returns to normal
        return NormalMode{};
    }

    // ========================================================================
    // Movement commands (update visual selection)
    // ========================================================================

    if(c == 'h' || c == Terminal::ARROW_LEFT)
    {
        ed->moveLeft(1);
    }
    else if(c == 'j' || c == Terminal::ARROW_DOWN)
    {
        ed->moveDown(1);
    }
    else if(c == 'k' || c == Terminal::ARROW_UP)
    {
        ed->moveUp(1);
    }
    else if(c == 'l' || c == Terminal::ARROW_RIGHT)
    {
        ed->moveRight(1);
    }
    else if(c == 'w')
    {
        ed->moveWordForward();
    }
    else if(c == 'b')
    {
        ed->moveWordBackward();
    }
    else if(c == 'e')
    {
        ed->moveToEndOfWord();
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
    else if(c == 'G')
    {
        ed->moveToLastLine();
    }
    else if(c == 'g')
    {
        int nextChar = Terminal::readKey();
        if(nextChar == 'g')
        {
            ed->moveToFirstLine();
        }
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

    // Update visual end position after movement
    ed->currentBuffer->visualEndX = ctx.cursorX();
    ed->currentBuffer->visualEndY = ctx.cursorY();

    // ========================================================================
    // Operations on selection
    // ========================================================================

    if(c == 'd' || c == 'x')
    {
        ed->deleteSelection();
        ed->saveState();
        return NormalMode{};
    }
    else if(c == 'y')
    {
        ed->yankSelection();
        return NormalMode{};
    }
    else if(c == 'c' || c == 's')
    {
        ed->deleteSelection();
        ed->saveState();
        return InsertMode{};
    }
    else if(c == '>')
    {
        ed->indentSelection();
        ed->saveState();
        return NormalMode{};
    }
    else if(c == '<')
    {
        ed->dedentSelection();
        ed->saveState();
        return NormalMode{};
    }
    else if(c == '=')
    {
        ed->autoIndentSelection();
        ed->saveState();
        return NormalMode{};
    }
    else if(c == 'u')
    {
        ed->lowercaseSelection();
        ed->saveState();
        return NormalMode{};
    }
    else if(c == 'U')
    {
        ed->uppercaseSelection();
        ed->saveState();
        return NormalMode{};
    }
    else if(c == '~')
    {
        ed->toggleCaseSelection();
        ed->saveState();
        return NormalMode{};
    }
    else if(c == 'o')
    {
        // Swap cursor to other end of selection
        ed->swapVisualEnds();
    }
    else if(c == ':')
    {
        // Enter command mode with visual range
        ed->setVisualRange();
        return CommandMode{};
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}

// ============================================================================
// VisualLineMode Implementation
// ============================================================================

void VisualLineMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;

    ed->currentBuffer->visualStartX = 0;
    ed->currentBuffer->visualStartY = ctx.cursorY();
    ed->currentBuffer->visualEndX = ctx.lines()[ctx.cursorY()].length();
    ed->currentBuffer->visualEndY = ctx.cursorY();

    ed->needsFullRedraw = true;
}

void VisualLineMode::on_exit(ModeContext& /* ctx */) {}

std::optional<ModeState> VisualLineMode::handle(ModeContext& ctx,
                                                const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    // Escape -> return to normal mode
    if(c == Terminal::ESC)
    {
        return NormalMode{};
    }

    // Switch to other visual modes
    if(c == 'v')
    {
        return VisualMode{};
    }
    if(c == Terminal::CTRL_V)
    {
        return VisualBlockMode{};
    }
    if(c == 'V')
    {
        // V in visual line mode returns to normal
        return NormalMode{};
    }

    // ========================================================================
    // Line-wise movement
    // ========================================================================

    if(c == 'j' || c == Terminal::ARROW_DOWN)
    {
        ed->moveDown(1);
    }
    else if(c == 'k' || c == Terminal::ARROW_UP)
    {
        ed->moveUp(1);
    }
    else if(c == 'G')
    {
        ed->moveToLastLine();
    }
    else if(c == 'g')
    {
        int nextChar = Terminal::readKey();
        if(nextChar == 'g')
        {
            ed->moveToFirstLine();
        }
    }
    else if(c == '{')
    {
        ed->moveParagraphBackward();
    }
    else if(c == '}')
    {
        ed->moveParagraphForward();
    }

    // Update visual end (line-wise)
    ed->currentBuffer->visualEndY = ctx.cursorY();

    // ========================================================================
    // Operations on line selection
    // ========================================================================

    if(c == 'd' || c == 'x')
    {
        ed->deleteLineSelection();
        ed->saveState();
        return NormalMode{};
    }
    else if(c == 'y')
    {
        ed->yankLineSelection();
        return NormalMode{};
    }
    else if(c == 'c' || c == 'S')
    {
        ed->deleteLineSelection();
        ed->insertLineAbove();
        ed->saveState();
        return InsertMode{};
    }
    else if(c == '>')
    {
        ed->indentLineSelection();
        ed->saveState();
        return NormalMode{};
    }
    else if(c == '<')
    {
        ed->dedentLineSelection();
        ed->saveState();
        return NormalMode{};
    }
    else if(c == '=')
    {
        ed->autoIndentLineSelection();
        ed->saveState();
        return NormalMode{};
    }
    else if(c == 'o')
    {
        ed->swapVisualEnds();
    }
    else if(c == ':')
    {
        ed->setVisualRange();
        return CommandMode{};
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}

// ============================================================================
// VisualBlockMode Implementation
// ============================================================================

void VisualBlockMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;

    ed->currentBuffer->visualBlockStartX = ctx.cursorX();
    ed->currentBuffer->visualBlockStartY = ctx.cursorY();
    ed->currentBuffer->visualBlockEndX = ctx.cursorX();
    ed->currentBuffer->visualBlockEndY = ctx.cursorY();

    ed->needsFullRedraw = true;
}

void VisualBlockMode::on_exit(ModeContext& /* ctx */) {}

std::optional<ModeState> VisualBlockMode::handle(ModeContext& ctx,
                                                 const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    // Escape -> return to normal mode
    if(c == Terminal::ESC)
    {
        return NormalMode{};
    }

    // Switch to other visual modes
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
        // Ctrl+V in visual block mode returns to normal
        return NormalMode{};
    }

    // ========================================================================
    // Block movement
    // ========================================================================

    if(c == 'h' || c == Terminal::ARROW_LEFT)
    {
        ed->moveLeft(1);
    }
    else if(c == 'j' || c == Terminal::ARROW_DOWN)
    {
        ed->moveDown(1);
    }
    else if(c == 'k' || c == Terminal::ARROW_UP)
    {
        ed->moveUp(1);
    }
    else if(c == 'l' || c == Terminal::ARROW_RIGHT)
    {
        ed->moveRight(1);
    }
    else if(c == '0')
    {
        ed->moveToLineStart();
    }
    else if(c == '$')
    {
        ed->moveToLineEnd();
    }

    // Update block end position
    ed->currentBuffer->visualBlockEndX = ctx.cursorX();
    ed->currentBuffer->visualBlockEndY = ctx.cursorY();

    // ========================================================================
    // Block operations
    // ========================================================================

    if(c == 'd' || c == 'x')
    {
        ed->deleteVisualBlock();
        ed->saveState();
        return NormalMode{};
    }
    else if(c == 'y')
    {
        ed->yankVisualBlock();
        return NormalMode{};
    }
    else if(c == 'c')
    {
        ed->changeVisualBlock();
        return InsertMode{}; // Special: block insert mode
    }
    else if(c == 'I')
    {
        // Insert at beginning of block
        ed->prepareBlockInsert(true);
        return InsertMode{};
    }
    else if(c == 'A')
    {
        // Append at end of block
        ed->prepareBlockInsert(false);
        return InsertMode{};
    }
    else if(c == 'o' || c == 'O')
    {
        ed->swapVisualBlockCorner();
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}
