#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

class Theme;

namespace widgets
{
// Geometry of a picker view (Ctrl-P find, Ctrl-A grep). Floating pickers
// sit in a bordered box over the code, Telescope style, optionally with a
// preview pane on the right; fullscreen pickers use the whole content area.
struct PickerLayout
{
    bool floating = false;
    // List pane content area (1-based screen coordinates).
    int top = 1;
    int left = 1;
    int rows = 0;
    int cols = 0;
    // Preview pane (floating only, when wide enough).
    bool preview = false;
    int previewLeft = 0;
    int previewCols = 0;
    // Header (prompt, key hints) spans the whole width above the panes.
    int headerCols = 0;
    // Outer box including the border (floating only).
    int boxTop = 0;
    int boxLeft = 0;
    int boxRows = 0;
    int boxCols = 0;
};

struct PickerLayoutOptions
{
    int screenRows = 0; // rows available above the status/message bars
    int screenCols = 0;
    bool floating = true;
    int widthPercent = 80;
    int heightPercent = 70;
    bool preview = true;
};

PickerLayout computePickerLayout(const PickerLayoutOptions& options);

// Writes picker rows into `out` at the layout's positions. In floating mode
// it also draws the border, title and the list/preview divider.
class PickerCanvas
{
public:
    PickerCanvas(
        std::string& out,
        const PickerLayout& layout,
        const Theme& theme,
        bool unicode);

    // Border with `title` in the top edge and `footer` in the bottom edge.
    // The first `headerRows` rows span the full width; in a floating box a
    // separator follows them and the list/preview panes start below it.
    void frame(std::string_view title, std::string_view footer, int headerRows);
    // Full-width header row `row` (0-based).
    void beginHeaderRow(int row);
    void endHeaderRow(int usedWidth);
    // Rows available to the list and preview panes below the header.
    int bodyRows() const;
    // Positions at list row `row` (0-based, below the header) and clears it.
    void beginRow(int row);
    // Pads the row to the pane width with the active colours, then resets.
    void endRow(int usedWidth);

    // One preview line: dim line number gutter, text clipped to the pane.
    // `spans` (byte ranges of `text`, ascending, non-overlapping) colour
    // parts of the line, e.g. syntax tokens.
    struct Span
    {
        int start = 0;
        int length = 0;
        std::string color;
    };

    void previewRow(
        int row,
        int lineNumber,
        std::string_view text,
        bool current,
        const std::vector<Span>& spans = {});
    void previewMessage(int row, std::string_view text);
    // Compose a preview row with multiple foreground colours.
    void beginPreviewRow(int row);
    void endPreviewRow(int usedWidth);
    // Show the visible portion of a long list on the floating box's right edge.
    void scrollbar(size_t totalRows, size_t firstVisibleRow);

    const PickerLayout& layout() const
    {
        return layout_;
    }

private:
    int bodyTop() const;

    std::string& out_;
    const PickerLayout& layout_;
    const Theme& theme_;
    bool unicode_;
    int headerRows_ = 0;
};

// Lines of `path` for a preview pane, tabs expanded and control characters
// dropped. Binary or unreadable files yield a single explanatory line.
std::vector<std::string> loadPreviewLines(
    const std::string& path,
    size_t maxLines = 20000);
} // namespace widgets
