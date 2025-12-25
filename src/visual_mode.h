#pragma once

#include "editor_context.h"

class TextOperations;

class VisualMode
{
public:
    VisualMode(EditorContext& ctx, TextOperations& textOps);

    void startVisualMode();
    void startVisualLineMode();
    void startVisualBlockMode();

    void updateVisualSelection();
    void updateVisualBlockSelection();

    bool isInSelection(int row, int col) const;
    bool isInVisualBlock(int row, int col) const;

    void getSelectionBounds(int& startY, int& startX, int& endY,
                            int& endX) const;
    void getVisualBlockBounds(int& startY, int& startX, int& endY,
                              int& endX) const;

    // Visual block operations
    void deleteVisualBlock();
    void yankVisualBlock();
    void changeVisualBlock();
    void applyVisualBlockInsert();

private:
    EditorContext& ctx;
    TextOperations& textOps;
};
