#include "editor.h"
#include "text_utils.h"

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
            if(utf8Mode && cy >= 0 && cy < line_count(*lines))
            {
                std::string_view ln = line_view(*lines, cy);
                cx = text_utils::prevUtf8CharStart(ln, cx);
            }
            else
            {
                --cx;
            }
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
            if(utf8Mode)
            {
                std::string_view ln = line_view(*lines, cy);
                cx = text_utils::nextUtf8CharStart(ln, cx);
            }
            else
            {
                ++cx;
            }
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
    if(utf8Mode && cy >= 0 && cy < line_count(*lines))
    {
        std::string_view ln = line_view(*lines, cy);
        cx = text_utils::prevUtf8CharStart(ln, cx);
    }
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
    if(utf8Mode && cy >= 0 && cy < line_count(*lines))
    {
        std::string_view ln = line_view(*lines, cy);
        cx = text_utils::prevUtf8CharStart(ln, cx);
    }
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
    const int half = contentRows() / 2;
    moveDown(half);
    adjustViewport();
    if(visual)
        updateVisualSelection();
}

void Editor::scrollHalfPageUp(bool visual)
{
    const int half = contentRows() / 2;
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

    // --------------------------------------------------------------------
    // Block comment matching: /* ... */
    //
    // Support hitting '%' on either character of either delimiter:
    //   - '/*' : cursor on '/' or '*'  -> jump forward to matching '*/'
    //   - '*/' : cursor on '*' or '/'  -> jump backward to matching '/*'
    //
    // Note: C/C++ block comments do not nest, so we match the nearest
    // corresponding delimiter in the chosen direction.
    // --------------------------------------------------------------------
    {
        const int L = static_cast<int>(ln.size());
        const char c = ln[x];

        bool onOpen = false;
        int openX = -1; // points at '/' in "/*"
        if(c == '/' && x + 1 < L && ln[x + 1] == '*')
        {
            onOpen = true;
            openX = x;
        }
        else if(c == '*' && x > 0 && ln[x - 1] == '/')
        {
            onOpen = true;
            openX = x - 1;
        }

        bool onClose = false;
        int closeX = -1; // points at '*' in "*/"
        if(c == '*' && x + 1 < L && ln[x + 1] == '/')
        {
            onClose = true;
            closeX = x;
        }
        else if(c == '/' && x > 0 && ln[x - 1] == '*')
        {
            onClose = true;
            closeX = x - 1;
        }

        if(onOpen)
        {
            int yy = y;
            int xx = openX + 2; // start scanning after "/*"

            while(yy < n)
            {
                std::string_view lnv = line_view(*lines, yy);
                const int LL = static_cast<int>(lnv.size());

                for(; xx < LL - 1; ++xx)
                {
                    if(lnv[xx] == '*' && lnv[xx + 1] == '/')
                    {
                        *cursorY = yy;
                        *cursorX = xx; // land on '*'
                        *wantedX = xx;
                        return;
                    }
                }

                ++yy;
                xx = 0;
            }
            return;
        }

        if(onClose)
        {
            int yy = y;
            int xx = closeX - 1; // start scanning before '*'

            while(yy >= 0)
            {
                std::string_view lnv = line_view(*lines, yy);
                const int LL = static_cast<int>(lnv.size());

                if(LL >= 2)
                {
                    int i = std::min(xx, LL - 2);
                    for(; i >= 0; --i)
                    {
                        if(lnv[i] == '/' && lnv[i + 1] == '*')
                        {
                            *cursorY = yy;
                            *cursorX = i; // land on '/'
                            *wantedX = i;
                            return;
                        }
                    }
                }

                --yy;
                if(yy >= 0)
                {
                    const int prevLen = line_len(*lines, yy);
                    xx = prevLen - 2; // last index where i+1 is valid
                }
            }
            return;
        }
    }

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

void Editor::moveToFirstNonBlank()
{
    if(*cursorY >= (int)lines->size())
        return;
    const std::string& line = (*lines)[*cursorY];
    *cursorX = 0;
    while(*cursorX < (int)line.length() &&
          (line[*cursorX] == ' ' || line[*cursorX] == '\t'))
    {
        (*cursorX)++;
    }
    *wantedX = *cursorX;
}

void Editor::moveParagraphForward()
{
    if(lines->empty())
        return;

    // Skip current non-empty lines
    while(*cursorY < (int)lines->size() - 1 && !(*lines)[*cursorY].empty())
    {
        (*cursorY)++;
    }
    // Skip empty lines
    while(*cursorY < (int)lines->size() - 1 && (*lines)[*cursorY].empty())
    {
        (*cursorY)++;
    }
    *cursorX = 0;
    adjustViewport();
}

void Editor::moveParagraphBackward()
{
    if(lines->empty())
        return;

    // Skip current empty lines
    while(*cursorY > 0 && (*lines)[*cursorY].empty())
    {
        (*cursorY)--;
    }
    // Skip non-empty lines
    while(*cursorY > 0 && !(*lines)[*cursorY].empty())
    {
        (*cursorY)--;
    }
    *cursorX = 0;
    adjustViewport();
}

void Editor::moveWordForwardBig()
{
    if(lines->empty() || *cursorY >= (int)lines->size())
        return;

    const std::string& line = (*lines)[*cursorY];

    // Skip current non-whitespace
    while(*cursorX < (int)line.length() && !std::isspace(line[*cursorX]))
    {
        (*cursorX)++;
    }
    // Skip whitespace
    while(*cursorX < (int)line.length() && std::isspace(line[*cursorX]))
    {
        (*cursorX)++;
    }
    // If at end of line, move to next line
    if(*cursorX >= (int)line.length() && *cursorY < (int)lines->size() - 1)
    {
        (*cursorY)++;
        *cursorX = 0;
        const std::string& nextLine = (*lines)[*cursorY];
        while(*cursorX < (int)nextLine.length() &&
              std::isspace(nextLine[*cursorX]))
        {
            (*cursorX)++;
        }
    }
    *wantedX = *cursorX;
}

void Editor::moveWordBackwardBig()
{
    if(lines->empty() || *cursorY >= (int)lines->size())
        return;

    // Move back one if not at start
    if(*cursorX > 0)
        (*cursorX)--;
    else if(*cursorY > 0)
    {
        (*cursorY)--;
        *cursorX = (*lines)[*cursorY].length();
        if(*cursorX > 0)
            (*cursorX)--;
    }

    const std::string& line = (*lines)[*cursorY];

    // Skip whitespace backward
    while(*cursorX > 0 && std::isspace(line[*cursorX]))
    {
        (*cursorX)--;
    }
    // Skip non-whitespace backward
    while(*cursorX > 0 && !std::isspace(line[*cursorX - 1]))
    {
        (*cursorX)--;
    }
    *wantedX = *cursorX;
}

void Editor::moveToEndOfWordBig()
{
    if(lines->empty() || *cursorY >= (int)lines->size())
        return;

    const std::string& line = (*lines)[*cursorY];

    // Move forward one
    if(*cursorX < (int)line.length())
        (*cursorX)++;

    // Skip whitespace
    while(*cursorX < (int)line.length() && std::isspace(line[*cursorX]))
    {
        (*cursorX)++;
    }
    // Move to end of word
    while(*cursorX < (int)line.length() - 1 &&
          !std::isspace(line[*cursorX + 1]))
    {
        (*cursorX)++;
    }
    *wantedX = *cursorX;
}

void Editor::findCharForwardBefore(char c)
{
    if(*cursorY >= (int)lines->size())
        return;
    const std::string& line = (*lines)[*cursorY];

    for(int i = *cursorX + 1; i < (int)line.length(); i++)
    {
        if(line[i] == c)
        {
            *cursorX = i - 1;
            if(*cursorX < 0)
                *cursorX = 0;
            *wantedX = *cursorX;
            return;
        }
    }
}

void Editor::findCharBackwardAfter(char c)
{
    if(*cursorY >= (int)lines->size())
        return;
    const std::string& line = (*lines)[*cursorY];

    for(int i = *cursorX - 1; i >= 0; i--)
    {
        if(line[i] == c)
        {
            *cursorX = i + 1;
            if(*cursorX >= (int)line.length())
                *cursorX = line.length() - 1;
            *wantedX = *cursorX;
            return;
        }
    }
}

// ============================================================================
// Character finding (f/F motions) - base implementations
// These may already exist in another file, but are needed by operator_pending
// ============================================================================

void Editor::findCharForward(char c)
{
    if(*cursorY >= (int)lines->size())
        return;
    const std::string& line = (*lines)[*cursorY];

    for(int i = *cursorX + 1; i < (int)line.length(); i++)
    {
        if(line[i] == c)
        {
            *cursorX = i;
            *wantedX = *cursorX;
            lastFindChar = c;
            lastFindForward = true;
            return;
        }
    }
}

void Editor::findCharBackward(char c)
{
    if(*cursorY >= (int)lines->size())
        return;
    const std::string& line = (*lines)[*cursorY];

    for(int i = *cursorX - 1; i >= 0; i--)
    {
        if(line[i] == c)
        {
            *cursorX = i;
            *wantedX = *cursorX;
            lastFindChar = c;
            lastFindForward = false;
            return;
        }
    }
}

// ============================================================================
// Scrolling Commands
// ============================================================================

void Editor::scrollToTop()
{
    *offsetY = 0;
    *cursorY = 0;
    *cursorX = 0;
    needsFullRedraw = true;
}

void Editor::scrollToBottom()
{
    if(lines->empty())
        return;
    *cursorY = lines->size() - 1;
    *cursorX = 0;
    adjustViewport();
    needsFullRedraw = true;
}

void Editor::scrollPageUp()
{
    int pageSize = contentRows() - 2;
    *cursorY -= pageSize;
    if(*cursorY < 0)
        *cursorY = 0;
    *offsetY -= pageSize;
    if(*offsetY < 0)
        *offsetY = 0;
    if(*cursorY < (int)lines->size())
        *cursorX = std::min(*wantedX, (int)(*lines)[*cursorY].length());
    needsFullRedraw = true;
}

void Editor::scrollPageDown()
{
    int pageSize = contentRows() - 2;
    *cursorY += pageSize;
    if(*cursorY >= (int)lines->size())
        *cursorY = lines->size() - 1;
    *offsetY += pageSize;
    int maxOffset = std::max(0, (int)lines->size() - contentRows() + 2);
    if(*offsetY > maxOffset)
        *offsetY = maxOffset;
    if(*cursorY >= 0 && *cursorY < (int)lines->size())
        *cursorX = std::min(*wantedX, (int)(*lines)[*cursorY].length());
    needsFullRedraw = true;
}

void Editor::moveToScreenTop()
{
    *cursorY = *offsetY;
    if(*cursorY >= (int)lines->size())
        *cursorY = lines->size() - 1;
    moveToFirstNonBlank();
}

void Editor::moveToScreenMiddle()
{
    *cursorY = *offsetY + (contentRows() - 2) / 2;
    if(*cursorY >= (int)lines->size())
        *cursorY = lines->size() - 1;
    moveToFirstNonBlank();
}

void Editor::moveToScreenBottom()
{
    *cursorY = *offsetY + contentRows() - 3;
    if(*cursorY >= (int)lines->size())
        *cursorY = lines->size() - 1;
    moveToFirstNonBlank();
}

void Editor::adjustViewport()
{
    // uvim can start in FILE_BROWSER mode with skipInitialBuffer=true.
    // If the user exits the browser without opening a file, buffer pointers
    // (lines/cursorX/cursorY/offsetX/offsetY) may still be unset. Recover by
    // ensuring a valid current buffer exists.
    if(!lines || !cursorX || !cursorY || !offsetX || !offsetY)
    {
        if(buffers.empty())
        {
            createNewBuffer();
        }
        else
        {
            if(currentBufferIndex < 0 ||
               currentBufferIndex >= static_cast<int>(buffers.size()))
                currentBufferIndex = 0;
            updateCurrentBufferPointers();
        }
    }

    // If recovery failed for any reason, don't crash.
    if(!lines || !cursorX || !cursorY || !offsetX || !offsetY)
        return;

    if(lines->empty())
    {
        *cursorX = 0;
        *cursorY = 0;
        *offsetX = 0;
        *offsetY = 0;
        return;
    }

    if(splitActive)
    {
        PaneLayout layout = getPaneLayout(activePane);
        int rows = std::max(1, layout.rows - tabBarRows());
        int cols = std::max(1, layout.cols - gutterWidth());
        adjustViewportForPane(splitPanes[activePane], rows, cols);
        return;
    }

    const int n = line_count(*lines);

    int& cx = *cursorX;
    int& cy = *cursorY;
    int& ox = *offsetX;
    int& oy = *offsetY;

    // Keep cursor in a sane range before we compute the viewport.
    cy = std::clamp(cy, 0, n - 1);
    cx = std::clamp(cx, 0, line_len(*lines, cy));

    // Screen rows/cols are the drawable area (status+msg bars already
    // subtracted).
    const int rows = std::max(1, contentRows());
    const int cols = std::max(1, screenCols - gutterWidth());

    const int maxOffsetY = std::max(0, n - rows);

    // Vertical
    if(cy < oy)
        oy = cy;
    else if(cy >= oy + rows)
        oy = cy - rows + 1;
    oy = std::clamp(oy, 0, maxOffsetY);

    // Horizontal (clamp to current cursor; avoids runaway offsets)
    if(cx < ox)
        ox = cx;
    else if(cx >= ox + cols)
        ox = cx - cols + 1;

    // We don't know the longest visible line here without extra work,
    // so clamp to [0..cx] to avoid huge offsets when cx is small.
    ox = std::clamp(ox, 0, std::max(0, cx));
}

void Editor::adjustViewportForPane(PaneState& pane, int rows, int cols)
{
    if(!lines || lines->empty())
    {
        pane.cursorX = 0;
        pane.cursorY = 0;
        pane.offsetX = 0;
        pane.offsetY = 0;
        pane.wantedX = 0;
        return;
    }

    const int n = line_count(*lines);

    pane.cursorY = std::clamp(pane.cursorY, 0, n - 1);
    pane.cursorX = std::clamp(pane.cursorX, 0, line_len(*lines, pane.cursorY));

    rows = std::max(1, rows);
    cols = std::max(1, cols);

    const int maxOffsetY = std::max(0, n - rows);

    if(pane.cursorY < pane.offsetY)
        pane.offsetY = pane.cursorY;
    else if(pane.cursorY >= pane.offsetY + rows)
        pane.offsetY = pane.cursorY - rows + 1;
    pane.offsetY = std::clamp(pane.offsetY, 0, maxOffsetY);

    if(pane.cursorX < pane.offsetX)
        pane.offsetX = pane.cursorX;
    else if(pane.cursorX >= pane.offsetX + cols)
        pane.offsetX = pane.cursorX - cols + 1;

    pane.offsetX = std::clamp(pane.offsetX, 0, std::max(0, pane.cursorX));
}

void Editor::centerScreen()
{
    // Same recovery logic as adjustViewport() to avoid null deref when
    // called without an initialized buffer (e.g., exiting file browser).
    if(!lines || !cursorY || !offsetY)
    {
        if(buffers.empty())
        {
            createNewBuffer();
        }
        else
        {
            if(currentBufferIndex < 0 ||
               currentBufferIndex >= static_cast<int>(buffers.size()))
                currentBufferIndex = 0;
            updateCurrentBufferPointers();
        }
    }

    if(!lines || !cursorY || !offsetY)
        return;

    if(lines->empty())
    {
        *cursorY = 0;
        *offsetY = 0;
        return;
    }

    const int n = line_count(*lines);
    const int rows = std::max(1, contentRows());
    const int maxOffsetY = std::max(0, n - rows);

    *cursorY = std::clamp(*cursorY, 0, n - 1);
    *offsetY = std::clamp(*cursorY - rows / 2, 0, maxOffsetY);
}
