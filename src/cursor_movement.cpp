#include "editor.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace
{

// ---------- ASCII helpers (constexpr, locale-free) ----------
constexpr char ascii_tolower(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

constexpr bool is_space(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
           c == '\v';
}

constexpr bool is_digit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

constexpr bool is_alpha(char c) noexcept
{
    c = ascii_tolower(c);
    return c >= 'a' && c <= 'z';
}

constexpr bool is_alnum(char c) noexcept
{
    return is_alpha(c) || is_digit(c);
}

constexpr bool is_word_char(char c) noexcept
{
    return is_alnum(c) || c == '_';
}

// ---------- line helpers ----------
inline int line_count(const std::vector<std::string>& ls) noexcept
{
    return static_cast<int>(ls.size());
}

inline int line_len(const std::vector<std::string>& ls, int y) noexcept
{
    if(y < 0 || y >= line_count(ls))
        return 0;
    return static_cast<int>(ls[static_cast<size_t>(y)].size());
}

inline std::string_view line_view(const std::vector<std::string>& ls,
                                  int y) noexcept
{
    if(y < 0 || y >= line_count(ls))
        return {};
    return std::string_view{ls[static_cast<size_t>(y)]};
}

} // namespace

void Editor::moveLeft(int count)
{
    if(!lines || lines->empty())
        return;

    int& cx = *cursorX;
    int& cy = *cursorY;
    int& wx = *wantedX;

    while(count-- > 0)
    {
        if(cx > 0)
        {
            --cx;
        }
        else if(cy > 0)
        {
            --cy;
            cx = line_len(*lines, cy);
        }
        else
        {
            break;
        }
    }

    wx = cx;
}

void Editor::moveRight(int count)
{
    if(!lines || lines->empty())
        return;

    int& cx = *cursorX;
    int& cy = *cursorY;
    int& wx = *wantedX;

    const int n = line_count(*lines);
    cy = std::clamp(cy, 0, n - 1);

    while(count-- > 0)
    {
        const int L = line_len(*lines, cy);
        if(cx < L)
        {
            ++cx;
        }
        else if(cy < n - 1)
        {
            ++cy;
            cx = 0;
        }
        else
        {
            break;
        }
    }

    wx = cx;
}

void Editor::moveUp(int count)
{
    if(!lines || lines->empty())
        return;

    int& cx = *cursorX;
    int& cy = *cursorY;
    const int& wx = *wantedX;

    const int n = line_count(*lines);
    cy = std::clamp(cy, 0, n - 1);

    cy = std::max(0, cy - std::max(0, count));
    cx = std::min(wx, line_len(*lines, cy));
}

void Editor::moveDown(int count)
{
    if(!lines || lines->empty())
        return;

    int& cx = *cursorX;
    int& cy = *cursorY;
    const int& wx = *wantedX;

    const int n = line_count(*lines);
    cy = std::clamp(cy, 0, n - 1);

    cy = std::min(n - 1, cy + std::max(0, count));
    cx = std::min(wx, line_len(*lines, cy));
}

void Editor::moveWordForward()
{
    if(!lines || lines->empty())
        return;

    int y = std::clamp(*cursorY, 0, line_count(*lines) - 1);
    int x = std::clamp(*cursorX, 0, line_len(*lines, y));

    const int n = line_count(*lines);

    while(true)
    {
        std::string_view ln = line_view(*lines, y);
        const int L = static_cast<int>(ln.size());

        // At end of line: go to next line, skip leading spaces
        if(x >= L)
        {
            if(y + 1 >= n)
                break;
            ++y;
            x = 0;

            ln = line_view(*lines, y);
            while(x < static_cast<int>(ln.size()) && is_space(ln[x]))
                ++x;
            break;
        }

        const char c = ln[x];

        // If on whitespace: skip whitespace -> lands on next token start
        if(is_space(c))
        {
            while(x < L && is_space(ln[x]))
                ++x;
            break;
        }

        // Otherwise: consume current "word class" (word vs non-word
        // operator-ish)
        const bool alphaWord = is_word_char(c);
        ++x;

        while(x < L)
        {
            const char d = ln[x];
            if(is_space(d))
                break;
            if(alphaWord != is_word_char(d))
                break;
            ++x;
        }
        break;
    }

    *cursorY = y;
    *cursorX = x;
    *wantedX = x;
}

void Editor::moveWordBackward()
{
    if(!lines || lines->empty())
        return;

    int y = std::clamp(*cursorY, 0, line_count(*lines) - 1);
    int x = std::clamp(*cursorX, 0, line_len(*lines, y));

    if(y == 0 && x == 0)
        return;

    if(x == 0)
    {
        --y;
        x = line_len(*lines, y);
    }

    std::string_view ln = line_view(*lines, y);

    if(x > 0)
        --x;

    while(x > 0 && is_space(ln[x]))
        --x;

    if(!ln.empty() && !is_space(ln[x]))
    {
        const bool alphaWord = is_word_char(ln[x]);

        while(x > 0)
        {
            const char prev = ln[x - 1];
            if(is_space(prev))
                break;
            if(alphaWord != is_word_char(prev))
                break;
            --x;
        }
    }

    *cursorY = y;
    *cursorX = x;
    *wantedX = x;
}

void Editor::moveToEndOfWord()
{
    if(!lines || lines->empty())
        return;

    int y = std::clamp(*cursorY, 0, line_count(*lines) - 1);
    int x = std::clamp(*cursorX, 0, line_len(*lines, y));
    const int n = line_count(*lines);

    auto advance_to_word_end_on_line = [&](std::string_view ln, int& xx)
    {
        const int L = static_cast<int>(ln.size());

        while(xx < L && is_space(ln[xx]))
            ++xx;
        if(xx >= L)
            return;

        const bool alphaWord = is_word_char(ln[xx]);
        while(xx < L - 1)
        {
            const char next = ln[xx + 1];
            if(is_space(next))
                break;
            if(alphaWord != is_word_char(next))
                break;
            ++xx;
        }
    };

    std::string_view ln = line_view(*lines, y);
    const int L = static_cast<int>(ln.size());

    if(x >= L - 1)
    {
        if(y + 1 < n)
        {
            ++y;
            x = 0;
            ln = line_view(*lines, y);
            advance_to_word_end_on_line(ln, x);
        }
    }
    else
    {
        ++x;
        ln = line_view(*lines, y);
        advance_to_word_end_on_line(ln, x);
    }

    *cursorY = y;
    *cursorX = x;
    *wantedX = x;
}

void Editor::moveToLineStart()
{
    *cursorX = 0;
    *wantedX = 0;
}

void Editor::moveToLineEnd()
{
    if(!lines || lines->empty())
        return;

    int& cx = *cursorX;
    int& cy = *cursorY;
    int& wx = *wantedX;

    cy = std::clamp(cy, 0, line_count(*lines) - 1);

    cx = line_len(*lines, cy);
    if(cx > 0 && currentMode == NORMAL)
        --cx;

    wx = cx;
}

void Editor::moveToFirstLine()
{
    *cursorY = 0;
    *cursorX = 0;
    *wantedX = 0;
}

void Editor::moveToLastLine()
{
    if(!lines || lines->empty())
        return;

    *cursorY = line_count(*lines) - 1;
    *cursorX = 0;
    *wantedX = 0;
}

void Editor::moveToLine(int line)
{
    if(!lines || lines->empty())
        return;

    *cursorY = std::clamp(line, 0, line_count(*lines) - 1);
    *cursorX = 0;
    *wantedX = 0;
}

void Editor::jumpForward()
{
    if(jumpForwardStack.empty())
    {
        setStatusMessage("Jump stack empty");
        return;
    }

    JumpLocation current;
    current.bufferIndex = currentBufferIndex;
    current.cursorX = *cursorX;
    current.cursorY = *cursorY;
    current.offsetX = *offsetX;
    current.offsetY = *offsetY;

    jumpBackStack.push_back(current);

    JumpLocation target = jumpForwardStack.back();
    jumpForwardStack.pop_back();

    restoreJumpLocation(target);
}

void Editor::jumpBack()
{
    if(jumpBackStack.empty())
    {
        setStatusMessage("Jump stack empty");
        return;
    }

    JumpLocation current;
    current.bufferIndex = currentBufferIndex;
    current.cursorX = *cursorX;
    current.cursorY = *cursorY;
    current.offsetX = *offsetX;
    current.offsetY = *offsetY;

    jumpForwardStack.push_back(current);

    JumpLocation target = jumpBackStack.back();
    jumpBackStack.pop_back();

    restoreJumpLocation(target);
}

void Editor::pushJumpLocation()
{
    if(!currentBuffer)
        return;

    JumpLocation loc;
    loc.bufferIndex = currentBufferIndex;
    loc.cursorX = *cursorX;
    loc.cursorY = *cursorY;
    loc.offsetX = *offsetX;
    loc.offsetY = *offsetY;

    jumpBackStack.push_back(loc);
    jumpForwardStack.clear();
}

void Editor::restoreJumpLocation(const JumpLocation& loc)
{
    if(loc.bufferIndex < 0 ||
       loc.bufferIndex >= static_cast<int>(buffers.size()))
        return;

    switchToBuffer(loc.bufferIndex);

    *cursorX = loc.cursorX;
    *cursorY = loc.cursorY;
    *offsetX = loc.offsetX;
    *offsetY = loc.offsetY;

    needsFullRedraw = true;
}

void Editor::scrollHalfPageDown(bool visual)
{
    const int half = screenRows / 2;
    moveDown(half);
    adjustViewport();
    if(visual)
        updateVisualSelection();
}

void Editor::scrollHalfPageUp(bool visual)
{
    const int half = screenRows / 2;
    moveUp(half);
    adjustViewport();
    if(visual)
        updateVisualSelection();
}

void Editor::moveToMatchingBracket()
{
    if(!lines || lines->empty())
        return;

    const int n = line_count(*lines);
    int y = std::clamp(*cursorY, 0, n - 1);

    std::string_view ln = line_view(*lines, y);
    if(ln.empty())
        return;

    int x = std::clamp(*cursorX, 0, static_cast<int>(ln.size()) - 1);

    const char c = ln[x];

    constexpr std::array<std::pair<char, char>, 3> pairs{{
        {'(', ')'},
        {'{', '}'},
        {'[', ']'},
    }};

    char open = 0, close = 0;
    for(auto [o, cl] : pairs)
    {
        if(c == o)
        {
            open = o;
            close = cl;
            break;
        }
        if(c == cl)
        {
            open = o;
            close = cl;
            break;
        }
    }
    if(!open)
        return;

    const int direction = (c == open) ? +1 : -1;
    int depth = 0;

    while(true)
    {
        x += direction;

        // Walk across lines
        while(true)
        {
            if(y < 0 || y >= n)
                return;

            ln = line_view(*lines, y);
            const int L = static_cast<int>(ln.size());

            if(direction == +1)
            {
                if(x < L)
                    break;
                ++y;
                x = 0;
            }
            else
            {
                if(x >= 0)
                    break;
                --y;
                if(y < 0)
                    return;
                ln = line_view(*lines, y);
                x = static_cast<int>(ln.size()) - 1;
            }
        }

        const char ch = ln[x];

        if(ch == c)
            ++depth;
        else if(ch == (direction == +1 ? close : open))
        {
            if(depth == 0)
            {
                *cursorY = y;
                *cursorX = x;
                *wantedX = x;
                return;
            }
            --depth;
        }
    }
}

void Editor::adjustViewport()
{
    if(!lines || lines->empty())
    {
        *offsetX = *offsetY = 0;
        return;
    }

    const int n = line_count(*lines);
    const int maxOffsetY = std::max(0, n - screenRows);
    const int maxOffsetX =
        std::max(0, 1000000000); // optional; horizontal clamp is app-specific

    int& ox = *offsetX;
    int& oy = *offsetY;
    const int cx = *cursorX;
    const int cy = *cursorY;

    // Vertical
    if(cy < oy)
        oy = cy;
    else if(cy >= oy + screenRows)
        oy = cy - screenRows + 1;
    oy = std::clamp(oy, 0, maxOffsetY);

    // Horizontal
    if(cx < ox)
        ox = cx;
    else if(cx >= ox + screenCols)
        ox = cx - screenCols + 1;
    ox = std::clamp(ox, 0, maxOffsetX);
}

void Editor::centerScreen()
{
    if(!lines || lines->empty())
    {
        *offsetY = 0;
        return;
    }

    const int n = line_count(*lines);
    const int maxOffsetY = std::max(0, n - screenRows);

    *offsetY = std::clamp(*cursorY - screenRows / 2, 0, maxOffsetY);
}
