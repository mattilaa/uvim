#pragma once

#include "mode_context.h"

#include <utility>

class Editor;

namespace editor::statemachine
{
struct PopupPlacement
{
    int top = 1;
    int left = 1;
    int width = 1;
    int height = 1;
    int cursorRow = 1;
    int cursorCol = 1;
};

std::pair<int, int> editorCursorScreenPosition(const Editor& editor);

PopupPlacement placePopupNearEditorCursor(const Editor& editor, int width,
                                          int preferredHeight,
                                          int minHeight = 6,
                                          bool centerHorizontally = true);

bool moveEditorCursorForPopup(ModeContext& ctx, int c);

class PopupBase
{
public:
    void resetBackdrop() const;
    void requestBackdropRedraw(ModeContext& ctx) const;
    void drawBackdropIfNeeded(Editor& editor) const;

protected:
    mutable bool backdropDrawn = false;
    mutable int backdropRows = 0;
    mutable int backdropCols = 0;
};
} // namespace editor::statemachine
