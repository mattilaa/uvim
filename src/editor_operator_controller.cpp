#include "editor_operator_controller.h"
#include "constants.h"
#include "editor.h"
#include "key_enums.h"

#include <algorithm>
#include <cctype>
#include <string>

EditorOperatorController::EditorOperatorController(Editor& editor)
    : editor(editor)
{
}

void EditorOperatorController::enterOperatorPending(char op)
{
    editor.enterOperatorPendingImpl(op);
}

bool EditorOperatorController::getTextObjectRange(char objChar, bool around,
                                                  int& outStartY,
                                                  int& outStartX, int& outEndY,
                                                  int& outEndX)
{
    return editor.getTextObjectRangeImpl(objChar, around, outStartY, outStartX,
                                         outEndY, outEndX);
}

void EditorOperatorController::applyOperatorToRange(char op, int startY,
                                                    int startX, int endY,
                                                    int endX)
{
    editor.applyOperatorToRangeImpl(op, startY, startX, endY, endX);
}

void Editor::enterOperatorPendingImpl(char op)
{
    pendingOperator = op;
    pendingAwaitingObject = false;
    pendingObjectType = 0;
    pendingCount = std::max(1, repeatCount);
    commandBuffer.clear();
    setStatusMessage(std::string("Operator: ") + op);
    setMode(OP_PENDING);
}

bool Editor::getTextObjectRangeImpl(char objChar, bool around, int& outStartY,
                                    int& outStartX, int& outEndY, int& outEndX)
{
    int y = *cursorY;
    int x = *cursorX;

    auto findEnclosing = [&](char openc, char closec) -> bool
    {
        int ly = y, lx = x;
        for(;;)
        {
            const std::string& line = (*lines)[ly];
            for(int i = lx; i >= 0; --i)
            {
                if(line[i] == openc)
                {
                    int depth = 0;
                    int ty = ly;
                    int tx = i;
                    for(;;)
                    {
                        tx++;
                        while(ty < static_cast<int>(lines->size()) &&
                              tx >= static_cast<int>((*lines)[ty].length()))
                        {
                            ty++;
                            tx = 0;
                            if(ty >= static_cast<int>(lines->size()))
                                break;
                        }
                        if(ty >= static_cast<int>(lines->size()))
                            break;
                        char ch = (*lines)[ty][tx];
                        if(ch == openc)
                            depth++;
                        else if(ch == closec)
                        {
                            if(depth == 0)
                            {
                                outStartY = ly;
                                outStartX = i;
                                outEndY = ty;
                                outEndX = tx;
                                if(!around)
                                {
                                    if(outStartX + 1 <
                                       static_cast<int>(
                                           (*lines)[outStartY].length()))
                                    {
                                        outStartX = outStartX + 1;
                                    }
                                    else
                                    {
                                        outStartY++;
                                        outStartX = 0;
                                    }

                                    if(outEndX - 1 >= 0)
                                    {
                                        outEndX = outEndX - 1;
                                    }
                                    else
                                    {
                                        outEndY--;
                                        outEndX =
                                            (*lines)[outEndY].length() - 1;
                                    }
                                }
                                return true;
                            }
                            depth--;
                        }
                    }
                }
            }
            if(ly == 0)
                break;
            ly--;
            if(ly >= 0)
                lx = (*lines)[ly].length() - 1;
        }
        return false;
    };

    if(objChar == keyCode(command::CommandKey::KEY_LEFT_PAREN) ||
       objChar == keyCode(command::CommandKey::KEY_RIGHT_PAREN))
    {
        if(findEnclosing(keyCode(command::CommandKey::KEY_LEFT_PAREN),
                         keyCode(command::CommandKey::KEY_RIGHT_PAREN)))
            return true;
    }
    if(objChar == keyCode(command::CommandKey::KEY_LEFT_BRACE) ||
       objChar == keyCode(command::CommandKey::KEY_RIGHT_BRACE))
    {
        if(findEnclosing(keyCode(command::CommandKey::KEY_LEFT_BRACE),
                         keyCode(command::CommandKey::KEY_RIGHT_BRACE)))
            return true;
    }
    if(objChar == keyCode(command::CommandKey::KEY_LEFT_BRACKET) ||
       objChar == keyCode(command::CommandKey::KEY_RIGHT_BRACKET))
    {
        if(findEnclosing(keyCode(command::CommandKey::KEY_LEFT_BRACKET),
                         keyCode(command::CommandKey::KEY_RIGHT_BRACKET)))
            return true;
    }

    if(objChar == keyCode(command::CommandKey::KEY_DOUBLE_QUOTE) ||
       objChar == keyCode(command::CommandKey::KEY_APOSTROPHE))
    {
        const std::string& line = (*lines)[y];
        int lpos = -1;
        int rpos = -1;
        for(int i = x; i >= 0; --i)
        {
            if(line[i] == objChar)
            {
                lpos = i;
                break;
            }
        }
        for(int i = x; i < static_cast<int>(line.length()); ++i)
        {
            if(line[i] == objChar)
            {
                rpos = i;
                break;
            }
        }

        if(lpos >= 0 && rpos >= 0 && lpos < rpos)
        {
            outStartY = y;
            outEndY = y;
            if(around)
            {
                outStartX = lpos;
                outEndX = rpos;
            }
            else
            {
                outStartX = lpos + 1;
                outEndX = rpos - 1;
            }
            return true;
        }
    }

    if(objChar == keyCode(typed::TypedKey::KEY_W) ||
       objChar == keyCode(typed::TypedKey::KEY_CAP_W))
    {
        // Vim word objects: the run of same-class characters under the
        // cursor. `w` classes are word chars / punctuation / whitespace;
        // `W` classes are non-blank / whitespace. `a` adds the trailing
        // whitespace (or the leading one when there is none after).
        const std::string& line = (*lines)[y];
        const int len = static_cast<int>(line.length());
        if(len == 0)
            return false;
        const bool bigWord = objChar == keyCode(typed::TypedKey::KEY_CAP_W);
        auto isBlank = [](char ch) { return ch == ' ' || ch == '\t'; };
        // Text-object words are identifier-like (letters, digits, '_', and
        // UTF-8 bytes); isWordChar() counts punctuation for w/dw motions.
        auto isKeywordChar = [](char ch)
        {
            const unsigned char u = static_cast<unsigned char>(ch);
            return u >= 0x80 || std::isalnum(u) || ch == '_';
        };
        auto charClass = [&](char ch)
        {
            if(isBlank(ch))
                return 0;
            if(bigWord)
                return 1;
            return isKeywordChar(ch) ? 1 : 2;
        };

        const int at = std::clamp(x, 0, len - 1);
        const int cls = charClass(line[at]);
        int L = at;
        int R = at;
        while(L > 0 && charClass(line[L - 1]) == cls)
            L--;
        while(R + 1 < len && charClass(line[R + 1]) == cls)
            R++;

        if(around && cls != 0)
        {
            if(R + 1 < len && isBlank(line[R + 1]))
            {
                while(R + 1 < len && isBlank(line[R + 1]))
                    R++;
            }
            else
            {
                while(L > 0 && isBlank(line[L - 1]))
                    L--;
            }
        }
        outStartY = y;
        outEndY = y;
        outStartX = L;
        outEndX = R;
        return true;
    }

    if(objChar == keyCode(typed::TypedKey::KEY_P))
    {
        int sy = y;
        int ey = y;
        while(sy > 0 && !(*lines)[sy].empty())
            sy--;
        if((*lines)[sy].empty() && sy < y)
            sy++;
        while(ey < static_cast<int>(lines->size()) - 1 && !(*lines)[ey].empty())
            ey++;
        if((*lines)[ey].empty() && ey > y)
            ey--;
        outStartY = sy;
        outEndY = ey;
        outStartX = 0;
        outEndX = (*lines)[outEndY].length() - 1;
        return true;
    }

    return false;
}

void Editor::applyOperatorToRangeImpl(char op, int startY, int startX, int endY,
                                      int endX)
{
    if(startY > endY || (startY == endY && startX > endX))
    {
        std::swap(startY, endY);
        std::swap(startX, endX);
    }

    if(op == keyCode(typed::TypedKey::KEY_Y) ||
       op == keyCode(typed::TypedKey::KEY_D) ||
       op == keyCode(typed::TypedKey::KEY_C))
    {
        yankRange(startY, startX, endY, endX);
    }

    if(op == keyCode(typed::TypedKey::KEY_D) ||
       op == keyCode(typed::TypedKey::KEY_C))
    {
        deleteRange(startY, startX, endY, endX);
        saveState();
    }

    if(op == keyCode(command::CommandKey::KEY_EQUAL))
    {
        autoIndentRange(startY, endY);

        int linesIndented = endY - startY + 1;
        setStatusMessage(std::to_string(linesIndented) + " line" +
                         (linesIndented > 1 ? "s" : "") + " indented");
        saveState();
    }

    if(op == keyCode(typed::TypedKey::KEY_C))
    {
        *cursorY = startY;
        *cursorX = startX;
    }
    else if(op != keyCode(command::CommandKey::KEY_EQUAL))
    {
        *cursorY = startY;
        *cursorX = startX;
    }

    needsFullRedraw = true;
    *dirty = true;
}
