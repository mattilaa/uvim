#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"

// ============================================================================
// OperatorPendingMode Implementation
// ============================================================================

void OperatorPendingMode::on_enter(ModeContext& ctx)
{
    ctx.setStatusMessage(std::string("Operator: ") + op);
    ctx.editor->needsFullRedraw = true;
}

void OperatorPendingMode::on_exit(ModeContext& ctx)
{
    ctx.pendingOperator = 0;
    ctx.pendingAwaitingObject = false;
    ctx.pendingObjectType = 0;
    ctx.pendingCount = 0;
}

std::optional<ModeState> OperatorPendingMode::handle(ModeContext& ctx,
                                                     const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    // Escape -> cancel operator
    if(c == Terminal::ESC)
    {
        ctx.setStatusMessage("");
        return NormalMode{};
    }

    // Double operator (dd/cc/yy/>>/<< etc.) -> linewise operation
    if(!awaitingObject && c == op)
    {
        ed->handleLinewiseOperator(op, count);
        ctx.repeatCount = 0;
        ctx.commandBuffer.clear();
        if(op == 'c')
        {
            return InsertMode{};
        }
        return NormalMode{};
    }

    // 'i' or 'a' enter text object mode
    if(!awaitingObject && (c == 'i' || c == 'a'))
    {
        awaitingObject = true;
        objectType = static_cast<char>(c);
        ctx.setStatusMessage(std::string("Operator: ") + op + " " + objectType);
        return std::nullopt;
    }

    int startY, startX, endY, endX;
    bool rangeFound = false;

    if(awaitingObject)
    {
        // Expect a text-object specifier (e.g., '(', '{', '"', 'w', 'p')
        char objSpec = static_cast<char>(c);
        bool around = (objectType == 'a');
        rangeFound =
            ed->getTextObjectRange(objSpec, around, startY, startX, endY, endX);
    }
    else
    {
        // Motion-based operator
        int saveX = ctx.cursorX();
        int saveY = ctx.cursorY();
        int saveWanted = ctx.wantedX();
        int saveOffsetY = ctx.offsetY();
        int saveOffsetX = ctx.offsetX();

        bool isExclusiveMotion = false;
        bool validMotion = true;

        switch(c)
        {
        case 'w':
            ed->moveWordForward();
            isExclusiveMotion = true;
            break;
        case 'W':
            ed->moveWordForwardBig();
            isExclusiveMotion = true;
            break;
        case 'b':
            ed->moveWordBackward();
            isExclusiveMotion = true;
            break;
        case 'B':
            ed->moveWordBackwardBig();
            isExclusiveMotion = true;
            break;
        case 'e':
            ed->moveToEndOfWord();
            break;
        case 'E':
            ed->moveToEndOfWordBig();
            break;
        case '0':
            ed->moveToLineStart();
            break;
        case '^':
            ed->moveToFirstNonBlank();
            break;
        case '$':
            ed->moveToLineEnd();
            break;
        case '%':
            ed->moveToMatchingBracket();
            break;
        case 'j':
            ed->moveDown(count);
            break;
        case 'k':
            ed->moveUp(count);
            break;
        case 'G':
            if(count > 1)
                ed->moveToLine(count - 1);
            else
                ed->moveToLastLine();
            break;
        case 'g':
        {
            int nextChar = Terminal::readKey();
            if(nextChar == 'g')
            {
                ed->moveToFirstLine();
            }
            else
            {
                validMotion = false;
            }
            break;
        }
        case '{':
            ed->moveParagraphBackward();
            break;
        case '}':
            ed->moveParagraphForward();
            break;
        case 'h':
            ed->moveLeft(count);
            break;
        case 'l':
            ed->moveRight(count);
            break;
        case 'f':
        case 'F':
        case 't':
        case 'T':
        {
            int targetChar = Terminal::readKey();
            if(targetChar != Terminal::ESC)
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
            break;
        }
        default:
            validMotion = false;
            break;
        }

        if(!validMotion)
        {
            ctx.setStatusMessage("Unknown motion for operator");
            ctx.cursorX() = saveX;
            ctx.cursorY() = saveY;
            ctx.wantedX() = saveWanted;
            ctx.offsetY() = saveOffsetY;
            ctx.offsetX() = saveOffsetX;
            return NormalMode{};
        }

        // Get destination
        int destX = ctx.cursorX();
        int destY = ctx.cursorY();

        // Restore original cursor
        ctx.cursorX() = saveX;
        ctx.cursorY() = saveY;
        ctx.wantedX() = saveWanted;
        ctx.offsetY() = saveOffsetY;
        ctx.offsetX() = saveOffsetX;

        // Compute range between original and destination
        if(saveY < destY || (saveY == destY && saveX <= destX))
        {
            // Forward motion
            startY = saveY;
            startX = saveX;
            endY = destY;
            endX = destX;

            if(isExclusiveMotion)
            {
                // Make exclusive by moving end back one position
                if(endX > 0)
                {
                    endX--;
                }
                else if(endY > 0)
                {
                    endY--;
                    endX = ctx.lines()[endY].length() - 1;
                }
            }
        }
        else
        {
            // Backward motion
            startY = destY;
            startX = destX;
            endY = saveY;
            endX = saveX;
        }

        rangeFound = true;
    }

    if(!rangeFound)
    {
        ctx.setStatusMessage("No object found");
        return NormalMode{};
    }

    // Apply the operator to the range
    ed->applyOperatorToRange(op, startY, startX, endY, endX);

    ctx.repeatCount = 0;
    ctx.commandBuffer.clear();

    // 'c' operator enters insert mode after deletion
    if(op == 'c')
    {
        return InsertMode{};
    }

    return NormalMode{};
}
