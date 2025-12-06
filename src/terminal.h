#pragma once
#include <string>
#include <termios.h>
#include <unistd.h>

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
        BACKSPACE = 8, // Changed from 127 to 8 (actual backspace)
        DEL = 127,     // 127 is actually DEL key
        ENTER = 13,
        ESC = 27,
        TAB = 9,
        CTRL_S = 19,
        CTRL_Q = 17,
        CTRL_F = 6,
        CTRL_P = 16,
        CTRL_R = 18,
        // CTRL_H = 8      // Ctrl-H is also backspace
    };

private:
    static termios originalTermios;
    static bool rawModeEnabled;
};
