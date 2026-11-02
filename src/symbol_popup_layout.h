#pragma once

#include <algorithm>
#include <string>
#include <vector>

class Editor;

struct SymbolPopupRow
{
    std::string text;
    bool padding = false;
    size_t colon = std::string::npos;
    bool table = false;
    bool heading = false;
    size_t memberStart = 0;
    bool byteMap = false;
    int sourceRow = -1;
};

struct SymbolPopupLayout
{
    std::vector<SymbolPopupRow> rows;
    int innerWidth = 0;
    int visibleRows = 0;
    int width = 0, height = 0;
    int top = 1, left = 1;
    int detailRow = -1;

    int maxScroll() const
    {
        return std::max(0, static_cast<int>(rows.size()) - visibleRows);
    }
};

SymbolPopupLayout layoutSymbolPopup(const Editor& editor);
std::vector<std::string> wrapSymbolPopupLines(
    const std::vector<std::string>& lines,
    int width);
