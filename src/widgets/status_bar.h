#pragma once

#include "mode.h"
#include <string>
#include <string_view>
#include <vector>

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
    int lineCount = 0;
    std::string_view filename;
    bool dirty = false;
    std::string_view searchQuery;
    int searchMatchIndex = 0;
    int searchMatchCount = 0;
    std::string_view lspLabel;
    int lspGap = 7;
    // Background task progress (fixed width); its 1-based start column is
    // written to progressColumn, or 0 when it did not fit.
    std::string_view progress;
    int* progressColumn = nullptr;
    // Width progress needs when it appears; lets an empty bar report where
    // it would go (in the padding) so the first tick can paint in place.
    int progressReserve = 0;
};

struct MessageBarView
{
    Mode currentMode = NORMAL;
    int screenCols = 0;
    std::string_view commandBuffer;
    std::string_view searchQuery;
    int searchMatchIndex = 0;
    int searchMatchCount = 0;
    bool commandLineMessagePrefix = false;
    bool showGitBlame = false;
    bool showGitBlameInfo = false;
    std::string_view blameLine;
    std::string_view locMessage;
    std::string_view statusMessage;
};

// Bottom status line for views that draw their own screen (grep, find,
// regex, buffers). Same layout as the editor status bar:
//   " MODE | context state      <progress> [flags] [tool]     ruler "
struct ViewStatusBarView
{
    const Theme& theme;
    int screenCols = 0;
    // Owned strings: callers often build them from temporaries.
    std::string modeLabel;
    std::string context;          // query, path ...
    std::string state;            // transient, e.g. "searching" (warning)
    std::vector<std::string> flags; // dim tags, e.g. "gitignore"
    std::string toolLabel;        // backing tool, styled like [clangd]
    int toolGap = 7;
    int index = 0;                // 1-based selected row; ruler hidden if
    int count = -1;               //   count < 0
    std::string progress;         // background task progress (fixed width)
    int* progressColumn = nullptr;
    int progressReserve = 0;      // see StatusBarView::progressReserve
};

void appendStatusBar(std::string& output, const StatusBarView& view);
void appendViewStatusBar(std::string& output, const ViewStatusBarView& view);
void appendMessageBar(std::string& output, const MessageBarView& view);
} // namespace widgets
