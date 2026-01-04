#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include <algorithm>

// ============================================================================
// VisualMode Implementation
// ============================================================================

void VisualMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;

    // Initialize visual selection start
    ed->currentBuffer->visualStartX = ctx.cursorX();
    ed->currentBuffer->visualStartY = ctx.cursorY();
    ed->currentBuffer->visualEndX = ctx.cursorX();
    ed->currentBuffer->visualEndY = ctx.cursorY();

    ed->needsFullRedraw = true;
}

void VisualMode::on_exit(ModeContext& /* ctx */)
{
    // Nothing specific to do on exit
}

std::optional<ModeState> VisualMode::handle(ModeContext& ctx,
                                            const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    // ========================================================================
    // Leader Key (Space)
    // ========================================================================

    if(ctx.commandBuffer == " ")
    {
        if(c == 'f')
        {
            ctx.commandBuffer.clear();
            ctx.repeatCount = 0;
            ed->clangFormatVisualSelection();
            return NormalMode{};
        }
        if(c == ' ')
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

    if(c == ' ')
    {
        ctx.commandBuffer = " ";
        ctx.setStatusMessage("Leader");
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Exit Visual Mode
    // ========================================================================

    if(c == Terminal::ESC || c == 'v')
    {
        return NormalMode{};
    }

    // ========================================================================
    // Switch to Visual Line Mode
    // ========================================================================

    if(c == 'V')
    {
        return VisualLineMode{};
    }

    // ========================================================================
    // Switch to Visual Block Mode
    // ========================================================================

    if(c == Terminal::CTRL_V)
    {
        return VisualBlockMode{};
    }

    // ========================================================================
    // Movement
    // ========================================================================

    switch(c)
    {
    case 'h':
    case Terminal::ARROW_LEFT:
        ed->moveLeft(1);
        break;
    case 'j':
    case Terminal::ARROW_DOWN:
        ed->moveDown(1);
        break;
    case 'k':
    case Terminal::ARROW_UP:
        ed->moveUp(1);
        break;
    case 'l':
    case Terminal::ARROW_RIGHT:
        ed->moveRight(1);
        break;

    // Word movements
    case 'w':
        ed->moveWordForward();
        break;
    case 'W':
        ed->moveWordForwardBig();
        break;
    case 'b':
        ed->moveWordBackward();
        break;
    case 'B':
        ed->moveWordBackwardBig();
        break;
    case 'e':
        ed->moveToEndOfWord();
        break;
    case 'E':
        ed->moveToEndOfWordBig();
        break;

    // Line movements
    case '0':
        ed->moveToLineStart();
        break;
    case '^':
        ed->moveToFirstNonBlank();
        break;
    case '$':
        ed->moveToLineEnd();
        break;

    // Paragraph movements
    case '{':
        ed->moveParagraphBackward();
        break;
    case '}':
        ed->moveParagraphForward();
        break;

    // File movements
    case 'G':
        ed->moveToLastLine();
        break;
    case 'g':
    {
        int nextChar = Terminal::readKey();
        if(nextChar == 'g')
        {
            ed->moveToFirstLine();
        }
    }
    break;

    // Matching bracket
    case '%':
        ed->moveToMatchingBracket();
        break;

    // Screen movements
    case 'H':
        ed->moveToScreenTop();
        break;
    case 'M':
        ed->moveToScreenMiddle();
        break;
    case 'L':
        ed->moveToScreenBottom();
        break;

    // Scrolling
    case Terminal::CTRL_D:
        ed->scrollHalfPageDown(false);
        break;
    case Terminal::CTRL_U:
        ed->scrollHalfPageUp(false);
        break;
    case Terminal::CTRL_F:
    case Terminal::PAGE_DOWN:
        ed->scrollPageDown();
        break;
    case Terminal::CTRL_B:
    case Terminal::PAGE_UP:
        ed->scrollPageUp();
        break;

        // ========================================================================
        // Operations on Selection
        // ========================================================================

    case 'd':
    case 'x':
        ed->yankSelection();
        ed->deleteSelection();
        ed->saveState();
        return NormalMode{};

    case 'y':
        ed->yankSelection();
        return NormalMode{};

    case 'c':
        ed->yankSelection();
        ed->deleteSelection();
        ed->saveState();
        return InsertMode{};

    case '>':
        ed->indentSelection();
        ed->saveState();
        return NormalMode{};

    case '<':
        ed->dedentSelection();
        ed->saveState();
        return NormalMode{};

    case '~':
        ed->toggleCaseSelection();
        ed->saveState();
        return NormalMode{};

    case 'u':
        ed->lowercaseSelection();
        ed->saveState();
        return NormalMode{};

    case 'U':
        ed->uppercaseSelection();
        ed->saveState();
        return NormalMode{};

    case 'J':
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
    case 'o':
    {
        std::swap(ctx.cursorX(), ed->currentBuffer->visualStartX);
        std::swap(ctx.cursorY(), ed->currentBuffer->visualStartY);
    }
    break;
    }

    // Update visual end
    ed->currentBuffer->visualEndX = ctx.cursorX();
    ed->currentBuffer->visualEndY = ctx.cursorY();
    ed->needsFullRedraw = true;

    return std::nullopt;
}

// ============================================================================
// VisualLineMode Implementation
// ============================================================================

void VisualLineMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;

    // Initialize visual line selection
    ed->currentBuffer->visualStartY = ctx.cursorY();
    ed->currentBuffer->visualEndY = ctx.cursorY();

    ed->needsFullRedraw = true;
}

void VisualLineMode::on_exit(ModeContext& /* ctx */)
{
    // Nothing specific to do on exit
}

std::optional<ModeState> VisualLineMode::handle(ModeContext& ctx,
                                                const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    // ========================================================================
    // Leader Key (Space)
    // ========================================================================

    if(ctx.commandBuffer == " ")
    {
        if(c == 'f')
        {
            ctx.commandBuffer.clear();
            ctx.repeatCount = 0;
            ed->clangFormatVisualSelection();
            return NormalMode{};
        }
        if(c == ' ')
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

    if(c == ' ')
    {
        ctx.commandBuffer = " ";
        ctx.setStatusMessage("Leader");
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Exit / Switch Modes
    // ========================================================================

    if(c == Terminal::ESC || c == 'V')
    {
        return NormalMode{};
    }

    if(c == 'v')
    {
        return VisualMode{};
    }

    if(c == Terminal::CTRL_V)
    {
        return VisualBlockMode{};
    }

    // ========================================================================
    // Movement
    // ========================================================================

    switch(c)
    {
    case 'j':
    case Terminal::ARROW_DOWN:
        ed->moveDown(1);
        break;
    case 'k':
    case Terminal::ARROW_UP:
        ed->moveUp(1);
        break;
    case 'G':
        ed->moveToLastLine();
        break;
    case 'g':
    {
        int nextChar = Terminal::readKey();
        if(nextChar == 'g')
        {
            ed->moveToFirstLine();
        }
    }
    break;
    case '{':
        ed->moveParagraphBackward();
        break;
    case '}':
        ed->moveParagraphForward();
        break;
    case Terminal::CTRL_D:
        ed->scrollHalfPageDown(false);
        break;
    case Terminal::CTRL_U:
        ed->scrollHalfPageUp(false);
        break;

        // ========================================================================
        // Line Operations
        // ========================================================================

    case 'd':
    case 'x':
        ed->yankLineSelection();
        ed->deleteLineSelection();
        ed->saveState();
        return NormalMode{};

    case 'y':
        ed->yankLineSelection();
        return NormalMode{};

    case 'c':
        ed->yankLineSelection();
        ed->deleteLineSelection();
        ed->saveState();
        return InsertMode{};

    case '>':
        ed->indentLineSelection();
        ed->saveState();
        return NormalMode{};

    case '<':
        ed->dedentLineSelection();
        ed->saveState();
        return NormalMode{};

    case '=':
        ed->autoIndentLineSelection();
        ed->saveState();
        return NormalMode{};

    case 'J':
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
    case 'o':
        std::swap(ctx.cursorY(), ed->currentBuffer->visualStartY);
        break;
    }

    // Update visual end
    ed->currentBuffer->visualEndY = ctx.cursorY();
    ed->needsFullRedraw = true;

    return std::nullopt;
}

// ============================================================================
// VisualBlockMode Implementation
// ============================================================================

void VisualBlockMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;

    // Initialize visual block selection
    ed->currentBuffer->visualBlockStartX = ctx.cursorX();
    ed->currentBuffer->visualBlockStartY = ctx.cursorY();
    ed->currentBuffer->visualBlockEndX = ctx.cursorX();
    ed->currentBuffer->visualBlockEndY = ctx.cursorY();

    ed->needsFullRedraw = true;
}

void VisualBlockMode::on_exit(ModeContext& /* ctx */)
{
    // Nothing specific to do on exit
}

std::optional<ModeState> VisualBlockMode::handle(ModeContext& ctx,
                                                 const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    // ========================================================================
    // Exit
    // ========================================================================

    if(c == Terminal::ESC || c == Terminal::CTRL_V)
    {
        return NormalMode{};
    }

    // ========================================================================
    // Leader Key (Space)
    // ========================================================================

    if(ctx.commandBuffer == " ")
    {
        if(c == 'f')
        {
            ctx.commandBuffer.clear();
            ctx.repeatCount = 0;
            ed->clangFormatVisualBlockSelection();
            return NormalMode{};
        }
        if(c == ' ')
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

    if(c == ' ')
    {
        ctx.commandBuffer = " ";
        ctx.setStatusMessage("Leader");
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Movement
    // ========================================================================

    switch(c)
    {
    case 'h':
    case Terminal::ARROW_LEFT:
        ed->moveLeft(1);
        break;
    case 'j':
    case Terminal::ARROW_DOWN:
        ed->moveDown(1);
        break;
    case 'k':
    case Terminal::ARROW_UP:
        ed->moveUp(1);
        break;
    case 'l':
    case Terminal::ARROW_RIGHT:
        ed->moveRight(1);
        break;

        // ========================================================================
        // Block Operations
        // ========================================================================

    case 'd':
    case 'x':
        ed->deleteVisualBlock();
        ed->saveState();
        return NormalMode{};

    case 'y':
        ed->yankVisualBlock();
        return NormalMode{};

    case 'c':
        ed->changeVisualBlock();
        return InsertMode{};

    // Insert at block start
    case 'I':
        ed->prepareBlockInsert(false);
        return InsertMode{};

    // Append at block end
    case 'A':
        ed->prepareBlockInsert(true);
        return InsertMode{};

    // Swap corners
    case 'o':
    case 'O':
        ed->swapVisualBlockCorner();
        break;
    }

    // Update block end
    ed->currentBuffer->visualBlockEndX = ctx.cursorX();
    ed->currentBuffer->visualBlockEndY = ctx.cursorY();
    ed->needsFullRedraw = true;

    return std::nullopt;
}
