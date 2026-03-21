#include "terminal.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(UVIM_TERMINAL_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#if defined(UVIM_TERMINAL_POSIX)
termios Terminal::originalTermios;
#endif
bool Terminal::rawModeEnabled = false;
std::deque<int> Terminal::keyBuffer;

namespace
{
using namespace std::chrono;

#if defined(UVIM_TERMINAL_WIN32)

static DWORD g_origInMode = 0;
static DWORD g_origOutMode = 0;

static HANDLE hIn() noexcept
{
    return GetStdHandle(STD_INPUT_HANDLE);
}
static HANDLE hOut() noexcept
{
    return GetStdHandle(STD_OUTPUT_HANDLE);
}

static bool input_is_console() noexcept
{
    DWORD mode = 0;
    return GetConsoleMode(hIn(), &mode) != 0;
}

static bool output_is_console() noexcept
{
    DWORD mode = 0;
    return GetConsoleMode(hOut(), &mode) != 0;
}

static std::wstring utf8_to_utf16(std::string_view text)
{
    if(text.empty())
        return {};
    const int needed = MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if(needed <= 0)
        return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    const int converted = MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(),
        needed);
    if(converted <= 0)
        return {};
    return out;
}

static void enable_vt_and_raw_console()
{
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    DWORD inMode = 0;
    if(GetConsoleMode(hIn(), &inMode))
    {
        g_origInMode = inMode;
        inMode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
        inMode &= ~(ENABLE_MOUSE_INPUT);
        inMode |= ENABLE_EXTENDED_FLAGS;
        inMode &= ~(ENABLE_QUICK_EDIT_MODE);
        inMode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
        SetConsoleMode(hIn(), inMode);
    }

    DWORD outMode = 0;
    if(GetConsoleMode(hOut(), &outMode))
    {
        g_origOutMode = outMode;
        outMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut(), outMode);
    }
}

static void restore_console_modes()
{
    if(g_origInMode)
        SetConsoleMode(hIn(), g_origInMode);
    if(g_origOutMode)
        SetConsoleMode(hOut(), g_origOutMode);
}

static bool wait_stdin(milliseconds timeout) noexcept
{
    const DWORD ms =
        (timeout.count() < 0) ? INFINITE : static_cast<DWORD>(timeout.count());
    return WaitForSingleObject(hIn(), ms) == WAIT_OBJECT_0;
}

static bool read_console_key_event(KEY_EVENT_RECORD& kev) noexcept
{
    INPUT_RECORD rec{};
    DWORD n = 0;
    while(true)
    {
        if(!ReadConsoleInputW(hIn(), &rec, 1, &n) || n != 1)
            return false;
        if(rec.EventType == KEY_EVENT)
        {
            kev = rec.Event.KeyEvent;
            return true;
        }
    }
}

static bool read_input_byte(char& c) noexcept
{
    DWORD n = 0;
    return ReadFile(hIn(), &c, 1, &n, nullptr) != 0 && n == 1;
}

static int map_windows_key(const KEY_EVENT_RECORD& k) noexcept
{
    if(!k.bKeyDown)
        return -1;

    const WORD vk = k.wVirtualKeyCode;
    const WCHAR wc = k.uChar.UnicodeChar;
    const DWORD mods = k.dwControlKeyState;

    if(vk == VK_TAB && (mods & SHIFT_PRESSED))
        return keyCode(control::ControlKey::SHIFT_TAB);

    switch(vk)
    {
    case VK_UP:
        return keyCode(navigation::NavigationKey::ARROW_UP);
    case VK_DOWN:
        return keyCode(navigation::NavigationKey::ARROW_DOWN);
    case VK_LEFT:
        return keyCode(navigation::NavigationKey::ARROW_LEFT);
    case VK_RIGHT:
        return keyCode(navigation::NavigationKey::ARROW_RIGHT);
    case VK_HOME:
        return keyCode(navigation::NavigationKey::HOME);
    case VK_END:
        return keyCode(navigation::NavigationKey::END);
    case VK_PRIOR:
        return keyCode(navigation::NavigationKey::PAGE_UP);
    case VK_NEXT:
        return keyCode(navigation::NavigationKey::PAGE_DOWN);
    case VK_DELETE:
        return keyCode(navigation::NavigationKey::DELETE_KEY);
    case VK_ESCAPE:
        return keyCode(control::ControlKey::ESC);
    case VK_RETURN:
        return keyCode(control::ControlKey::ENTER);
    case VK_BACK:
        return keyCode(control::ControlKey::BACKSPACE);
    default:
        break;
    }

    if(wc != 0)
    {
        if(wc <= 0xFF)
            return static_cast<unsigned char>(wc);
        return -1; // non-ASCII ignored for now
    }

    return -1;
}

static int read_vt_key_stream(milliseconds timeout)
{
    if(!wait_stdin(timeout))
        return -1;

    char c = 0;
    if(!read_input_byte(c))
        return -1;

    if(c == '\x1b')
    {
        if(!wait_stdin(milliseconds(50)))
            return keyCode(control::ControlKey::ESC);

        std::array<char, 5> seq{};
        if(!read_input_byte(seq[0]))
            return keyCode(control::ControlKey::ESC);
        if(!read_input_byte(seq[1]))
            return keyCode(control::ControlKey::ESC);

        if(seq[0] == '[')
        {
            if(seq[1] >= '0' && seq[1] <= '9')
            {
                if(!read_input_byte(seq[2]))
                    return keyCode(control::ControlKey::ESC);

                if(seq[2] == '~')
                {
                    switch(seq[1])
                    {
                    case '1':
                        return keyCode(navigation::NavigationKey::HOME);
                    case '3':
                        return keyCode(navigation::NavigationKey::DELETE_KEY);
                    case '4':
                        return keyCode(navigation::NavigationKey::END);
                    case '5':
                        return keyCode(navigation::NavigationKey::PAGE_UP);
                    case '6':
                        return keyCode(navigation::NavigationKey::PAGE_DOWN);
                    case '7':
                        return keyCode(navigation::NavigationKey::HOME);
                    case '8':
                        return keyCode(navigation::NavigationKey::END);
                    }
                }
                else if(seq[2] == ';')
                {
                    if(!read_input_byte(seq[3]))
                        return keyCode(control::ControlKey::ESC);
                    if(!read_input_byte(seq[4]))
                        return keyCode(control::ControlKey::ESC);

                    if(seq[4] == 'Z')
                        return keyCode(control::ControlKey::SHIFT_TAB);

                    switch(seq[4])
                    {
                    case 'A':
                        return keyCode(navigation::NavigationKey::ARROW_UP);
                    case 'B':
                        return keyCode(navigation::NavigationKey::ARROW_DOWN);
                    case 'C':
                        return keyCode(navigation::NavigationKey::ARROW_RIGHT);
                    case 'D':
                        return keyCode(navigation::NavigationKey::ARROW_LEFT);
                    }
                }
            }
            else if(seq[1] == 'Z')
            {
                return keyCode(control::ControlKey::SHIFT_TAB);
            }
            else
            {
                switch(seq[1])
                {
                case 'A':
                    return keyCode(navigation::NavigationKey::ARROW_UP);
                case 'B':
                    return keyCode(navigation::NavigationKey::ARROW_DOWN);
                case 'C':
                    return keyCode(navigation::NavigationKey::ARROW_RIGHT);
                case 'D':
                    return keyCode(navigation::NavigationKey::ARROW_LEFT);
                case 'H':
                    return keyCode(navigation::NavigationKey::HOME);
                case 'F':
                    return keyCode(navigation::NavigationKey::END);
                }
            }
        }
        else if(seq[0] == 'O')
        {
            switch(seq[1])
            {
            case 'H':
                return keyCode(navigation::NavigationKey::HOME);
            case 'F':
                return keyCode(navigation::NavigationKey::END);
            }
        }

        return keyCode(control::ControlKey::ESC);
    }

    return static_cast<unsigned char>(c);
}

#else // POSIX

static bool wait_stdin(milliseconds timeout) noexcept
{
    pollfd pfd{};
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;

    const int ms =
        (timeout.count() < 0) ? -1 : static_cast<int>(timeout.count());
    const int r = ::poll(&pfd, 1, ms);
    return r > 0 && (pfd.revents & POLLIN);
}

static bool read_byte(char& c) noexcept
{
    return ::read(STDIN_FILENO, &c, 1) == 1;
}

static int read_vt_key_stream(milliseconds timeout)
{
    if(!wait_stdin(timeout))
        return -1;

    char c = 0;
    if(!read_byte(c))
        return -1;

    if(c == '\x1b')
    {
        if(!wait_stdin(milliseconds(50)))
            return keyCode(control::ControlKey::ESC);

        std::array<char, 5> seq{};
        if(!read_byte(seq[0]))
            return keyCode(control::ControlKey::ESC);
        if(!read_byte(seq[1]))
            return keyCode(control::ControlKey::ESC);

        if(seq[0] == '[')
        {
            if(seq[1] >= '0' && seq[1] <= '9')
            {
                if(!read_byte(seq[2]))
                    return keyCode(control::ControlKey::ESC);

                if(seq[2] == '~')
                {
                    switch(seq[1])
                    {
                    case '1':
                        return keyCode(navigation::NavigationKey::HOME);
                    case '3':
                        return keyCode(navigation::NavigationKey::DELETE_KEY);
                    case '4':
                        return keyCode(navigation::NavigationKey::END);
                    case '5':
                        return keyCode(navigation::NavigationKey::PAGE_UP);
                    case '6':
                        return keyCode(navigation::NavigationKey::PAGE_DOWN);
                    case '7':
                        return keyCode(navigation::NavigationKey::HOME);
                    case '8':
                        return keyCode(navigation::NavigationKey::END);
                    }
                }
                else if(seq[2] == ';')
                {
                    if(!read_byte(seq[3]))
                        return keyCode(control::ControlKey::ESC);
                    if(!read_byte(seq[4]))
                        return keyCode(control::ControlKey::ESC);

                    if(seq[4] == 'Z')
                        return keyCode(control::ControlKey::SHIFT_TAB);

                    switch(seq[4])
                    {
                    case 'A':
                        return keyCode(navigation::NavigationKey::ARROW_UP);
                    case 'B':
                        return keyCode(navigation::NavigationKey::ARROW_DOWN);
                    case 'C':
                        return keyCode(navigation::NavigationKey::ARROW_RIGHT);
                    case 'D':
                        return keyCode(navigation::NavigationKey::ARROW_LEFT);
                    }
                }
            }
            else if(seq[1] == 'Z')
            {
                return keyCode(control::ControlKey::SHIFT_TAB);
            }
            else
            {
                switch(seq[1])
                {
                case 'A':
                    return keyCode(navigation::NavigationKey::ARROW_UP);
                case 'B':
                    return keyCode(navigation::NavigationKey::ARROW_DOWN);
                case 'C':
                    return keyCode(navigation::NavigationKey::ARROW_RIGHT);
                case 'D':
                    return keyCode(navigation::NavigationKey::ARROW_LEFT);
                case 'H':
                    return keyCode(navigation::NavigationKey::HOME);
                case 'F':
                    return keyCode(navigation::NavigationKey::END);
                }
            }
        }
        else if(seq[0] == 'O')
        {
            switch(seq[1])
            {
            case 'H':
                return keyCode(navigation::NavigationKey::HOME);
            case 'F':
                return keyCode(navigation::NavigationKey::END);
            }
        }

        return keyCode(control::ControlKey::ESC);
    }

    return static_cast<unsigned char>(c);
}

#endif

} // namespace

void Terminal::enableRawMode()
{
    if(rawModeEnabled)
        return;

#if defined(UVIM_TERMINAL_WIN32)
    enable_vt_and_raw_console();
    atexit(restoreTerminal);
    rawModeEnabled = true;
#else
    tcgetattr(STDIN_FILENO, &originalTermios);
    atexit(restoreTerminal);

    termios raw = originalTermios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    rawModeEnabled = true;

    // Disable mouse reporting and use alternate screen to avoid scrollback.
    write("\x1b[?1049h");
    write("\x1b[?1000l\x1b[?1002l\x1b[?1003l\x1b[?1004l");
    write("\x1b[?1005l\x1b[?1006l\x1b[?1015l");
#endif
}

void Terminal::disableRawMode()
{
    if(!rawModeEnabled)
        return;

#if defined(UVIM_TERMINAL_WIN32)
    restore_console_modes();
    rawModeEnabled = false;
#else
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalTermios);
    rawModeEnabled = false;

    // Restore normal screen and ensure mouse reporting stays disabled.
    write("\x1b[?25h");  // Show cursor
    write("\x1b[0 q");   // Reset cursor style to default
    write("\x1b[?1000l\x1b[?1002l\x1b[?1003l\x1b[?1004l");
    write("\x1b[?1005l\x1b[?1006l\x1b[?1015l");
    write("\x1b[?1049l");
#endif
}

void Terminal::restoreTerminal()
{
    disableRawMode();
}

void Terminal::clearScreen()
{
    write("\x1b[2J");
    write("\x1b[H");
    flush();
}

void Terminal::clearLine()
{
    write("\x1b[K");
}

void Terminal::moveCursor(int row, int col)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
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
#if defined(UVIM_TERMINAL_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if(GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
    {
        cols = static_cast<int>(info.srWindow.Right - info.srWindow.Left + 1);
        rows = static_cast<int>(info.srWindow.Bottom - info.srWindow.Top + 1);
        return;
    }
    rows = 24;
    cols = 80;
#else
    // 1) Primary: ioctl on stdout
    winsize ws{};
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 &&
       ws.ws_row > 0)
    {
        cols = static_cast<int>(ws.ws_col);
        rows = static_cast<int>(ws.ws_row);
        return;
    }

    // 2) Secondary: environment variables (often set by shells/terminals)
    auto parse_env_int = [](const char* name) -> int
    {
        const char* v = std::getenv(name);
        if(!v || !*v)
            return 0;
        char* end = nullptr;
        long n = std::strtol(v, &end, 10);
        if(end == v || n <= 0 || n > 10000)
            return 0;
        return static_cast<int>(n);
    };

    const int envCols = parse_env_int("COLUMNS");
    const int envRows = parse_env_int("LINES");
    if(envCols > 0 && envRows > 0)
    {
        cols = envCols;
        rows = envRows;
        return;
    }

    // 3) Last resort
    cols = 80;
    rows = 24;
#endif
}

void Terminal::write(const std::string& str)
{
#if defined(UVIM_TERMINAL_WIN32)
    if(output_is_console())
    {
        std::wstring wide = utf8_to_utf16(str);
        if(!wide.empty())
        {
            DWORD written = 0;
            (void)WriteConsoleW(hOut(), wide.data(), static_cast<DWORD>(wide.size()),
                                &written, nullptr);
            return;
        }
    }
    DWORD written = 0;
    (void)WriteFile(hOut(), str.data(), static_cast<DWORD>(str.size()), &written,
                    nullptr);
#else
    ::write(STDOUT_FILENO, str.c_str(), str.length());
#endif
}

void Terminal::write(char c)
{
#if defined(UVIM_TERMINAL_WIN32)
    write(std::string(1, c));
#else
    ::write(STDOUT_FILENO, &c, 1);
#endif
}

void Terminal::flush()
{
    std::fflush(stdout);
#if defined(UVIM_TERMINAL_WIN32)
    FlushFileBuffers(GetStdHandle(STD_OUTPUT_HANDLE));
#endif
}

bool Terminal::isTmux()
{
    const char* tmux = std::getenv("TMUX");
    return tmux && *tmux;
}

bool Terminal::useSynchronizedOutput()
{
#if defined(UVIM_TERMINAL_WIN32)
    return false;
#else
    // Allow explicit opt-out while keeping tmux flicker fix on by default.
    const char* disabled = std::getenv("UVIM_DISABLE_SYNC_OUTPUT");
    if(disabled && *disabled && std::string(disabled) != "0")
        return false;
    return isTmux();
#endif
}

void Terminal::unreadKey(int key)
{
    keyBuffer.push_front(key);
}

bool Terminal::hasBufferedKeys()
{
    return !keyBuffer.empty();
}

int Terminal::readKeyInternal(int timeoutMs)
{
    const auto timeout =
        (timeoutMs >= 0) ? milliseconds(timeoutMs) : milliseconds(-1);

#if defined(UVIM_TERMINAL_WIN32)
    if(!input_is_console())
        return read_vt_key_stream(timeout);

    if(!wait_stdin(timeout))
        return -1;

    KEY_EVENT_RECORD kev{};
    while(read_console_key_event(kev))
    {
        const int k = map_windows_key(kev);
        if(k != -1)
            return k;
    }
    return -1;

#else
    return read_vt_key_stream(timeout);
#endif
}

int Terminal::readKey()
{
    if(!keyBuffer.empty())
    {
        const int key = keyBuffer.front();
        keyBuffer.pop_front();
        return key;
    }
    // On Windows/ConPTY, readKeyInternal can return -1 when only non-character
    // events (key-up, focus, resize) were in the console input buffer.  Retry
    // until a real key arrives — this is an infinite-timeout call so -1 never
    // means "timed out".
    int k;
    do
    {
        k = readKeyInternal(-1);
    } while(k < 0);
    return k;
}

int Terminal::readKeyTimeout(int timeoutMs)
{
    if(!keyBuffer.empty())
    {
        const int key = keyBuffer.front();
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
        std::snprintf(buf, sizeof(buf), "\x1b[%dm", 30 + fg);
        write(buf);
    }
    if(bg >= 0)
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "\x1b[%dm", 40 + bg);
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
    write("\x1b[2 q");
}
void Terminal::setCursorBarBlinking()
{
    write("\x1b[5 q");
}

std::string Terminal::cursorPos(int row, int col)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
    return std::string(buf);
}

std::string Terminal::scrollRegion(int top, int bottom)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "\x1b[%d;%dr", top, bottom);
    return std::string(buf);
}
