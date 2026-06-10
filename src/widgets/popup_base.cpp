#include "widgets/popup_base.h"

#include "editor.h"
#include "key_enums.h"
#include "terminal.h"
#include "text_utils.h"

#include <algorithm>
#include <string_view>

namespace widgets
{
std::pair<int, int> editorCursorScreenPosition(const Editor& editor)
{
    Editor::PaneLayout layout = editor.getPaneLayout(editor.activePane);
    int row = layout.y + (*editor.cursorY - *editor.offsetY) + 1 +
              editor.tabBarRows();
    int col = layout.x + (*editor.cursorX - *editor.offsetX) + 1 +
              editor.gutterWidth();
    if(editor.utf8Mode && *editor.cursorY >= 0 &&
       *editor.cursorY < (int)editor.lines->size())
    {
        const std::string& line = (*editor.lines)[*editor.cursorY];
        int start = std::clamp(*editor.offsetX, 0, (int)line.size());
        int end = std::clamp(*editor.cursorX, 0, (int)line.size());
        if(end < start)
            std::swap(start, end);
        col = text_utils::utf8DisplayWidth(
                  std::string_view(line).substr(start, end - start)) +
              1 + editor.gutterWidth() + layout.x;
    }
    return {std::clamp(row, 1, editor.screenRows),
            std::clamp(col, 1, editor.screenCols)};
}

PopupPlacement placePopupNearEditorCursor(const Editor& editor, int width,
                                          int preferredHeight, int minHeight,
                                          bool centerHorizontally)
{
    PopupPlacement placement;
    placement.width = std::clamp(width, 1, std::max(1, editor.screenCols));
    placement.height =
        std::clamp(preferredHeight, 1, std::max(1, editor.screenRows));

    const auto [cursorRow, cursorCol] = editorCursorScreenPosition(editor);
    placement.cursorRow = cursorRow;
    placement.cursorCol = cursorCol;

    placement.left = centerHorizontally
                         ? std::max(1, (editor.screenCols - placement.width) /
                                           2 +
                                           1)
                         : cursorCol;
    if(placement.left + placement.width - 1 > editor.screenCols)
        placement.left =
            std::max(1, editor.screenCols - placement.width + 1);

    placement.top =
        std::max(1, (editor.screenRows - placement.height) / 2 + 1);
    const int bottomSpace = editor.screenRows - cursorRow;
    const int topSpace = cursorRow - 1;
    if(bottomSpace >= minHeight)
    {
        placement.height = std::min(placement.height, bottomSpace);
        placement.top = cursorRow + 1;
    }
    else if(topSpace >= minHeight)
    {
        placement.height = std::min(placement.height, topSpace);
        placement.top = cursorRow - placement.height;
    }

    const bool overlapsCursorRow =
        cursorRow >= placement.top &&
        cursorRow < placement.top + placement.height;
    if(overlapsCursorRow && cursorCol >= placement.left &&
       cursorCol < placement.left + placement.width)
    {
        const int rightLeft = cursorCol + 2;
        if(rightLeft + placement.width - 1 <= editor.screenCols)
            placement.left = rightLeft;
        else
        {
            const int leftLeft = cursorCol - placement.width - 1;
            if(leftLeft >= 1)
                placement.left = leftLeft;
        }
    }

    return placement;
}

PopupPlacement placeBottomLeftPopup(int screenRows, int screenCols, int width,
                                    int height, int preferredLeft)
{
    PopupPlacement placement;
    placement.width = std::clamp(width, 1, std::max(1, screenCols));
    placement.height = std::clamp(height, 1, std::max(1, screenRows));
    placement.top = screenRows - placement.height + 1;
    if(placement.top < 1)
        placement.top = 1;
    placement.left = std::max(1, preferredLeft);
    if(placement.left + placement.width - 1 > screenCols)
        placement.left = std::max(1, screenCols - placement.width + 1);
    return placement;
}

int visibleRowsForCursorPopup(editor::statemachine::ModeContext& ctx,
                              int width, int preferredHeight, int chromeRows)
{
    if(ctx.editor)
    {
        const PopupPlacement placement =
            placePopupNearEditorCursor(*ctx.editor, width, preferredHeight);
        return std::max(1, placement.height - chromeRows);
    }
    return std::max(1, preferredHeight - chromeRows);
}

bool moveEditorCursorForPopup(editor::statemachine::ModeContext& ctx, int c)
{
    if(!ctx.editor || !ctx.editor->hasBuffer())
        return false;

    Editor& editor = *ctx.editor;
    if(c == keyCode(control::ControlKey::CTRL_H))
        editor.moveLeft();
    else if(c == keyCode(control::ControlKey::CTRL_L))
        editor.moveRight();
    else if(c == keyCode(control::ControlKey::CTRL_K))
        editor.moveUp();
    else if(c == keyCode(control::ControlKey::CTRL_J))
        editor.moveDown();
    else
        return false;

    editor.adjustViewport();
    editor.needsFullRedraw = true;
    return true;
}

void PopupBase::resetBackdrop() const
{
    backdropDrawn = false;
}

void PopupBase::requestBackdropRedraw(
    editor::statemachine::ModeContext& ctx) const
{
    resetBackdrop();
    ctx.requestFullRedraw();
}

void PopupBase::drawBackdropIfNeeded(Editor& editor) const
{
    if(!backdropDrawn || backdropRows != editor.screenRows ||
       backdropCols != editor.screenCols)
    {
        editor.drawFullScreenSingle();
        backdropDrawn = true;
        backdropRows = editor.screenRows;
        backdropCols = editor.screenCols;
    }
}
} // namespace widgets
