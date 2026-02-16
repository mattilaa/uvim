#pragma once

namespace esc
{
constexpr const char* ESC_CURSOR_HOME = "\x1b[H";
constexpr const char* ESC_CLEAR_LINE = "\x1b[K";
constexpr const char* ESC_CLEAR_SCREEN = "\x1b[2J";
constexpr const char* ESC_RESET_ALL = "\x1b[m";
constexpr const char* ESC_RESET_ATTRS = "\x1b[0m";
constexpr const char* ESC_BOLD = "\x1b[1m";
constexpr const char* ESC_DIM = "\x1b[2m";
constexpr const char* ESC_ITALIC = "\x1b[3m";
constexpr const char* ESC_UNDERLINE = "\x1b[4m";
constexpr const char* ESC_BLINK = "\x1b[5m";
constexpr const char* ESC_BLINK_OFF = "\x1b[25m";
constexpr const char* ESC_REVERSE = "\x1b[7m";
constexpr const char* ESC_BOLD_OFF = "\x1b[22m";
constexpr const char* ESC_HIDE_CURSOR = "\x1b[?25l";
constexpr const char* ESC_SHOW_CURSOR = "\x1b[?25h";

constexpr const char* FG_BLACK = "\x1b[30m";
constexpr const char* FG_RED = "\x1b[31m";
constexpr const char* FG_GREEN = "\x1b[32m";
constexpr const char* FG_YELLOW = "\x1b[33m";
constexpr const char* FG_BLUE = "\x1b[34m";
constexpr const char* FG_MAGENTA = "\x1b[35m";
constexpr const char* FG_CYAN = "\x1b[36m";
constexpr const char* FG_WHITE = "\x1b[37m";
constexpr const char* FG_DEFAULT = "\x1b[39m";

constexpr const char* FG_BRIGHT_BLACK = "\x1b[90m";
constexpr const char* FG_BRIGHT_RED = "\x1b[91m";
constexpr const char* FG_BRIGHT_GREEN = "\x1b[92m";
constexpr const char* FG_BRIGHT_YELLOW = "\x1b[93m";
constexpr const char* FG_BRIGHT_BLUE = "\x1b[94m";
constexpr const char* FG_BRIGHT_MAGENTA = "\x1b[95m";
constexpr const char* FG_BRIGHT_CYAN = "\x1b[96m";
constexpr const char* FG_BRIGHT_WHITE = "\x1b[97m";

constexpr const char* BG_BLACK = "\x1b[40m";
constexpr const char* BG_RED = "\x1b[41m";
constexpr const char* BG_GREEN = "\x1b[42m";
constexpr const char* BG_YELLOW = "\x1b[43m";
constexpr const char* BG_BLUE = "\x1b[44m";
constexpr const char* BG_MAGENTA = "\x1b[45m";
constexpr const char* BG_CYAN = "\x1b[46m";
constexpr const char* BG_WHITE = "\x1b[47m";
constexpr const char* BG_DEFAULT = "\x1b[49m";

constexpr const char* STYLE_SEARCH_MATCH = "\x1b[43m\x1b[30m";
constexpr const char* STYLE_SELECTION = "\x1b[7m";
constexpr const char* STYLE_GREEN_BOLD = "\x1b[32;1m";
constexpr const char* STYLE_RESET_GREEN_BOLD = "\x1b[39;22m";
constexpr const char* STYLE_CURSOR = "\x1b[103m\x1b[30;1m";

constexpr const char* ESC_DELETE_LINE = "\x1b[M";
constexpr const char* ESC_INSERT_LINE = "\x1b[L";
constexpr const char* ESC_RESET_SCROLL_REGION = "\x1b[r";
constexpr const char* ESC_SYNC_UPDATE_BEGIN = "\x1b[?2026h";
constexpr const char* ESC_SYNC_UPDATE_END = "\x1b[?2026l";

constexpr const char* NEWLINE_CLEAR = "\r\n\x1b[K";
} // namespace esc
