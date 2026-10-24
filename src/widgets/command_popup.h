#pragma once

#include "widgets/popup_base.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace widgets
{
struct CommandPopupView
{
    PopupFrameView frame;
    const std::vector<std::string>& entries;
    const std::vector<int>& filtered;
    int offset = 0;
    int cursor = 0;
    std::string_view title{};
    std::string_view emptyMessage = "No matches";
    int maxRows = 8;
    // Optional foreground colors, indexed like entries. Selection keeps its
    // background.
    std::span<const std::string> rowColors{};
};

std::string_view commandDocumentation(std::string_view command);
void drawCommandPopup(std::string& output, const CommandPopupView& view);
} // namespace widgets
