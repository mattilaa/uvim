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
                                                     int key)
{
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    // Escape -> cancel operator
    if(c == keyCode(control::ControlKey::ESC))
    {
        ctx.setStatusMessage("");
        ed->noteDoubleEscStatusClear();
        ed->cancelChangeRecording();
        return NormalMode{};
    }

    if(ed->isRecordingChange() && !ed->isReplayingChange())
    {
        ed->recordChangeKey(c);
    }

    // Double operator (dd/cc/yy/>>/<< etc.) -> linewise operation
    if(!awaitingObject && c == op)
    {
        ed->handleLinewiseOperator(op, count);
        ctx.repeatCount = 0;
        ctx.commandBuffer.clear();
        if(op == keyCode(typed::TypedKey::KEY_C))
        {
            ed->deferChangeRecordingCommit();
            return InsertMode{};
        }
        ed->commitChangeRecording();
        return NormalMode{};
    }

    // keyCode(typed::TypedKey::KEY_I) or keyCode(typed::TypedKey::KEY_A) enter text object mode
    if(!awaitingObject && (c == keyCode(typed::TypedKey::KEY_I) || c == keyCode(typed::TypedKey::KEY_A)))
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
        // Expect a text-object specifier (e.g., keyCode(command::CommandKey::KEY_LEFT_PAREN), keyCode(command::CommandKey::KEY_LEFT_BRACE), keyCode(command::CommandKey::KEY_DOUBLE_QUOTE), keyCode(typed::TypedKey::KEY_W), keyCode(typed::TypedKey::KEY_P))
        char objSpec = static_cast<char>(c);
        bool around = (objectType == keyCode(typed::TypedKey::KEY_A));
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
        case keyCode(typed::TypedKey::KEY_W):
            ed->moveWordForward();
            isExclusiveMotion = true;
            break;
        case keyCode(typed::TypedKey::KEY_CAP_W):
            ed->moveWordForwardBig();
            isExclusiveMotion = true;
            break;
        case keyCode(typed::TypedKey::KEY_B):
            ed->moveWordBackward();
            isExclusiveMotion = true;
            break;
        case keyCode(typed::TypedKey::KEY_CAP_B):
            ed->moveWordBackwardBig();
            isExclusiveMotion = true;
            break;
        case keyCode(typed::TypedKey::KEY_E):
            ed->moveToEndOfWord();
            break;
        case keyCode(typed::TypedKey::KEY_CAP_E):
            ed->moveToEndOfWordBig();
            break;
        case keyCode(typed::TypedKey::KEY_0):
            ed->moveToLineStart();
            break;
        case keyCode(command::CommandKey::KEY_CARET):
            ed->moveToFirstNonBlank();
            break;
        case keyCode(command::CommandKey::KEY_DOLLAR):
            ed->moveToLineEnd();
            break;
        case keyCode(command::CommandKey::KEY_PERCENT):
            ed->moveToMatchingBracket();
            break;
        case keyCode(typed::TypedKey::KEY_J):
            ed->moveDown(count);
            break;
        case keyCode(typed::TypedKey::KEY_K):
            ed->moveUp(count);
            break;
        case keyCode(typed::TypedKey::KEY_CAP_G):
            if(count > 1)
                ed->moveToLine(count - 1);
            else
                ed->moveToLastLine();
            break;
        case keyCode(typed::TypedKey::KEY_G):
        {
            int nextChar = ed->isRecordingChange() ? ed->readKeyRecorded()
                                                   : Terminal::readKey();
            if(nextChar == keyCode(typed::TypedKey::KEY_G))
            {
                ed->moveToFirstLine();
            }
            else
            {
                validMotion = false;
            }
            break;
        }
        case keyCode(command::CommandKey::KEY_LEFT_BRACE):
            ed->moveParagraphBackward();
            break;
        case keyCode(command::CommandKey::KEY_RIGHT_BRACE):
            ed->moveParagraphForward();
            break;
        case keyCode(typed::TypedKey::KEY_H):
            ed->moveLeft(count);
            break;
        case keyCode(typed::TypedKey::KEY_L):
            ed->moveRight(count);
            break;
        case keyCode(typed::TypedKey::KEY_F):
        case keyCode(typed::TypedKey::KEY_CAP_F):
        case keyCode(typed::TypedKey::KEY_T):
        case keyCode(typed::TypedKey::KEY_CAP_T):
        {
            int targetChar = ed->isRecordingChange() ? ed->readKeyRecorded()
                                                     : Terminal::readKey();
            if(targetChar != keyCode(control::ControlKey::ESC))
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
            ed->cancelChangeRecording();
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
        ed->cancelChangeRecording();
        return NormalMode{};
    }

    // Apply the operator to the range
    ed->applyOperatorToRange(op, startY, startX, endY, endX);

    ctx.repeatCount = 0;
    ctx.commandBuffer.clear();

    // keyCode(typed::TypedKey::KEY_C) operator enters insert mode after deletion
    if(op == keyCode(typed::TypedKey::KEY_C))
    {
        ed->deferChangeRecordingCommit();
        return InsertMode{};
    }

    ed->commitChangeRecording();
    return NormalMode{};
}
