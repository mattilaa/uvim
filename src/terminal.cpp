#include "terminal.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>

termios Terminal::originalTermios;
bool Terminal::rawModeEnabled = false;
std::deque<int> Terminal::keyBuffer;

void Terminal::enableRawMode()
{
    if(rawModeEnabled)
        return;

    // Save original terminal settings
    tcgetattr(STDIN_FILENO, &originalTermios);

    // Register cleanup on exit
    atexit(restoreTerminal);

    // Get current settings and modify them
    termios raw = originalTermios;

    // Input flags: disable break, CR to NL, parity check, strip 8th bit,
    // start/stop
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    // Output flags: disable post processing
    raw.c_oflag &= ~(OPOST);

    // Control flags: set 8 bit chars
    raw.c_cflag |= (CS8);

    // Local flags: disable canonical mode, echo, signals
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    // Control characters: set minimum bytes and timeout for read()
    raw.c_cc[VMIN] = 0;  // Read doesn't block
    raw.c_cc[VTIME] = 1; // Read timeout (deciseconds)

    // Apply the settings
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    rawModeEnabled = true;
}

void Terminal::disableRawMode()
{
    if(!rawModeEnabled)
        return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalTermios);
    rawModeEnabled = false;
}

void Terminal::restoreTerminal()
{
    disableRawMode();
    clearScreen();
    moveCursor(1, 1);
}

void Terminal::clearScreen()
{
    write("\x1b[2J"); // Clear entire screen
    write("\x1b[H");  // Move cursor to home position (1,1)
    flush();
}

void Terminal::clearLine()
{
    write("\x1b[K"); // Clear from cursor to end of line
}

void Terminal::moveCursor(int row, int col)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
    write(buf);
}

void Terminal::hideCursor()
{
    write("\x1b[?25l");
}

void Terminal::showCursor()
{
    write("\x1b[?25h");
}

void Terminal::getWindowSize(int& rows, int& cols)
{
    struct winsize ws;

    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        // Fallback method: move cursor far right/bottom and get position
        write("\x1b[999C\x1b[999B");

        // Query cursor position
        write("\x1b[6n");
        flush();

        char buf[32];
        int i = 0;
        while(i < sizeof(buf) - 1)
        {
            if(read(STDIN_FILENO, &buf[i], 1) != 1)
                break;
            if(buf[i] == 'R')
                break;
            i++;
        }
        buf[i] = '\0';

        // Parse response: ESC[rows;colsR
        if(buf[0] == '\x1b' && buf[1] == '[')
        {
            sscanf(&buf[2], "%d;%d", &rows, &cols);
        }
        else
        {
            rows = 24;
            cols = 80;
        }
    }
    else
    {
        cols = ws.ws_col;
        rows = ws.ws_row;
    }
}

void Terminal::write(const std::string& str)
{
    ::write(STDOUT_FILENO, str.c_str(), str.length());
}

void Terminal::write(char c)
{
    ::write(STDOUT_FILENO, &c, 1);
}

void Terminal::flush()
{
    fflush(stdout);
    fsync(STDOUT_FILENO);
}

void Terminal::unreadKey(int key)
{
    keyBuffer.push_front(key);
}

int Terminal::readKeyInternal(int timeoutMs)
{
    char c;

    if(timeoutMs >= 0)
    {
        // Use select() for timeout
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);

        struct timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;

        int ret = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
        if(ret <= 0)
        {
            return -1; // Timeout or error
        }
    }

    int nread;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if(nread == -1)
            return -1;
        if(timeoutMs >= 0)
            return -1; // Don't block if we had a timeout
    }

    // Handle escape sequences
    if(c == '\x1b')
    {
        char seq[5];

        // Try to read the next character with a short timeout
        // to distinguish ESC key from escape sequences
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv = {0, 50000}; // 50ms timeout

        if(select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) <= 0)
        {
            return ESC; // Just the ESC key
        }

        if(read(STDIN_FILENO, &seq[0], 1) != 1)
            return ESC;
        if(read(STDIN_FILENO, &seq[1], 1) != 1)
            return ESC;

        if(seq[0] == '[')
        {
            if(seq[1] >= '0' && seq[1] <= '9')
            {
                if(read(STDIN_FILENO, &seq[2], 1) != 1)
                    return ESC;

                if(seq[2] == '~')
                {
                    switch(seq[1])
                    {
                    case '1':
                        return HOME;
                    case '3':
                        return DELETE_KEY;
                    case '4':
                        return END;
                    case '5':
                        return PAGE_UP;
                    case '6':
                        return PAGE_DOWN;
                    case '7':
                        return HOME;
                    case '8':
                        return END;
                    }
                }
                else if(seq[2] == ';')
                {
                    // Extended sequence like ESC[1;5A (Ctrl+Up)
                    // or ESC[1;2Z (Shift+Tab)
                    if(read(STDIN_FILENO, &seq[3], 1) != 1)
                        return ESC;
                    if(read(STDIN_FILENO, &seq[4], 1) != 1)
                        return ESC;

                    // Check for Shift+Tab: ESC[1;2Z or just ESC[Z
                    if(seq[4] == 'Z')
                    {
                        return SHIFT_TAB;
                    }

                    // Could handle other modified keys here
                    // For now, map to base arrow keys
                    switch(seq[4])
                    {
                    case 'A':
                        return ARROW_UP;
                    case 'B':
                        return ARROW_DOWN;
                    case 'C':
                        return ARROW_RIGHT;
                    case 'D':
                        return ARROW_LEFT;
                    }
                }
            }
            else if(seq[1] == 'Z')
            {
                // Shift+Tab: ESC[Z
                return SHIFT_TAB;
            }
            else
            {
                switch(seq[1])
                {
                case 'A':
                    return ARROW_UP;
                case 'B':
                    return ARROW_DOWN;
                case 'C':
                    return ARROW_RIGHT;
                case 'D':
                    return ARROW_LEFT;
                case 'H':
                    return HOME;
                case 'F':
                    return END;
                }
            }
        }
        else if(seq[0] == 'O')
        {
            switch(seq[1])
            {
            case 'H':
                return HOME;
            case 'F':
                return END;
            }
        }

        return ESC;
    }

    return c;
}

int Terminal::readKey()
{
    // First check if we have any unread keys in the buffer
    if(!keyBuffer.empty())
    {
        int key = keyBuffer.front();
        keyBuffer.pop_front();
        return key;
    }

    return readKeyInternal(-1); // No timeout, block until key
}

int Terminal::readKeyTimeout(int timeoutMs)
{
    // First check if we have any unread keys in the buffer
    if(!keyBuffer.empty())
    {
        int key = keyBuffer.front();
        keyBuffer.pop_front();
        return key;
    }

    return readKeyInternal(timeoutMs);
}

void Terminal::setColor(int fg, int bg)
{
    if(fg >= 0)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "\x1b[%dm", 30 + fg);
        write(buf);
    }
    if(bg >= 0)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "\x1b[%dm", 40 + bg);
        write(buf);
    }
}

void Terminal::resetColor()
{
    write("\x1b[39;49m");
}

void Terminal::setBold()
{
    write("\x1b[1m");
}

void Terminal::setReverse()
{
    write("\x1b[7m");
}

void Terminal::resetAttributes()
{
    write("\x1b[0m");
}

void Terminal::saveCursorPosition()
{
    write("\x1b[s");
}

void Terminal::restoreCursorPosition()
{
    write("\x1b[u");
}

void Terminal::setCursorBlock()
{
    // Steady block cursor
    write("\x1b[2 q");
}

void Terminal::setCursorBarBlinking()
{
    // Blinking bar (thin cursor)
    write("\x1b[5 q");
}

std::string Terminal::cursorPos(int row, int col)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
    return std::string(buf);
}

std::string Terminal::scrollRegion(int top, int bottom)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dr", top, bottom);
    return std::string(buf);
}
