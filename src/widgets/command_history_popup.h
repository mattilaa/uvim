#pragma once

#include "widgets/popup_base.h"

#include <string>
#include <vector>

namespace widgets
{
struct CommandHistoryPopupView
{
    PopupFrameView frame;
    const std::vector<std::string>& history;
    const std::vector<int>& matches;
    int offset = 0;
    int cursor = 0;
};

void drawCommandHistoryPopup(std::string& output,
                             const CommandHistoryPopupView& view);
} // namespace widgets
