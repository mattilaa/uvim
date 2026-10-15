#pragma once

#include <string>
#include <string_view>

namespace widgets
{
enum class ProgressStyle
{
    Ascii, // [======....]
    Glyph, // ▕█████▋    ▏ with eighth-block sub-cell precision
    Color, // glyph bar drawn with theme fg/bg colours
};

// Parses "ascii" / "glyph" / "color" (also "colour"). Returns false when the
// name is unknown and leaves `out` untouched.
bool parseProgressStyle(std::string_view name, ProgressStyle& out);
std::string_view progressStyleName(ProgressStyle style);

// One running operation, as shown by every status bar renderer so progress
// always looks and sits the same.
struct ProgressBarView
{
    std::string_view label;
    std::string_view detail;
    long long done = 0;
    long long total = 0; // <= 0: indeterminate (spinner + bouncing block)
    long long elapsedMs = 0;
    int position = 0;    // (position/queued) shown when queued > 1
    int queued = 0;
    int running = 0;     // jobs running concurrently, including this one
    bool waiting = false; // queued behind an older job, not started yet
    int queuedKeys = 0;  // keystrokes waiting for a foreground job
    bool showCounts = true; // "done/total" after the percentage
    ProgressStyle style = ProgressStyle::Color;
    int barCells = 10;
    int maxWidth = 60; // display columns available

    // Colour style only. `base` restores the surrounding (status bar)
    // colours after each coloured run.
    std::string_view base;
    std::string_view fill;
    std::string_view track;
    std::string_view accent;
};

// Spinner frame advances every 100 ms of elapsed time.
std::string formatProgressBar(const ProgressBarView& view);
// Just the bar cells, for list views that lay out their own columns.
std::string formatProgressMeter(const ProgressBarView& view);
} // namespace widgets
