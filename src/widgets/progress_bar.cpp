#include "widgets/progress_bar.h"

#include "text_utils.h"
#include <algorithm>
#include <array>

namespace widgets
{
namespace
{
constexpr std::array<std::string_view, 10> kUnicodeSpinner =
    {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
constexpr std::array<std::string_view, 4> kAsciiSpinner = {"|", "/", "-", "\\"};
// Left-aligned partial blocks, 1/8 .. 7/8 of a cell.
constexpr std::array<std::string_view, 7> kEighths =
    {"▏", "▎", "▍", "▌", "▋", "▊", "▉"};

bool unicodeStyle(ProgressStyle style)
{
    return style != ProgressStyle::Ascii;
}

std::string_view spinnerFrame(long long elapsedMs, ProgressStyle style)
{
    const long long frame = std::max(0LL, elapsedMs) / 100;
    if(unicodeStyle(style))
        return kUnicodeSpinner[frame % kUnicodeSpinner.size()];
    return kAsciiSpinner[frame % kAsciiSpinner.size()];
}

std::string colored(
    const ProgressBarView& view,
    std::string_view color,
    std::string_view text)
{
    if(view.style != ProgressStyle::Color || color.empty())
        return std::string(text);
    std::string out(color);
    out += text;
    out += view.base;
    return out;
}

// Cells for a fraction in [0, 1]. Indeterminate jobs pass a bouncing
// window instead: [windowStart, windowStart + windowCells).
std::string meterCells(
    const ProgressBarView& view,
    long long eighthsFilled,
    int windowStart = -1,
    int windowCells = 0)
{
    const int cells = std::max(1, view.barCells);
    std::string out;

    if(view.style == ProgressStyle::Ascii)
    {
        out += '[';
        for(int i = 0; i < cells; ++i)
        {
            const bool on = windowStart >= 0 ? (i >= windowStart &&
                                                i < windowStart + windowCells)
                                             : (i * 8LL + 8 <= eighthsFilled);
            out += on ? '=' : '.';
        }
        out += ']';
        return out;
    }

    const bool color = view.style == ProgressStyle::Color &&
                       !view.fill.empty() && !view.track.empty();
    std::string body;
    for(int i = 0; i < cells; ++i)
    {
        std::string_view glyph = color ? " " : "░";
        if(windowStart >= 0)
        {
            if(i >= windowStart && i < windowStart + windowCells)
                glyph = "█";
        }
        else
        {
            const long long cellEighths =
                std::clamp(eighthsFilled - i * 8LL, 0LL, 8LL);
            if(cellEighths == 8)
                glyph = "█";
            else if(cellEighths > 0)
                glyph = kEighths[cellEighths - 1];
        }
        body += glyph;
    }

    if(!color)
        return "▕" + body + "▏";
    // The track colour is the cell background, so empty cells read as an
    // unfilled channel and partial glyphs blend into it.
    std::string result(view.fill);
    result += body;
    result += view.base;
    return result;
}

std::string elapsedText(long long elapsedMs)
{
    const long long seconds = std::max(0LL, elapsedMs) / 1000;
    if(seconds < 60)
        return std::to_string(seconds) + "s";
    return std::to_string(seconds / 60) + "m" + std::to_string(seconds % 60) +
           "s";
}
} // namespace

bool parseProgressStyle(std::string_view name, ProgressStyle& out)
{
    if(name == "ascii")
        out = ProgressStyle::Ascii;
    else if(name == "glyph" || name == "glyphs" || name == "unicode")
        out = ProgressStyle::Glyph;
    else if(name == "color" || name == "colour")
        out = ProgressStyle::Color;
    else
        return false;
    return true;
}

std::string_view progressStyleName(ProgressStyle style)
{
    switch(style)
    {
    case ProgressStyle::Ascii:
        return "ascii";
    case ProgressStyle::Glyph:
        return "glyph";
    case ProgressStyle::Color:
        return "color";
    }
    return "color";
}

std::string formatProgressMeter(const ProgressBarView& view)
{
    const int cells = std::max(1, view.barCells);
    if(view.total > 0)
    {
        const long long done = std::clamp(view.done, 0LL, view.total);
        return meterCells(view, (done * cells * 8) / view.total);
    }
    if(view.waiting)
        return meterCells(view, 0);
    // Indeterminate: a 3-cell block bouncing across the track.
    const int window = std::min(3, cells);
    const int span = std::max(1, cells - window);
    const long long step = std::max(0LL, view.elapsedMs) / 120;
    const long long phase = step % (span * 2);
    const int start =
        static_cast<int>(phase <= span ? phase : span * 2 - phase);
    return meterCells(view, 0, start, window);
}

std::string formatProgressBar(const ProgressBarView& view)
{
    if(view.maxWidth <= 0)
        return {};

    const std::string spinner =
        view.waiting ? std::string(unicodeStyle(view.style) ? "⏸" : "=")
                     : colored(
                           view,
                           view.accent,
                           spinnerFrame(view.elapsedMs, view.style));

    // Segments from most to least important; the tail ones are dropped
    // until the text fits.
    std::string queue;
    if(view.queued > 1)
        queue = "(" + std::to_string(view.position) + "/" +
                std::to_string(view.queued) + ") ";
    std::string head = queue + spinner + " " + std::string(view.label);

    std::string meter = " " + formatProgressMeter(view);
    std::string percent;
    std::string counts;
    if(view.total > 0)
    {
        const long long done = std::clamp(view.done, 0LL, view.total);
        percent = " " + std::to_string((done * 100) / view.total) + "%";
        if(view.showCounts)
            counts =
                " " + std::to_string(done) + "/" + std::to_string(view.total);
    }
    else if(view.done > 0)
    {
        counts = " " + std::to_string(view.done);
    }

    std::string tail;
    if(view.waiting)
        tail += " waiting";
    else if(!view.detail.empty())
        tail += " " + std::string(view.detail);
    if(!view.waiting)
        tail += " " + elapsedText(view.elapsedMs);
    if(view.running > 1)
        tail += (unicodeStyle(view.style) ? " ×" : " x") +
                std::to_string(view.running);
    std::string keys;
    if(view.queuedKeys > 0)
        keys = " [" + std::to_string(view.queuedKeys) + " keys queued]";

    auto joined = [&] { return head + meter + percent + counts + keys + tail; };
    // Counts go first: the phase/detail says more about where a job is.
    for(std::string* droppable : {&counts, &tail, &keys, &meter})
    {
        std::string text = joined();
        if(text_utils::displayWidth(text) <= view.maxWidth)
            return text;
        droppable->clear();
    }
    std::string text = joined();
    if(text_utils::displayWidth(text) <= view.maxWidth)
        return text;
    // Even the label does not fit; the spinner alone still shows liveness.
    return spinner;
}
} // namespace widgets
