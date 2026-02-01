#pragma once

#include "mode.h"
#include <string>
#include <string_view>

class Theme;

namespace widgets
{
struct StatusBarView
{
    const Theme& theme;
    int screenCols = 0;
    std::string_view modeLabel;
    int currentBufferIndex = 0;
    int bufferCount = 0;
    int cursorY = 0;
    int cursorX = 0;
    std::string_view filename;
    bool dirty = false;
    std::string_view searchQuery;
    int searchMatchIndex = 0;
    int searchMatchCount = 0;
    std::string_view lspLabel;
};

struct MessageBarView
{
    Mode currentMode = NORMAL;
    int screenCols = 0;
    std::string_view commandBuffer;
    std::string_view searchQuery;
    int searchMatchIndex = 0;
    int searchMatchCount = 0;
    bool showGitBlame = false;
    bool showGitBlameInfo = false;
    std::string_view blameLine;
    std::string_view statusMessage;
};

void appendStatusBar(std::string& output, const StatusBarView& view);
void appendMessageBar(std::string& output, const MessageBarView& view);
} // namespace widgets
