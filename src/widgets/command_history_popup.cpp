#include "widgets/command_history_popup.h"

#include "terminal.h"
#include "text_utils.h"
#include "theme.h"
#include <algorithm>

namespace widgets
{
void drawCommandHistoryPopup(std::string& output,
                             const CommandHistoryPopupView& view)
{
    const PopupFrameView& frame = view.frame;
    output += frame.theme.baseFg();

    int rows = std::min(8, std::max(1, frame.screenRows - 2));
    if(rows <= 0)
        return;

    int maxContent = 0;
    if(!view.history.empty())
    {
        for(const auto& entry : view.history)
            maxContent = std::max(maxContent, text_utils::displayWidth(entry));
    }
    else
    {
        maxContent = text_utils::displayWidth("No matches");
    }

    if(maxContent <= 0)
        maxContent = text_utils::displayWidth("No matches");

    int innerW = std::max(24, maxContent);
    int totalW = innerW + 4;
    if(totalW > frame.screenCols)
    {
        totalW = frame.screenCols;
        innerW = std::max(4, totalW - 4);
    }

    int totalH = rows + 2;
    const PopupPlacement placement =
        placeBottomLeftPopup(frame.screenRows, frame.screenCols, totalW,
                             totalH);
    int top = placement.top;
    int left = placement.left;

    auto moveTo = [&](int r, int c) { output += Terminal::cursorPos(r, c); };

    moveTo(top, left);
    output += "+";
    output.append(innerW + 2, '-');
    output += "+";

    for(int i = 0; i < rows; ++i)
    {
        moveTo(top + 1 + i, left);
        output += "| ";

        std::string line;
        if(view.matches.empty() && i == 0)
        {
            line = "No matches";
        }
        else if(!view.matches.empty())
        {
            int visibleIndex = i + view.offset;
            if(visibleIndex >= 0 && visibleIndex < (int)view.matches.size())
            {
                int idx = view.matches[visibleIndex];
                if(idx >= 0 && idx < (int)view.history.size())
                    line = view.history[idx];
            }
        }

        if((int)line.length() > innerW)
            line = line.substr(0, innerW - 3) + "...";

        if(!view.matches.empty() &&
           (i + view.offset) < (int)view.matches.size() &&
           (i + view.offset) == view.cursor)
        {
            output += frame.theme.selection();
            output.append(line);
            output += frame.theme.reset();
        }
        else
        {
            output.append(line);
        }

        int pad = innerW - text_utils::displayWidth(line);
        if(pad > 0)
            output.append(pad, ' ');
        output += " |";
    }

    moveTo(top + totalH - 1, left);
    output += "+";
    output.append(innerW + 2, '-');
    output += "+";
}
} // namespace widgets
