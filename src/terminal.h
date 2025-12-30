#pragma once

#include <deque>
#include <string>

// Terminal backend selection
// Prefer explicit build definitions from CMake:
//   UVIM_TERMINAL_WIN32=1  (Windows console backend)
//   UVIM_TERMINAL_POSIX=1  (POSIX termios backend)
//
// If neither is provided, fall back to platform default.
#if defined(UVIM_TERMINAL_WIN32) && defined(UVIM_TERMINAL_POSIX)
#error "Define only one of UVIM_TERMINAL_WIN32 or UVIM_TERMINAL_POSIX"
#endif

#if !defined(UVIM_TERMINAL_WIN32) && !defined(UVIM_TERMINAL_POSIX)
#if defined(_WIN32)
#define UVIM_TERMINAL_WIN32 1
#else
#define UVIM_TERMINAL_POSIX 1
#endif
#endif

#if defined(UVIM_TERMINAL_WIN32)
#include <cstdint>
#else
#include <termios.h>
#endif

class Terminal
{
public:
    // Escape sequence constants (for building output strings)
    static constexpr const char* ESC_CURSOR_HOME = "\x1b[H";
    static constexpr const char* ESC_CLEAR_LINE = "\x1b[K";
    static constexpr const char* ESC_CLEAR_SCREEN = "\x1b[2J";
    static constexpr const char* ESC_RESET_ALL = "\x1b[m";
    static constexpr const char* ESC_RESET_ATTRS = "\x1b[0m";
    static constexpr const char* ESC_BOLD = "\x1b[1m";
    static constexpr const char* ESC_DIM = "\x1b[2m";
    static constexpr const char* ESC_ITALIC = "\x1b[3m";
    static constexpr const char* ESC_UNDERLINE = "\x1b[4m";
    static constexpr const char* ESC_BLINK = "\x1b[5m";
    static constexpr const char* ESC_BLINK_OFF = "\x1b[25m";
    static constexpr const char* ESC_REVERSE = "\x1b[7m";
    static constexpr const char* ESC_BOLD_OFF = "\x1b[22m";
    static constexpr const char* ESC_HIDE_CURSOR = "\x1b[?25l";
    static constexpr const char* ESC_SHOW_CURSOR = "\x1b[?25h";

    // Foreground colors
    static constexpr const char* FG_BLACK = "\x1b[30m";
    static constexpr const char* FG_RED = "\x1b[31m";
    static constexpr const char* FG_GREEN = "\x1b[32m";
    static constexpr const char* FG_YELLOW = "\x1b[33m";
    static constexpr const char* FG_BLUE = "\x1b[34m";
    static constexpr const char* FG_MAGENTA = "\x1b[35m";
    static constexpr const char* FG_CYAN = "\x1b[36m";
    static constexpr const char* FG_WHITE = "\x1b[37m";
    static constexpr const char* FG_DEFAULT = "\x1b[39m";

    // Bright foreground colors
    static constexpr const char* FG_BRIGHT_BLACK = "\x1b[90m"; // Gray
    static constexpr const char* FG_BRIGHT_RED = "\x1b[91m";
    static constexpr const char* FG_BRIGHT_GREEN = "\x1b[92m";
    static constexpr const char* FG_BRIGHT_YELLOW = "\x1b[93m";
    static constexpr const char* FG_BRIGHT_BLUE = "\x1b[94m";
    static constexpr const char* FG_BRIGHT_MAGENTA = "\x1b[95m";
    static constexpr const char* FG_BRIGHT_CYAN = "\x1b[96m";
    static constexpr const char* FG_BRIGHT_WHITE = "\x1b[97m";

    // Background colors
    static constexpr const char* BG_BLACK = "\x1b[40m";
    static constexpr const char* BG_RED = "\x1b[41m";
    static constexpr const char* BG_GREEN = "\x1b[42m";
    static constexpr const char* BG_YELLOW = "\x1b[43m";
    static constexpr const char* BG_BLUE = "\x1b[44m";
    static constexpr const char* BG_MAGENTA = "\x1b[45m";
    static constexpr const char* BG_CYAN = "\x1b[46m";
    static constexpr const char* BG_WHITE = "\x1b[47m";
    static constexpr const char* BG_DEFAULT = "\x1b[49m";

    // Combined styles for common use cases
    static constexpr const char* STYLE_SEARCH_MATCH = "\x1b[43m\x1b[30m";
    static constexpr const char* STYLE_SELECTION = "\x1b[7m";
    static constexpr const char* STYLE_GREEN_BOLD = "\x1b[32;1m";
    static constexpr const char* STYLE_RESET_GREEN_BOLD = "\x1b[39;22m";

    // Scroll region and line manipulation
    static constexpr const char* ESC_DELETE_LINE = "\x1b[M";
    static constexpr const char* ESC_INSERT_LINE = "\x1b[L";
    static constexpr const char* ESC_RESET_SCROLL_REGION = "\x1b[r";

    static std::string cursorPos(int row, int col);
    static std::string scrollRegion(int top, int bottom);

    static constexpr const char* NEWLINE_CLEAR = "\r\n\x1b[K";

    enum Key : int
    {
        ARROW_UP = 1000,
        ARROW_DOWN,
        ARROW_LEFT,
        ARROW_RIGHT,
        PAGE_UP,
        PAGE_DOWN,
        HOME,
        END,
        DELETE_KEY,

        BACKSPACE = 8,
        DEL = 127,

        ENTER = 13,
        ESC = 27,
        TAB = 9,

        SHIFT_TAB = 1100,

        CTRL_A = 1,
        CTRL_B = 2,
        CTRL_C = 3,
        CTRL_D = 4,
        CTRL_E = 5,
        CTRL_F = 6,
        CTRL_G = 7,
        CTRL_H = 8,
        CTRL_I = 9,
        CTRL_J = 10,
        CTRL_K = 11,
        CTRL_L = 12,
        CTRL_M = 13,
        CTRL_N = 14,
        CTRL_O = 15,
        CTRL_P = 16,
        CTRL_Q = 17,
        CTRL_R = 18,
        CTRL_S = 19,
        CTRL_T = 20,
        CTRL_U = 21,
        CTRL_V = 22,
        CTRL_W = 23,
        CTRL_X = 24,
        CTRL_Y = 25,
        CTRL_Z = 26,
    };

    static void enableRawMode();
    static void disableRawMode();
    static void restoreTerminal();

    static void clearScreen();
    static void clearLine();
    static void moveCursor(int row, int col);
    static void hideCursor();
    static void showCursor();
    static void getWindowSize(int& rows, int& cols);

    static void write(const std::string& str);
    static void write(char c);
    static void flush();

    static void unreadKey(int key);
    static int readKey();
    static int readKeyTimeout(int timeoutMs);

    static void setColor(int fg, int bg = -1);
    static void resetColor();
    static void setBold();
    static void setReverse();
    static void resetAttributes();

    static void saveCursorPosition();
    static void restoreCursorPosition();

    static void setCursorBlock();
    static void setCursorBarBlinking();

private:
    static int readKeyInternal(int timeoutMs);

#if defined(UVIM_TERMINAL_POSIX)
    static termios originalTermios;
#endif
    static bool rawModeEnabled;
    static std::deque<int> keyBuffer;
};
