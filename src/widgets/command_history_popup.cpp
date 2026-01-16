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
    output += view.theme.baseFg();

    int rows = 0;
    if(view.matches.empty())
    {
        rows = 1;
    }
    else
    {
        rows = std::min(8, (int)view.matches.size());
    }

    if(rows <= 0)
        return;

    int maxContent = 0;
    if(view.matches.empty())
    {
        maxContent = text_utils::displayWidth("No matches");
    }
    else
    {
        for(int i = 0; i < rows; ++i)
        {
            int idx = view.matches[i + view.offset];
            if(idx >= 0 && idx < (int)view.history.size())
            {
                maxContent =
                    std::max(maxContent,
                             text_utils::displayWidth(view.history[idx]));
            }
        }
    }

    int innerW = std::max(12, maxContent);
    int totalW = innerW + 4;
    if(totalW > view.screenCols)
    {
        totalW = view.screenCols;
        innerW = std::max(4, totalW - 4);
    }

    int totalH = rows + 2;
    int top = view.screenRows - totalH + 1;
    if(top < 1)
        top = 1;
    int left = 2;
    if(left + totalW - 1 > view.screenCols)
        left = std::max(1, view.screenCols - totalW + 1);

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
        if(view.matches.empty())
        {
            line = "No matches";
        }
        else
        {
            int idx = view.matches[i + view.offset];
            if(idx >= 0 && idx < (int)view.history.size())
                line = view.history[idx];
        }

        if((int)line.length() > innerW)
            line = line.substr(0, innerW - 3) + "...";

        if(!view.matches.empty() && (i + view.offset) == view.cursor)
        {
            output += view.theme.selection();
            output.append(line);
            output += view.theme.reset();
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
