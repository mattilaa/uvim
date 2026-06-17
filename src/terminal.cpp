#include "terminal.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
std::string Terminal::lastPasteText;

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

static void enable_vt_and_raw_console()
{
    DWORD inMode = 0;
    if(GetConsoleMode(hIn(), &inMode))
    {
        g_origInMode = inMode;
        inMode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT |
                    ENABLE_PROCESSED_INPUT);
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
#ifdef ENABLE_WRAP_AT_EOL_OUTPUT
        outMode &= ~ENABLE_WRAP_AT_EOL_OUTPUT;
#endif
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

static bool queued_console_input_events(DWORD& count) noexcept
{
    count = 0;
    return GetNumberOfConsoleInputEvents(hIn(), &count) != 0;
}

static bool read_console_input_event(INPUT_RECORD& rec) noexcept
{
    DWORD n = 0;
    return ReadConsoleInputW(hIn(), &rec, 1, &n) && n == 1;
}

static bool write_console_utf8(std::string_view str) noexcept
{
    if(str.empty())
        return true;

    DWORD mode = 0;
    HANDLE out = hOut();
    if(!GetConsoleMode(out, &mode))
        return false;

    if(str.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        return false;

    int wideLen =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.data(),
                            static_cast<int>(str.size()), nullptr, 0);
    if(wideLen <= 0)
        return false;

    std::wstring wide(static_cast<size_t>(wideLen), L'\0');
    wideLen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.data(),
                                  static_cast<int>(str.size()), wide.data(),
                                  wideLen);
    if(wideLen <= 0)
        return false;

    DWORD written = 0;
    return WriteConsoleW(out, wide.data(), static_cast<DWORD>(wide.size()),
                         &written, nullptr) != 0;
}

static int map_windows_key(const KEY_EVENT_RECORD& k) noexcept
{
    if(!k.bKeyDown)
        return -1;

    const WORD vk = k.wVirtualKeyCode;
    const WCHAR wc = k.uChar.UnicodeChar;
    const DWORD mods = k.dwControlKeyState;
    const bool ctrl = (mods & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    const bool shift = (mods & SHIFT_PRESSED) != 0;

    if(vk == VK_TAB && shift)
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

    if(ctrl && vk >= 'A' && vk <= 'Z')
    {
        if(shift)
        {
            if(vk == 'H')
                return keyCode(control::ControlKey::SHIFT_CTRL_H);
            if(vk == 'L')
                return keyCode(control::ControlKey::SHIFT_CTRL_L);
            if(vk == 'X')
                return keyCode(control::ControlKey::SHIFT_CTRL_X);
        }
        return vk - 'A' + 1;
    }

    if(wc != 0)
    {
        if(wc <= 0xFF)
            return static_cast<unsigned char>(wc);
        return -1; // non-ASCII ignored for now
    }

    return -1;
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

static FILE* keylogFile()
{
    static FILE* fp = []() -> FILE*
    {
        const char* path = std::getenv("UVIM_KEYLOG");
        if(!path || !*path)
            return nullptr;
        FILE* f = std::fopen(path, "a");
        if(f)
            std::setvbuf(f, nullptr, _IOLBF, 0);
        return f;
    }();
    return fp;
}

static bool read_byte(char& c) noexcept
{
    if(::read(STDIN_FILENO, &c, 1) != 1)
        return false;
    if(FILE* f = keylogFile())
    {
        unsigned char ub = static_cast<unsigned char>(c);
        if(ub == 0x1b)
            std::fputs("\\e", f);
        else if(ub >= 0x20 && ub < 0x7f)
            std::fputc(c, f);
        else
            std::fprintf(f, "<%02x>", ub);
    }
    return true;
}

static bool read_byte_blocking(char& c) noexcept
{
    if(!wait_stdin(milliseconds(-1)))
        return false;
    return read_byte(c);
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
    write("\x1b[?2004h");
    // Ask terminal to report modifier combos so Ctrl+Shift+letter is
    // distinguishable. Two requests cover most terminals:
    //   - xterm modifyOtherKeys=2:   CSI 27 ; mod ; key ~
    //   - kitty keyboard protocol:   CSI key ; mod u
    // The parser handles both formats and falls back to the natural keycode
    // for combos it doesn't specifically map.
    write("\x1b[>4;2m");
    write("\x1b[>1u");
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
    write("\x1b[?2004l");
    write("\x1b[>4;0m");
    write("\x1b[<u");
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
    if(write_console_utf8(str))
        return;
    DWORD written = 0;
    (void)WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), str.data(),
                    static_cast<DWORD>(str.size()), &written, nullptr);
#else
    ::write(STDOUT_FILENO, str.c_str(), str.length());
#endif
}

void Terminal::write(char c)
{
#if defined(UVIM_TERMINAL_WIN32)
    char buf[1] = {c};
    if(write_console_utf8(std::string_view(buf, 1)))
        return;
    DWORD written = 0;
    (void)WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), &c, 1, &written, nullptr);
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

std::string Terminal::takeLastPasteText()
{
    std::string text = std::move(lastPasteText);
    lastPasteText.clear();
    return text;
}

#ifdef UVIM_TESTING
void Terminal::setLastPasteTextForTests(std::string text)
{
    lastPasteText = std::move(text);
}
#endif

int Terminal::readKeyInternal(int timeoutMs)
{
    const auto timeout =
        (timeoutMs >= 0) ? milliseconds(timeoutMs) : milliseconds(-1);

#if defined(UVIM_TERMINAL_WIN32)
    if(!wait_stdin(timeout))
        return -1;

    while(true)
    {
        DWORD queued = 0;
        if(!queued_console_input_events(queued))
            return -1;
        if(queued == 0)
        {
            if(timeoutMs >= 0)
                return -1;
            if(!wait_stdin(milliseconds(-1)))
                return -1;
            continue;
        }

        INPUT_RECORD rec{};
        if(!read_console_input_event(rec))
            return -1;
        if(rec.EventType != KEY_EVENT)
        {
            if(timeoutMs >= 0)
            {
                DWORD remaining = 0;
                if(!queued_console_input_events(remaining) || remaining == 0)
                    return -1;
            }
            continue;
        }

        const int k = map_windows_key(rec.Event.KeyEvent);
        if(k != -1)
            return k;
        if(timeoutMs >= 0)
        {
            DWORD remaining = 0;
            if(!queued_console_input_events(remaining) || remaining == 0)
                return -1;
        }
    }

#else
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
                // Read parameter bytes (digits + ';') until a final byte.
                // Handles legacy single-param CSI Pn ~, CSI 1;mod letter for
                // modified arrows, xterm modifyOtherKeys CSI 27;mod;key ~,
                // and kitty CSI key;mod u.
                std::string params;
                params += seq[1];
                char final_byte = 0;
                for(int i = 0; i < 32; ++i)
                {
                    char b = 0;
                    if(!read_byte(b))
                        return keyCode(control::ControlKey::ESC);
                    if((b >= '0' && b <= '9') || b == ';' || b == ':')
                    {
                        params += b;
                        continue;
                    }
                    final_byte = b;
                    break;
                }
                if(final_byte == 0)
                    return keyCode(control::ControlKey::ESC);

                std::vector<int> ps;
                {
                    std::string tok;
                    for(char b : params)
                    {
                        if(b == ';' || b == ':')
                        {
                            ps.push_back(tok.empty() ? 0
                                                     : std::atoi(tok.c_str()));
                            tok.clear();
                        }
                        else
                        {
                            tok += b;
                        }
                    }
                    ps.push_back(tok.empty() ? 0 : std::atoi(tok.c_str()));
                }

                // Decode a (modifier, key) pair into our keycodes. Falls back
                // to a sensible plain code so unexpected modifyOtherKeys
                // sequences don't break basic editing.
                auto decode = [](int mod, int keyCh) -> int
                {
                    int lower = keyCh;
                    if(lower >= 'A' && lower <= 'Z')
                        lower = lower - 'A' + 'a';
                    if(mod == 6) // ctrl+shift
                    {
                        if(lower == 'h')
                            return keyCode(
                                control::ControlKey::SHIFT_CTRL_H);
                        if(lower == 'l')
                            return keyCode(
                                control::ControlKey::SHIFT_CTRL_L);
                        if(lower == 'x')
                            return keyCode(
                                control::ControlKey::SHIFT_CTRL_X);
                    }
                    // Fallback: synthesize the natural code.
                    if(mod == 5 && lower >= 'a' && lower <= 'z')
                        return lower - 'a' + 1; // ctrl-letter
                    if(mod == 1 || mod == 2)
                        return keyCh;
                    return keyCh;
                };

                if(final_byte == '~')
                {
                    if(ps.size() >= 1 && ps[0] == 200)
                    {
                        lastPasteText.clear();
                        std::string tail;
                        tail.reserve(6);
                        while(true)
                        {
                            char b = 0;
                            if(!read_byte_blocking(b))
                                break;
                            tail.push_back(b);
                            if(tail.size() > 6)
                            {
                                lastPasteText.push_back(tail.front());
                                tail.erase(tail.begin());
                            }
                            if(tail == "\x1b[201~")
                            {
                                break;
                            }
                        }
                        return keyCode(control::ControlKey::PASTE);
                    }
                    if(ps.size() >= 1 && ps[0] == 201)
                        return keyCode(control::ControlKey::ESC);
                    if(ps.size() == 3 && ps[0] == 27)
                    {
                        return decode(ps[1], ps[2]);
                    }
                    if(ps.size() >= 1)
                    {
                        switch(ps[0])
                        {
                        case 1:
                            return keyCode(navigation::NavigationKey::HOME);
                        case 3:
                            return keyCode(
                                navigation::NavigationKey::DELETE_KEY);
                        case 4:
                            return keyCode(navigation::NavigationKey::END);
                        case 5:
                            return keyCode(
                                navigation::NavigationKey::PAGE_UP);
                        case 6:
                            return keyCode(
                                navigation::NavigationKey::PAGE_DOWN);
                        case 7:
                            return keyCode(navigation::NavigationKey::HOME);
                        case 8:
                            return keyCode(navigation::NavigationKey::END);
                        }
                    }
                    return keyCode(control::ControlKey::ESC);
                }
                if(final_byte == 'u')
                {
                    // kitty CSI-u: ps[0] = key, ps[1] = mod
                    if(ps.size() >= 1)
                    {
                        int mod = ps.size() >= 2 ? ps[1] : 1;
                        return decode(mod, ps[0]);
                    }
                    return keyCode(control::ControlKey::ESC);
                }
                if(final_byte == 'Z')
                    return keyCode(control::ControlKey::SHIFT_TAB);
                switch(final_byte)
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
                return keyCode(control::ControlKey::ESC);
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
