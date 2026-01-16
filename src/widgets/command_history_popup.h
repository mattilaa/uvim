#pragma once

#include <string>
#include <vector>

class Theme;

namespace widgets
{
struct CommandHistoryPopupView
{
    const Theme& theme;
    int screenRows = 0;
    int screenCols = 0;
    const std::vector<std::string>& history;
    const std::vector<int>& matches;
    int offset = 0;
    int cursor = 0;
};

void drawCommandHistoryPopup(std::string& output,
                             const CommandHistoryPopupView& view);
} // namespace widgets
