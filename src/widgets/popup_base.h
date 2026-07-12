#pragma once

#include "mode_context.h"

#include <utility>

class Editor;
class Theme;

namespace widgets
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

struct PopupFrameView
{
    const Theme& theme;
    int screenRows = 0;
    int screenCols = 0;
};

std::pair<int, int> editorCursorScreenPosition(const Editor& editor);

PopupPlacement placePopupNearEditorCursor(const Editor& editor, int width,
                                          int preferredHeight,
                                          int minHeight = 6,
                                          bool centerHorizontally = true);

PopupPlacement placeBottomLeftPopup(int screenRows, int screenCols, int width,
                                    int height, int preferredLeft = 2);

int visibleRowsForCursorPopup(editor::statemachine::ModeContext& ctx,
                              int width, int preferredHeight,
                              int chromeRows);

bool moveEditorCursorForPopup(editor::statemachine::ModeContext& ctx, int c);

class PopupBase
{
public:
    void resetBackdrop();
    void requestBackdropRedraw(editor::statemachine::ModeContext& ctx);
    void drawBackdropIfNeeded(Editor& editor);

protected:
    bool backdropDrawn = false;
    int backdropRows = 0;
    int backdropCols = 0;
};
} // namespace widgets
