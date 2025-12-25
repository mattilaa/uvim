#pragma once

#include "editor_context.h"

class TextOperations;
class CursorMovement;

class OperatorPending
{
public:
    OperatorPending(EditorContext& ctx, TextOperations& textOps,
                    CursorMovement& cursor);

    void enter(char op);
    void handleKey(int c);
    void cancel();

    // Text object ranges
    bool getTextObjectRange(char objChar, bool around, int& outStartY,
                            int& outStartX, int& outEndY, int& outEndX);

private:
    EditorContext& ctx;
    TextOperations& textOps;
    CursorMovement& cursor;

    void applyOperatorToRange(char op, int startY, int startX, int endY,
                              int endX);

    // Find matching brackets
    bool findMatchingPair(char openChar, char closeChar, bool around,
                          int& startY, int& startX, int& endY, int& endX);

    // Find surrounding quotes
    bool findSurroundingQuotes(char quoteChar, bool around, int& startY,
                               int& startX, int& endY, int& endX);

    // Find word boundaries
    bool findWordBoundaries(bool around, int& startY, int& startX, int& endY,
                            int& endX);

    // Find WORD boundaries (space-separated)
    bool findWORDBoundaries(bool around, int& startY, int& startX, int& endY,
                            int& endX);
};
