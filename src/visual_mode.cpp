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
    // Leader Key (Space)
    // ========================================================================

    if(ctx.commandBuffer == " ")
    {
        if(c == 'f')
        {
            ctx.commandBuffer.clear();
            ctx.repeatCount = 0;
            if(ed->isPythonFile())
            {
                ed->pythonFormatBuffer();
            }
            else if(ed->isRobotFile())
            {
                ed->robotFormatBuffer();
            }
            else if(ed->isJsonFile())
            {
                ed->jsonFormatBuffer();
            }
            else if(ed->isYamlFile())
            {
                ed->yamlFormatBuffer();
            }
            else
            {
                ed->clangFormatVisualSelection();
            }
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

    bool didMove = false;
    switch(c)
    {
    case 'h':
    case Terminal::ARROW_LEFT:
        ed->moveLeft(count);
        didMove = true;
        break;
    case 'j':
    case Terminal::ARROW_DOWN:
        ed->moveDown(count);
        didMove = true;
        break;
    case 'k':
    case Terminal::ARROW_UP:
        ed->moveUp(count);
        didMove = true;
        break;
    case 'l':
    case Terminal::ARROW_RIGHT:
        ed->moveRight(count);
        didMove = true;
        break;

    // Word movements
    case 'w':
        for(int i = 0; i < count; i++)
            ed->moveWordForward();
        didMove = true;
        break;
    case 'W':
        for(int i = 0; i < count; i++)
            ed->moveWordForwardBig();
        didMove = true;
        break;
    case 'b':
        for(int i = 0; i < count; i++)
            ed->moveWordBackward();
        didMove = true;
        break;
    case 'B':
        for(int i = 0; i < count; i++)
            ed->moveWordBackwardBig();
        didMove = true;
        break;
    case 'e':
        for(int i = 0; i < count; i++)
            ed->moveToEndOfWord();
        didMove = true;
        break;
    case 'E':
        for(int i = 0; i < count; i++)
            ed->moveToEndOfWordBig();
        didMove = true;
        break;

    // Line movements
    case '0':
        ed->moveToLineStart();
        didMove = true;
        break;
    case '^':
        ed->moveToFirstNonBlank();
        didMove = true;
        break;
    case '$':
        ed->moveToLineEnd();
        didMove = true;
        break;

    // Paragraph movements
    case '{':
        for(int i = 0; i < count; i++)
            ed->moveParagraphBackward();
        didMove = true;
        break;
    case '}':
        for(int i = 0; i < count; i++)
            ed->moveParagraphForward();
        didMove = true;
        break;

    // File movements
    case 'G':
        ed->moveToLastLine();
        didMove = true;
        break;
    case 'g':
    {
        int nextChar = Terminal::readKey();
        if(nextChar == 'g')
        {
            ed->moveToFirstLine();
            didMove = true;
        }
    }
    break;
    // Screen movements
    case 'H':
        ed->moveToScreenTop();
        didMove = true;
        break;
    case 'M':
        ed->moveToScreenMiddle();
        didMove = true;
        break;
    case 'L':
        ed->moveToScreenBottom();
        didMove = true;
        break;

    // Scrolling
    case Terminal::CTRL_D:
        ed->scrollHalfPageDown(false);
        didMove = true;
        break;
    case Terminal::CTRL_U:
        ed->scrollHalfPageUp(false);
        didMove = true;
        break;
    case Terminal::CTRL_F:
    case Terminal::PAGE_DOWN:
        ed->scrollPageDown();
        didMove = true;
        break;
    case Terminal::CTRL_B:
    case Terminal::PAGE_UP:
        ed->scrollPageUp();
        didMove = true;
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
    // Leader Key (Space)
    // ========================================================================

    if(ctx.commandBuffer == " ")
    {
        if(c == 'f')
        {
            ctx.commandBuffer.clear();
            ctx.repeatCount = 0;
            if(ed->isPythonFile())
            {
                ed->pythonFormatBuffer();
            }
            else if(ed->isRobotFile())
            {
                ed->robotFormatBuffer();
            }
            else if(ed->isJsonFile())
            {
                ed->jsonFormatBuffer();
            }
            else if(ed->isYamlFile())
            {
                ed->yamlFormatBuffer();
            }
            else
            {
                ed->clangFormatVisualSelection();
            }
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

    bool didMove = false;
    switch(c)
    {
    case 'j':
    case Terminal::ARROW_DOWN:
        ed->moveDown(count);
        didMove = true;
        break;
    case 'k':
    case Terminal::ARROW_UP:
        ed->moveUp(count);
        didMove = true;
        break;
    case 'G':
        ed->moveToLastLine();
        didMove = true;
        break;
    case 'g':
    {
        int nextChar = Terminal::readKey();
        if(nextChar == 'g')
        {
            ed->moveToFirstLine();
            didMove = true;
        }
    }
    break;
    case '{':
        for(int i = 0; i < count; i++)
            ed->moveParagraphBackward();
        didMove = true;
        break;
    case '}':
        for(int i = 0; i < count; i++)
            ed->moveParagraphForward();
        didMove = true;
        break;
    case Terminal::CTRL_D:
        ed->scrollHalfPageDown(false);
        didMove = true;
        break;
    case Terminal::CTRL_U:
        ed->scrollHalfPageUp(false);
        didMove = true;
        break;
    case '%':
        ed->moveToMatchingBracket();
        didMove = true;
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
            if(ed->isPythonFile())
            {
                ed->pythonFormatBuffer();
            }
            else if(ed->isRobotFile())
            {
                ed->robotFormatBuffer();
            }
            else if(ed->isJsonFile())
            {
                ed->jsonFormatBuffer();
            }
            else if(ed->isYamlFile())
            {
                ed->yamlFormatBuffer();
            }
            else
            {
                ed->clangFormatVisualBlockSelection();
            }
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

    bool didMove = false;
    switch(c)
    {
    case 'h':
    case Terminal::ARROW_LEFT:
        ed->moveLeft(count);
        didMove = true;
        break;
    case 'j':
    case Terminal::ARROW_DOWN:
        ed->moveDown(count);
        didMove = true;
        break;
    case 'k':
    case Terminal::ARROW_UP:
        ed->moveUp(count);
        didMove = true;
        break;
    case 'l':
    case Terminal::ARROW_RIGHT:
        ed->moveRight(count);
        didMove = true;
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
        didMove = true;
        break;
    }

    if(didMove)
        ctx.repeatCount = 0;

    // Update block end
    ed->currentBuffer->visualBlockEndX = ctx.cursorX();
    ed->currentBuffer->visualBlockEndY = ctx.cursorY();
    ed->needsFullRedraw = true;

    return std::nullopt;
}
