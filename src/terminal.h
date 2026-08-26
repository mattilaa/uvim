#pragma once

#include <deque>
#include <string>
#include "terminal_escapes.h"
#include "key_enums.h"

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
    // Escape constants are centralized in esc::* and re-exposed here.
    static constexpr const char* ESC_CURSOR_HOME = esc::ESC_CURSOR_HOME;
    static constexpr const char* ESC_CLEAR_LINE = esc::ESC_CLEAR_LINE;
    static constexpr const char* ESC_CLEAR_SCREEN = esc::ESC_CLEAR_SCREEN;
    static constexpr const char* ESC_RESET_ALL = esc::ESC_RESET_ALL;
    static constexpr const char* ESC_RESET_ATTRS = esc::ESC_RESET_ATTRS;
    static constexpr const char* ESC_BOLD = esc::ESC_BOLD;
    static constexpr const char* ESC_DIM = esc::ESC_DIM;
    static constexpr const char* ESC_ITALIC = esc::ESC_ITALIC;
    static constexpr const char* ESC_UNDERLINE = esc::ESC_UNDERLINE;
    static constexpr const char* ESC_BLINK = esc::ESC_BLINK;
    static constexpr const char* ESC_BLINK_OFF = esc::ESC_BLINK_OFF;
    static constexpr const char* ESC_REVERSE = esc::ESC_REVERSE;
    static constexpr const char* ESC_BOLD_OFF = esc::ESC_BOLD_OFF;
    static constexpr const char* ESC_HIDE_CURSOR = esc::ESC_HIDE_CURSOR;
    static constexpr const char* ESC_SHOW_CURSOR = esc::ESC_SHOW_CURSOR;

    static constexpr const char* FG_BLACK = esc::FG_BLACK;
    static constexpr const char* FG_RED = esc::FG_RED;
    static constexpr const char* FG_GREEN = esc::FG_GREEN;
    static constexpr const char* FG_YELLOW = esc::FG_YELLOW;
    static constexpr const char* FG_BLUE = esc::FG_BLUE;
    static constexpr const char* FG_MAGENTA = esc::FG_MAGENTA;
    static constexpr const char* FG_CYAN = esc::FG_CYAN;
    static constexpr const char* FG_WHITE = esc::FG_WHITE;
    static constexpr const char* FG_DEFAULT = esc::FG_DEFAULT;

    static constexpr const char* FG_BRIGHT_BLACK = esc::FG_BRIGHT_BLACK;
    static constexpr const char* FG_BRIGHT_RED = esc::FG_BRIGHT_RED;
    static constexpr const char* FG_BRIGHT_GREEN = esc::FG_BRIGHT_GREEN;
    static constexpr const char* FG_BRIGHT_YELLOW = esc::FG_BRIGHT_YELLOW;
    static constexpr const char* FG_BRIGHT_BLUE = esc::FG_BRIGHT_BLUE;
    static constexpr const char* FG_BRIGHT_MAGENTA = esc::FG_BRIGHT_MAGENTA;
    static constexpr const char* FG_BRIGHT_CYAN = esc::FG_BRIGHT_CYAN;
    static constexpr const char* FG_BRIGHT_WHITE = esc::FG_BRIGHT_WHITE;

    static constexpr const char* BG_BLACK = esc::BG_BLACK;
    static constexpr const char* BG_RED = esc::BG_RED;
    static constexpr const char* BG_GREEN = esc::BG_GREEN;
    static constexpr const char* BG_YELLOW = esc::BG_YELLOW;
    static constexpr const char* BG_BLUE = esc::BG_BLUE;
    static constexpr const char* BG_MAGENTA = esc::BG_MAGENTA;
    static constexpr const char* BG_CYAN = esc::BG_CYAN;
    static constexpr const char* BG_WHITE = esc::BG_WHITE;
    static constexpr const char* BG_DEFAULT = esc::BG_DEFAULT;

    static constexpr const char* STYLE_SEARCH_MATCH = esc::STYLE_SEARCH_MATCH;
    static constexpr const char* STYLE_SELECTION = esc::STYLE_SELECTION;
    static constexpr const char* STYLE_GREEN_BOLD = esc::STYLE_GREEN_BOLD;
    static constexpr const char* STYLE_RESET_GREEN_BOLD =
        esc::STYLE_RESET_GREEN_BOLD;
    static constexpr const char* STYLE_CURSOR = esc::STYLE_CURSOR;

    static constexpr const char* ESC_DELETE_LINE = esc::ESC_DELETE_LINE;
    static constexpr const char* ESC_INSERT_LINE = esc::ESC_INSERT_LINE;
    static constexpr const char* ESC_RESET_SCROLL_REGION =
        esc::ESC_RESET_SCROLL_REGION;
    static constexpr const char* ESC_SYNC_UPDATE_BEGIN =
        esc::ESC_SYNC_UPDATE_BEGIN;
    static constexpr const char* ESC_SYNC_UPDATE_END = esc::ESC_SYNC_UPDATE_END;

    static std::string cursorPos(int row, int col);
    static std::string scrollRegion(int top, int bottom);

    static constexpr const char* NEWLINE_CLEAR = esc::NEWLINE_CLEAR;

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
    static bool isTmux();
    static bool useSynchronizedOutput();

    static void unreadKey(int key);
    static bool hasBufferedKeys();
    static void discardPendingInput();
    static std::string takeLastPasteText();
#ifdef UVIM_TESTING
    static void setLastPasteTextForTests(std::string text);
#endif
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
    static std::string lastPasteText;
};
