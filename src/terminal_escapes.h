#pragma once

#include "color_constant.h"

namespace esc
{
constexpr const char* ESC_CURSOR_HOME = "\x1b[H";
constexpr const char* ESC_CLEAR_LINE = "\x1b[K";
constexpr const char* ESC_CLEAR_SCREEN = "\x1b[2J";
constexpr const char* ESC_RESET_ALL = color::ansi(color::AnsiColor::ResetAll);
constexpr const char* ESC_RESET_ATTRS = color::ansi(color::AnsiColor::Reset);
constexpr const char* ESC_BOLD = color::ansi(color::AnsiColor::Bold);
constexpr const char* ESC_DIM = color::ansi(color::AnsiColor::Dim);
constexpr const char* ESC_ITALIC = color::ansi(color::AnsiColor::Italic);
constexpr const char* ESC_UNDERLINE = color::ansi(color::AnsiColor::Underline);
constexpr const char* ESC_BLINK = color::ansi(color::AnsiColor::Blink);
constexpr const char* ESC_BLINK_OFF = color::ansi(color::AnsiColor::BlinkOff);
constexpr const char* ESC_REVERSE = color::ansi(color::AnsiColor::Reverse);
constexpr const char* ESC_BOLD_OFF = color::ansi(color::AnsiColor::BoldOff);
constexpr const char* ESC_HIDE_CURSOR = "\x1b[?25l";
constexpr const char* ESC_SHOW_CURSOR = "\x1b[?25h";

constexpr const char* FG_BLACK = color::ansi(color::AnsiColor::FgBlack);
constexpr const char* FG_RED = color::ansi(color::AnsiColor::FgRed);
constexpr const char* FG_GREEN = color::ansi(color::AnsiColor::FgGreen);
constexpr const char* FG_YELLOW = color::ansi(color::AnsiColor::FgYellow);
constexpr const char* FG_BLUE = color::ansi(color::AnsiColor::FgBlue);
constexpr const char* FG_MAGENTA = color::ansi(color::AnsiColor::FgMagenta);
constexpr const char* FG_CYAN = color::ansi(color::AnsiColor::FgCyan);
constexpr const char* FG_WHITE = color::ansi(color::AnsiColor::FgWhite);
constexpr const char* FG_DEFAULT = color::ansi(color::AnsiColor::FgDefault);

constexpr const char* FG_BRIGHT_BLACK =
    color::ansi(color::AnsiColor::FgBrightBlack);
constexpr const char* FG_BRIGHT_RED =
    color::ansi(color::AnsiColor::FgBrightRed);
constexpr const char* FG_BRIGHT_GREEN =
    color::ansi(color::AnsiColor::FgBrightGreen);
constexpr const char* FG_BRIGHT_YELLOW =
    color::ansi(color::AnsiColor::FgBrightYellow);
constexpr const char* FG_BRIGHT_BLUE =
    color::ansi(color::AnsiColor::FgBrightBlue);
constexpr const char* FG_BRIGHT_MAGENTA =
    color::ansi(color::AnsiColor::FgBrightMagenta);
constexpr const char* FG_BRIGHT_CYAN =
    color::ansi(color::AnsiColor::FgBrightCyan);
constexpr const char* FG_BRIGHT_WHITE =
    color::ansi(color::AnsiColor::FgBrightWhite);

constexpr const char* BG_BLACK = color::ansi(color::AnsiColor::BgBlack);
constexpr const char* BG_RED = color::ansi(color::AnsiColor::BgRed);
constexpr const char* BG_GREEN = color::ansi(color::AnsiColor::BgGreen);
constexpr const char* BG_YELLOW = color::ansi(color::AnsiColor::BgYellow);
constexpr const char* BG_BLUE = color::ansi(color::AnsiColor::BgBlue);
constexpr const char* BG_MAGENTA = color::ansi(color::AnsiColor::BgMagenta);
constexpr const char* BG_CYAN = color::ansi(color::AnsiColor::BgCyan);
constexpr const char* BG_WHITE = color::ansi(color::AnsiColor::BgWhite);
constexpr const char* BG_DEFAULT = color::ansi(color::AnsiColor::BgDefault);

constexpr const char* STYLE_SEARCH_MATCH =
    color::ansi(color::AnsiColor::StyleSearchMatch);
constexpr const char* STYLE_SELECTION =
    color::ansi(color::AnsiColor::StyleSelection);
constexpr const char* STYLE_GREEN_BOLD =
    color::ansi(color::AnsiColor::StyleGreenBold);
constexpr const char* STYLE_RESET_GREEN_BOLD =
    color::ansi(color::AnsiColor::StyleResetGreenBold);
constexpr const char* STYLE_CURSOR =
    color::ansi(color::AnsiColor::StyleCursor);

constexpr const char* ESC_DELETE_LINE = "\x1b[M";
constexpr const char* ESC_INSERT_LINE = "\x1b[L";
constexpr const char* ESC_RESET_SCROLL_REGION = "\x1b[r";
constexpr const char* ESC_SYNC_UPDATE_BEGIN = "\x1b[?2026h";
constexpr const char* ESC_SYNC_UPDATE_END = "\x1b[?2026l";

constexpr const char* NEWLINE_CLEAR = "\r\n\x1b[K";
} // namespace esc
