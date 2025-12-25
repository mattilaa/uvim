#pragma once
#include <string>
#include <termios.h>
#include <unistd.h>

constexpr int BLOCK_BG = 8;  // dark gray
constexpr int CURSOR_BG = 7; // light gray
constexpr int FG_NORMAL = 7; // white

class Terminal
{
public:
    // Terminal modes
    static void enableRawMode();
    static void disableRawMode();
    static void restoreTerminal();

    // Screen control
    static void clearScreen();
    static void clearLine();
    static void moveCursor(int row, int col);
    static void hideCursor();
    static void showCursor();

    static void setCursorBlock();
    static void setCursorBarBlinking();

    // Screen info
    static void getWindowSize(int& rows, int& cols);

    // Output
    static void write(const std::string& str);
    static void write(char c);
    static void flush();

    // Input
    static int readKey();

    // Colors and styles
    static void setColor(int fg, int bg = -1);
    static void resetColor();
    static void setBold();
    static void setReverse();
    static void resetAttributes();

    // Cursor position
    static void saveCursorPosition();
    static void restoreCursorPosition();

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
    static constexpr const char* STYLE_SEARCH_MATCH =
        "\x1b[43m\x1b[30m"; // Yellow bg, black fg
    static constexpr const char* STYLE_SELECTION = "\x1b[7m"; // Reverse video
    static constexpr const char* STYLE_GREEN_BOLD = "\x1b[32;1m";
    static constexpr const char* STYLE_RESET_GREEN_BOLD = "\x1b[39;22m";

    // Scroll region and line manipulation
    static constexpr const char* ESC_DELETE_LINE = "\x1b[M";
    static constexpr const char* ESC_INSERT_LINE = "\x1b[L";
    static constexpr const char* ESC_RESET_SCROLL_REGION = "\x1b[r";

    // Helper to build cursor position escape sequence
    static std::string cursorPos(int row, int col);

    // Helper to build scroll region escape sequence
    static std::string scrollRegion(int top, int bottom);

    // Helper for newline + clear
    static constexpr const char* NEWLINE_CLEAR = "\r\n\x1b[K";

    // Special keys
    enum Key
    {
        ARROW_UP = 1000,
        ARROW_DOWN,
        ARROW_LEFT,
        ARROW_RIGHT,
        PAGE_UP,
        PAGE_DOWN,
        HOME,
        END,
        DELETE,
        BACKSPACE = 8,
        DEL = 127,
        ENTER = 13,
        ESC = 27,
        TAB = 9,
        CTRL_S = 19,
        CTRL_Q = 17,
        CTRL_F = 6,
        CTRL_P = 16,
        CTRL_R = 18,
        CTRL_N = 14,
        CTRL_J = 10,
        CTRL_K = 11,
        CTRL_U = 21,
        CTRL_V = 22,
        CTRL_W = 23,
        CTRL_D = 4,
        CTRL_O = 15,
        CTRL_I = 9,
    };

private:
    static termios originalTermios;
    static bool rawModeEnabled;
};
