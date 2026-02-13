#include "widgets/command_popup.h"

#include "terminal.h"
#include "text_utils.h"
#include "theme.h"
#include "token_type.h"
#include <algorithm>

namespace widgets
{
void drawCommandPopup(std::string& output, const CommandPopupView& view)
{
    output += view.theme.baseFg();

    int rows = std::min(8, std::max(1, view.screenRows - 2));
    if(rows <= 0)
        return;

    int maxContent = 0;
    if(!view.entries.empty())
    {
        for(const auto& entry : view.entries)
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
    text_utils::appendU8(output, u8"╭");
    text_utils::appendUtf8Repeat(output, u8"─", innerW + 2);
    text_utils::appendU8(output, u8"╮");

    for(int i = 0; i < rows; ++i)
    {
        moveTo(top + 1 + i, left);
        text_utils::appendU8(output, u8"│ ");

        std::string line;
        if(view.filtered.empty() && i == 0)
        {
            line = "No matches";
        }
        else if(!view.filtered.empty())
        {
            int visibleIndex = i + view.offset;
            if(visibleIndex >= 0 && visibleIndex < (int)view.filtered.size())
            {
                int idx = view.filtered[visibleIndex];
                if(idx >= 0 && idx < (int)view.entries.size())
                    line = view.entries[idx];
            }
        }

        if((int)line.length() > innerW)
            line = line.substr(0, innerW - 3) + "...";

        if(!view.filtered.empty() &&
           (i + view.offset) < (int)view.filtered.size() &&
           (i + view.offset) == view.cursor)
        {
            output += view.theme.selection();
            output.append(line);
            output += view.theme.reset();
        }
        else
        {
            output += view.theme.syntax(TOKEN_FUNCTION);
            output.append(line);
            output += view.theme.reset();
        }

        int pad = innerW - text_utils::displayWidth(line);
        if(pad > 0)
            output.append(pad, ' ');
        text_utils::appendU8(output, u8" │");
    }

    moveTo(top + totalH - 1, left);
    text_utils::appendU8(output, u8"╰");
    text_utils::appendUtf8Repeat(output, u8"─", innerW + 2);
    text_utils::appendU8(output, u8"╯");
}
} // namespace widgets
