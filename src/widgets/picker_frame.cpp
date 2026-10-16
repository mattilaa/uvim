#include "widgets/picker_frame.h"

#include "terminal.h"
#include "text_utils.h"
#include "theme.h"

#include <algorithm>
#include <fstream>

namespace widgets
{
namespace
{
// Below these sizes a floating box would be too cramped to be useful.
constexpr int kMinBoxCols = 40;
constexpr int kMinBoxRows = 8;
// Preview pane needs room for both panes.
constexpr int kMinPreviewBoxCols = 90;

// Clips UTF-8 `text` to `width` display columns.
std::string clipToWidth(std::string_view text, int width)
{
    std::string out;
    int used = 0;
    for(size_t i = 0; i < text.size() && used < width;)
    {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        const size_t len = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        out.append(text.substr(i, len));
        ++used;
        i += len;
    }
    return out;
}
} // namespace

PickerLayout computePickerLayout(const PickerLayoutOptions& options)
{
    PickerLayout layout;
    const int screenRows = std::max(1, options.screenRows);
    const int screenCols = std::max(1, options.screenCols);

    const int boxCols = std::min(
        screenCols,
        std::max(kMinBoxCols, screenCols * std::clamp(options.widthPercent, 10,
                                                      100) / 100));
    const int boxRows = std::min(
        screenRows,
        std::max(kMinBoxRows, screenRows * std::clamp(options.heightPercent, 10,
                                                      100) / 100));

    // Too small for a box (or disabled): use the whole area.
    if(!options.floating || screenCols < kMinBoxCols + 2 ||
       screenRows < kMinBoxRows + 2)
    {
        layout.rows = screenRows;
        layout.cols = screenCols;
        layout.headerCols = screenCols;
        return layout;
    }

    layout.floating = true;
    layout.boxCols = boxCols;
    layout.boxRows = boxRows;
    layout.boxTop = 1 + (screenRows - boxRows) / 2;
    layout.boxLeft = 1 + (screenCols - boxCols) / 2;
    layout.top = layout.boxTop + 1;
    layout.left = layout.boxLeft + 1;
    layout.rows = boxRows - 2;
    const int inner = boxCols - 2;
    layout.headerCols = inner;
    if(options.preview && boxCols >= kMinPreviewBoxCols)
    {
        layout.preview = true;
        layout.cols = (inner - 1) / 2;
        layout.previewLeft = layout.left + layout.cols + 1;
        layout.previewCols = inner - layout.cols - 1;
    }
    else
    {
        layout.cols = inner;
    }
    return layout;
}

PickerCanvas::PickerCanvas(std::string& out, const PickerLayout& layout,
                           const Theme& theme, bool unicode)
    : out_(out), layout_(layout), theme_(theme), unicode_(unicode)
{
}

void PickerCanvas::frame(std::string_view title, std::string_view footer,
                         int headerRows)
{
    headerRows_ = std::clamp(headerRows, 0, layout_.rows);
    if(!layout_.floating)
        return;

    const char* horizontal = unicode_ ? "─" : "-";
    const char* vertical = unicode_ ? "│" : "|";
    const int inner = layout_.boxCols - 2;
    const int dividerCol = layout_.preview ? layout_.previewLeft - 1 : -1;

    // One horizontal edge; `tee` marks where the pane divider meets it.
    auto edge = [&](int row, const char* leftCorner, const char* rightCorner,
                    std::string_view label, const char* tee)
    {
        out_ += Terminal::cursorPos(row, layout_.boxLeft);
        out_ += theme_.reset();
        out_ += theme_.uiAccent();
        out_ += leftCorner;
        std::string text;
        if(!label.empty())
            text = " " + clipToWidth(label, std::max(0, inner - 4)) + " ";
        const int labelWidth = text_utils::utf8DisplayWidth(text);
        int col = 0;
        // One rule character before the label, like Telescope titles.
        if(labelWidth > 0 && inner > labelWidth + 1)
        {
            out_ += horizontal;
            ++col;
            out_ += Terminal::ESC_BOLD;
            out_ += text;
            out_ += theme_.reset();
            out_ += theme_.uiAccent();
            col += labelWidth;
        }
        for(; col < inner; ++col)
        {
            if(tee && layout_.boxLeft + 1 + col == dividerCol)
                out_ += tee;
            else
                out_ += horizontal;
        }
        out_ += rightCorner;
        out_ += theme_.reset();
    };

    const int top = layout_.boxTop;
    const int bottom = layout_.boxTop + layout_.boxRows - 1;
    edge(top, unicode_ ? "╭" : "+", unicode_ ? "╮" : "+", title, nullptr);
    for(int row = 0; row < layout_.rows; ++row)
    {
        const int screenRow = layout_.top + row;
        if(headerRows_ > 0 && row == headerRows_)
        {
            // Separator between the header and the panes.
            edge(screenRow, unicode_ ? "├" : "+", unicode_ ? "┤" : "+", {},
                 unicode_ ? "┬" : "+");
            continue;
        }
        out_ += theme_.uiAccent();
        out_ += Terminal::cursorPos(screenRow, layout_.boxLeft);
        out_ += vertical;
        out_ += Terminal::cursorPos(screenRow,
                                    layout_.boxLeft + layout_.boxCols - 1);
        out_ += vertical;
        if(dividerCol > 0 && row > headerRows_)
        {
            out_ += Terminal::cursorPos(screenRow, dividerCol);
            out_ += vertical;
        }
        out_ += theme_.reset();
    }
    edge(bottom, unicode_ ? "╰" : "+", unicode_ ? "╯" : "+", footer,
         unicode_ ? "┴" : "+");
}

int PickerCanvas::bodyTop() const
{
    // Floating boxes spend one row on the header separator.
    return layout_.top + headerRows_ +
           (layout_.floating && headerRows_ > 0 ? 1 : 0);
}

int PickerCanvas::bodyRows() const
{
    return std::max(1, layout_.top + layout_.rows - bodyTop());
}

void PickerCanvas::beginHeaderRow(int row)
{
    out_ += Terminal::cursorPos(layout_.top + row, layout_.left);
    out_ += theme_.reset();
    if(!layout_.floating)
        out_ += Terminal::ESC_CLEAR_LINE;
}

void PickerCanvas::endHeaderRow(int usedWidth)
{
    if(usedWidth < layout_.headerCols)
        out_.append(layout_.headerCols - usedWidth, ' ');
    out_ += theme_.reset();
}

void PickerCanvas::beginRow(int row)
{
    out_ += Terminal::cursorPos(bodyTop() + row, layout_.left);
    out_ += theme_.reset();
    if(!layout_.floating)
        out_ += Terminal::ESC_CLEAR_LINE;
}

void PickerCanvas::endRow(int usedWidth)
{
    // Pad with the row's active colours so highlights span the pane, and
    // so the code underneath a floating box never shows through.
    if(usedWidth < layout_.cols)
        out_.append(layout_.cols - usedWidth, ' ');
    out_ += theme_.reset();
}

void PickerCanvas::previewRow(int row, int lineNumber, std::string_view text,
                              bool current, const std::vector<Span>& spans)
{
    if(!layout_.preview || row < 0 || row >= bodyRows())
        return;
    out_ += Terminal::cursorPos(bodyTop() + row, layout_.previewLeft);
    out_ += theme_.reset();

    std::string number = std::to_string(lineNumber);
    const int gutter = std::max<int>(4, (int)number.size()) + 1;
    number.insert(0, std::max(0, gutter - 1 - (int)number.size()), ' ');
    const int textWidth = std::max(0, layout_.previewCols - gutter);

    if(current)
        out_ += theme_.selection();
    out_ += current ? theme_.uiWarning() : theme_.uiGutter();
    out_ += number;
    out_ += ' ';
    // Plain text colour; span colours are foreground-only so the current
    // line keeps its selection background.
    const std::string& base = current ? theme_.selection() : theme_.reset();
    out_ += base;
    out_ += theme_.baseFg();

    int used = 0;
    auto emit = [&](size_t from, size_t to)
    {
        // Clip to the pane by display width (one column per code point).
        size_t i = from;
        while(i < to && used < textWidth)
        {
            const unsigned char lead = static_cast<unsigned char>(text[i]);
            const size_t len =
                lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
            out_.append(text.substr(i, len));
            ++used;
            i += len;
        }
    };

    size_t pos = 0;
    for(const Span& span : spans)
    {
        if(used >= textWidth)
            break;
        const size_t start = std::min<size_t>(std::max(0, span.start),
                                              text.size());
        const size_t end = std::min<size_t>(
            start + std::max(0, span.length), text.size());
        if(start < pos)
            continue; // overlapping span; keep the earlier colour
        emit(pos, start);
        if(!span.color.empty())
            out_ += span.color;
        emit(start, end);
        if(!span.color.empty())
            out_ += theme_.baseFg();
        pos = end;
    }
    emit(pos, text.size());

    const int filled = gutter + used;
    if(filled < layout_.previewCols)
        out_.append(layout_.previewCols - filled, ' ');
    out_ += theme_.reset();
}

void PickerCanvas::previewMessage(int row, std::string_view text)
{
    if(!layout_.preview || row < 0 || row >= bodyRows())
        return;
    out_ += Terminal::cursorPos(bodyTop() + row, layout_.previewLeft);
    out_ += theme_.reset();
    out_ += theme_.uiDim();
    const std::string clipped = clipToWidth(text, layout_.previewCols);
    out_ += clipped;
    const int used = text_utils::utf8DisplayWidth(clipped);
    if(used < layout_.previewCols)
        out_.append(layout_.previewCols - used, ' ');
    out_ += theme_.reset();
}

std::vector<std::string> loadPreviewLines(const std::string& path,
                                          size_t maxLines)
{
    std::ifstream file(path, std::ios::binary);
    if(!file)
        return {"(cannot open file)"};

    std::vector<std::string> lines;
    std::string raw;
    size_t inspected = 0;
    while(lines.size() < maxLines && std::getline(file, raw))
    {
        std::string line;
        line.reserve(raw.size());
        for(char ch : raw)
        {
            if(inspected < 4096 && ch == '\0')
                return {"(binary file)"};
            ++inspected;
            if(ch == '\t')
                line.append(4 - (line.size() % 4), ' ');
            else if(ch == '\r')
                continue;
            else if(static_cast<unsigned char>(ch) < 0x20)
                line += ' ';
            else
                line += ch;
        }
        lines.push_back(std::move(line));
    }
    if(lines.empty())
        lines.push_back("(empty file)");
    return lines;
}
} // namespace widgets
