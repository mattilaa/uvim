#pragma once

#include <string>
#include <vector>

class Theme;

namespace widgets
{
struct CommandPopupView
{
    const Theme& theme;
    int screenRows = 0;
    int screenCols = 0;
    const std::vector<std::string>& entries;
    const std::vector<int>& filtered;
    int offset = 0;
    int cursor = 0;
};

void drawCommandPopup(std::string& output, const CommandPopupView& view);
} // namespace widgets
