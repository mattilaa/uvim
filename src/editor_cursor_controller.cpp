#include "editor.h"
#include "editor_cursor_controller.h"
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

EditorCursorController::EditorCursorController(Editor& editor) : editor(editor)
{
}

void EditorCursorController::moveLeft(int count)
{
    if(!editor.lines || editor.lines->empty())
        return;

    int& cx = *editor.cursorX;
    int& cy = *editor.cursorY;
    int& wx = *editor.wantedX;

    while(count-- > 0)
    {
        if(cx > 0)
        {
            if(editor.utf8Mode && cy >= 0 && cy < line_count(*editor.lines))
            {
                std::string_view ln = line_view(*editor.lines, cy);
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
            cx = line_len(*editor.lines, cy);
        }
        else
        {
            break;
        }
    }

    wx = cx;
}

void EditorCursorController::moveRight(int count)
{
    if(!editor.lines || editor.lines->empty())
        return;

    int& cx = *editor.cursorX;
    int& cy = *editor.cursorY;
    int& wx = *editor.wantedX;

    const int n = line_count(*editor.lines);
    cy = std::clamp(cy, 0, n - 1);

    while(count-- > 0)
    {
        const int L = line_len(*editor.lines, cy);
        if(cx < L)
        {
            if(editor.utf8Mode)
            {
                std::string_view ln = line_view(*editor.lines, cy);
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

void EditorCursorController::moveUp(int count)
{
    if(!editor.lines || editor.lines->empty())
        return;

    int& cx = *editor.cursorX;
    int& cy = *editor.cursorY;
    const int& wx = *editor.wantedX;

    const int n = line_count(*editor.lines);
    cy = std::clamp(cy, 0, n - 1);

    cy = std::max(0, cy - std::max(0, count));
    cx = std::min(wx, line_len(*editor.lines, cy));
    if(editor.utf8Mode && cy >= 0 && cy < line_count(*editor.lines))
    {
        std::string_view ln = line_view(*editor.lines, cy);
        cx = text_utils::prevUtf8CharStart(ln, cx);
    }
}

void EditorCursorController::moveDown(int count)
{
    if(!editor.lines || editor.lines->empty())
        return;

    int& cx = *editor.cursorX;
    int& cy = *editor.cursorY;
    const int& wx = *editor.wantedX;

    const int n = line_count(*editor.lines);
    cy = std::clamp(cy, 0, n - 1);

    cy = std::min(n - 1, cy + std::max(0, count));
    cx = std::min(wx, line_len(*editor.lines, cy));
    if(editor.utf8Mode && cy >= 0 && cy < line_count(*editor.lines))
    {
        std::string_view ln = line_view(*editor.lines, cy);
        cx = text_utils::prevUtf8CharStart(ln, cx);
    }
}

void EditorCursorController::moveWordForward()
{
    if(!editor.lines || editor.lines->empty())
        return;

    int y = std::clamp(*editor.cursorY, 0, line_count(*editor.lines) - 1);
    int x = std::clamp(*editor.cursorX, 0, line_len(*editor.lines, y));

    const int n = line_count(*editor.lines);

    while(true)
    {
        std::string_view ln = line_view(*editor.lines, y);
        const int L = static_cast<int>(ln.size());

        // At end of line: go to next line, skip leading spaces
        if(x >= L)
        {
            if(y + 1 >= n)
                break;
            ++y;
            x = 0;

            ln = line_view(*editor.lines, y);
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

    *editor.cursorY = y;
    *editor.cursorX = x;
    *editor.wantedX = x;
}

void EditorCursorController::moveWordBackward()
{
    if(!editor.lines || editor.lines->empty())
        return;

    int y = std::clamp(*editor.cursorY, 0, line_count(*editor.lines) - 1);
    int x = std::clamp(*editor.cursorX, 0, line_len(*editor.lines, y));

    if(y == 0 && x == 0)
        return;

    if(x == 0)
    {
        --y;
        x = line_len(*editor.lines, y);
    }

    std::string_view ln = line_view(*editor.lines, y);

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

    *editor.cursorY = y;
    *editor.cursorX = x;
    *editor.wantedX = x;
}

void EditorCursorController::moveToEndOfWord()
{
    if(!editor.lines || editor.lines->empty())
        return;

    int y = std::clamp(*editor.cursorY, 0, line_count(*editor.lines) - 1);
    int x = std::clamp(*editor.cursorX, 0, line_len(*editor.lines, y));
    const int n = line_count(*editor.lines);

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

    std::string_view ln = line_view(*editor.lines, y);
    const int L = static_cast<int>(ln.size());

    if(x >= L - 1)
    {
        if(y + 1 < n)
        {
            ++y;
            x = 0;
            ln = line_view(*editor.lines, y);
            advance_to_word_end_on_line(ln, x);
        }
    }
    else
    {
        ++x;
        ln = line_view(*editor.lines, y);
        advance_to_word_end_on_line(ln, x);
    }

    *editor.cursorY = y;
    *editor.cursorX = x;
    *editor.wantedX = x;
}

void EditorCursorController::moveToLineStart()
{
    *editor.cursorX = 0;
    *editor.wantedX = 0;
}

void EditorCursorController::moveToLineEnd()
{
    if(!editor.lines || editor.lines->empty())
        return;

    int& cx = *editor.cursorX;
    int& cy = *editor.cursorY;
    int& wx = *editor.wantedX;

    cy = std::clamp(cy, 0, line_count(*editor.lines) - 1);

    cx = line_len(*editor.lines, cy);
    if(cx > 0 && editor.currentMode == NORMAL)
        --cx;

    wx = cx;
}

void EditorCursorController::moveToFirstLine()
{
    *editor.cursorY = 0;
    *editor.cursorX = 0;
    *editor.wantedX = 0;
}

void EditorCursorController::moveToLastLine()
{
    if(!editor.lines || editor.lines->empty())
        return;

    *editor.cursorY = line_count(*editor.lines) - 1;
    *editor.cursorX = 0;
    *editor.wantedX = 0;
}

void EditorCursorController::moveToLine(int line)
{
    if(!editor.lines || editor.lines->empty())
        return;

    *editor.cursorY = std::clamp(line, 0, line_count(*editor.lines) - 1);
    *editor.cursorX = 0;
    *editor.wantedX = 0;
}

void EditorCursorController::jumpForward()
{
    if(editor.jumpForwardStack.empty())
    {
        return;
    }

    JumpLocation current;
    current.bufferIndex = editor.currentBufferIndex;
    current.cursorX = *editor.cursorX;
    current.cursorY = *editor.cursorY;
    current.offsetX = *editor.offsetX;
    current.offsetY = *editor.offsetY;

    editor.jumpBackStack.push_back(current);

    JumpLocation target = editor.jumpForwardStack.back();
    editor.jumpForwardStack.pop_back();

    restoreJumpLocation(target);
}

void EditorCursorController::jumpBack()
{
    if(editor.jumpBackStack.empty())
    {
        return;
    }

    JumpLocation current;
    current.bufferIndex = editor.currentBufferIndex;
    current.cursorX = *editor.cursorX;
    current.cursorY = *editor.cursorY;
    current.offsetX = *editor.offsetX;
    current.offsetY = *editor.offsetY;

    editor.jumpForwardStack.push_back(current);

    JumpLocation target = editor.jumpBackStack.back();
    editor.jumpBackStack.pop_back();

    restoreJumpLocation(target);
}

void EditorCursorController::pushJumpLocation()
{
    if(!editor.currentBuffer)
        return;

    JumpLocation loc;
    loc.bufferIndex = editor.currentBufferIndex;
    loc.cursorX = *editor.cursorX;
    loc.cursorY = *editor.cursorY;
    loc.offsetX = *editor.offsetX;
    loc.offsetY = *editor.offsetY;

    editor.jumpBackStack.push_back(loc);
    editor.jumpForwardStack.clear();
}

void EditorCursorController::restoreJumpLocation(const JumpLocation& loc)
{
    if(loc.bufferIndex < 0 ||
       loc.bufferIndex >= static_cast<int>(editor.buffers.size()))
        return;

    editor.switchToBuffer(loc.bufferIndex);

    *editor.cursorX = loc.cursorX;
    *editor.cursorY = loc.cursorY;
    *editor.offsetX = loc.offsetX;
    *editor.offsetY = loc.offsetY;

    editor.needsFullRedraw = true;
}

void EditorCursorController::scrollHalfPageDown(bool visual)
{
    const int half = editor.contentRows() / 2;
    moveDown(half);
    adjustViewport();
    if(visual)
        editor.updateVisualSelection();
}

void EditorCursorController::scrollHalfPageUp(bool visual)
{
    const int half = editor.contentRows() / 2;
    moveUp(half);
    adjustViewport();
    if(visual)
        editor.updateVisualSelection();
}

void EditorCursorController::moveToMatchingBracket()
{
    if(!editor.lines || editor.lines->empty())
        return;

    const int n = line_count(*editor.lines);
    int y = std::clamp(*editor.cursorY, 0, n - 1);

    std::string_view ln = line_view(*editor.lines, y);
    if(ln.empty())
        return;

    int x = std::clamp(*editor.cursorX, 0, static_cast<int>(ln.size()) - 1);

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
                std::string_view lnv = line_view(*editor.lines, yy);
                const int LL = static_cast<int>(lnv.size());

                for(; xx < LL - 1; ++xx)
                {
                    if(lnv[xx] == '*' && lnv[xx + 1] == '/')
                    {
                        *editor.cursorY = yy;
                        *editor.cursorX = xx; // land on '*'
                        *editor.wantedX = xx;
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
                std::string_view lnv = line_view(*editor.lines, yy);
                const int LL = static_cast<int>(lnv.size());

                if(LL >= 2)
                {
                    int i = std::min(xx, LL - 2);
                    for(; i >= 0; --i)
                    {
                        if(lnv[i] == '/' && lnv[i + 1] == '*')
                        {
                            *editor.cursorY = yy;
                            *editor.cursorX = i; // land on '/'
                            *editor.wantedX = i;
                            return;
                        }
                    }
                }

                --yy;
                if(yy >= 0)
                {
                    const int prevLen = line_len(*editor.lines, yy);
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

            ln = line_view(*editor.lines, y);
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
                ln = line_view(*editor.lines, y);
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
                *editor.cursorY = y;
                *editor.cursorX = x;
                *editor.wantedX = x;
                return;
            }
            --depth;
        }
    }
}

void EditorCursorController::moveToFirstNonBlank()
{
    if(*editor.cursorY >= (int)editor.lines->size())
        return;
    const std::string& line = (*editor.lines)[*editor.cursorY];
    *editor.cursorX = 0;
    while(*editor.cursorX < (int)line.length() &&
          (line[*editor.cursorX] == ' ' || line[*editor.cursorX] == '\t'))
    {
        (*editor.cursorX)++;
    }
    *editor.wantedX = *editor.cursorX;
}

void EditorCursorController::moveParagraphForward()
{
    if(editor.lines->empty())
        return;

    // Skip current non-empty lines
    while(*editor.cursorY < (int)editor.lines->size() - 1 &&
          !(*editor.lines)[*editor.cursorY].empty())
    {
        (*editor.cursorY)++;
    }
    // Skip empty lines
    while(*editor.cursorY < (int)editor.lines->size() - 1 &&
          (*editor.lines)[*editor.cursorY].empty())
    {
        (*editor.cursorY)++;
    }
    *editor.cursorX = 0;
    adjustViewport();
}

void EditorCursorController::moveParagraphBackward()
{
    if(editor.lines->empty())
        return;

    // Skip current empty lines
    while(*editor.cursorY > 0 && (*editor.lines)[*editor.cursorY].empty())
    {
        (*editor.cursorY)--;
    }
    // Skip non-empty editor.lines
    while(*editor.cursorY > 0 && !(*editor.lines)[*editor.cursorY].empty())
    {
        (*editor.cursorY)--;
    }
    *editor.cursorX = 0;
    adjustViewport();
}

void EditorCursorController::moveWordForwardBig()
{
    if(editor.lines->empty() || *editor.cursorY >= (int)editor.lines->size())
        return;

    const std::string& line = (*editor.lines)[*editor.cursorY];

    // Skip current non-whitespace
    while(*editor.cursorX < (int)line.length() &&
          !std::isspace(line[*editor.cursorX]))
    {
        (*editor.cursorX)++;
    }
    // Skip whitespace
    while(*editor.cursorX < (int)line.length() &&
          std::isspace(line[*editor.cursorX]))
    {
        (*editor.cursorX)++;
    }
    // If at end of line, move to next line
    if(*editor.cursorX >= (int)line.length() &&
       *editor.cursorY < (int)editor.lines->size() - 1)
    {
        (*editor.cursorY)++;
        *editor.cursorX = 0;
        const std::string& nextLine = (*editor.lines)[*editor.cursorY];
        while(*editor.cursorX < (int)nextLine.length() &&
              std::isspace(nextLine[*editor.cursorX]))
        {
            (*editor.cursorX)++;
        }
    }
    *editor.wantedX = *editor.cursorX;
}

void EditorCursorController::moveWordBackwardBig()
{
    if(editor.lines->empty() || *editor.cursorY >= (int)editor.lines->size())
        return;

    // Move back one if not at start
    if(*editor.cursorX > 0)
        (*editor.cursorX)--;
    else if(*editor.cursorY > 0)
    {
        (*editor.cursorY)--;
        *editor.cursorX = (*editor.lines)[*editor.cursorY].length();
        if(*editor.cursorX > 0)
            (*editor.cursorX)--;
    }

    const std::string& line = (*editor.lines)[*editor.cursorY];

    // Skip whitespace backward
    while(*editor.cursorX > 0 && std::isspace(line[*editor.cursorX]))
    {
        (*editor.cursorX)--;
    }
    // Skip non-whitespace backward
    while(*editor.cursorX > 0 && !std::isspace(line[*editor.cursorX - 1]))
    {
        (*editor.cursorX)--;
    }
    *editor.wantedX = *editor.cursorX;
}

void EditorCursorController::moveToEndOfWordBig()
{
    if(editor.lines->empty() || *editor.cursorY >= (int)editor.lines->size())
        return;

    const std::string& line = (*editor.lines)[*editor.cursorY];

    // Move forward one
    if(*editor.cursorX < (int)line.length())
        (*editor.cursorX)++;

    // Skip whitespace
    while(*editor.cursorX < (int)line.length() &&
          std::isspace(line[*editor.cursorX]))
    {
        (*editor.cursorX)++;
    }
    // Move to end of word
    while(*editor.cursorX < (int)line.length() - 1 &&
          !std::isspace(line[*editor.cursorX + 1]))
    {
        (*editor.cursorX)++;
    }
    *editor.wantedX = *editor.cursorX;
}

void EditorCursorController::findCharForwardBefore(char c)
{
    if(*editor.cursorY >= (int)editor.lines->size())
        return;
    const std::string& line = (*editor.lines)[*editor.cursorY];

    for(int i = *editor.cursorX + 1; i < (int)line.length(); i++)
    {
        if(line[i] == c)
        {
            *editor.cursorX = i - 1;
            if(*editor.cursorX < 0)
                *editor.cursorX = 0;
            *editor.wantedX = *editor.cursorX;
            return;
        }
    }
}

void EditorCursorController::findCharBackwardAfter(char c)
{
    if(*editor.cursorY >= (int)editor.lines->size())
        return;
    const std::string& line = (*editor.lines)[*editor.cursorY];

    for(int i = *editor.cursorX - 1; i >= 0; i--)
    {
        if(line[i] == c)
        {
            *editor.cursorX = i + 1;
            if(*editor.cursorX >= (int)line.length())
                *editor.cursorX = line.length() - 1;
            *editor.wantedX = *editor.cursorX;
            return;
        }
    }
}

// ============================================================================
// Character finding (f/F motions) - base implementations
// These may already exist in another file, but are needed by operator_pending
// ============================================================================

void EditorCursorController::findCharForward(char c)
{
    if(*editor.cursorY >= (int)editor.lines->size())
        return;
    const std::string& line = (*editor.lines)[*editor.cursorY];

    for(int i = *editor.cursorX + 1; i < (int)line.length(); i++)
    {
        if(line[i] == c)
        {
            *editor.cursorX = i;
            *editor.wantedX = *editor.cursorX;
            editor.lastFindChar = c;
            editor.lastFindForward = true;
            return;
        }
    }
}

void EditorCursorController::findCharBackward(char c)
{
    if(*editor.cursorY >= (int)editor.lines->size())
        return;
    const std::string& line = (*editor.lines)[*editor.cursorY];

    for(int i = *editor.cursorX - 1; i >= 0; i--)
    {
        if(line[i] == c)
        {
            *editor.cursorX = i;
            *editor.wantedX = *editor.cursorX;
            editor.lastFindChar = c;
            editor.lastFindForward = false;
            return;
        }
    }
}

// ============================================================================
// Scrolling Commands
// ============================================================================

void EditorCursorController::scrollToTop()
{
    *editor.offsetY = 0;
    *editor.cursorY = 0;
    *editor.cursorX = 0;
    editor.needsFullRedraw = true;
}

void EditorCursorController::scrollToBottom()
{
    if(editor.lines->empty())
        return;
    *editor.cursorY = editor.lines->size() - 1;
    *editor.cursorX = 0;
    adjustViewport();
    editor.needsFullRedraw = true;
}

void EditorCursorController::scrollPageUp()
{
    int pageSize = editor.contentRows() - 2;
    *editor.cursorY -= pageSize;
    if(*editor.cursorY < 0)
        *editor.cursorY = 0;
    *editor.offsetY -= pageSize;
    if(*editor.offsetY < 0)
        *editor.offsetY = 0;
    if(*editor.cursorY < (int)editor.lines->size())
        *editor.cursorX = std::min(
            *editor.wantedX, (int)(*editor.lines)[*editor.cursorY].length());
    editor.needsFullRedraw = true;
}

void EditorCursorController::scrollPageDown()
{
    int pageSize = editor.contentRows() - 2;
    *editor.cursorY += pageSize;
    if(*editor.cursorY >= (int)editor.lines->size())
        *editor.cursorY = editor.lines->size() - 1;
    *editor.offsetY += pageSize;
    int maxOffset =
        std::max(0, (int)editor.lines->size() - editor.contentRows() + 2);
    if(*editor.offsetY > maxOffset)
        *editor.offsetY = maxOffset;
    if(*editor.cursorY >= 0 && *editor.cursorY < (int)editor.lines->size())
        *editor.cursorX = std::min(
            *editor.wantedX, (int)(*editor.lines)[*editor.cursorY].length());
    editor.needsFullRedraw = true;
}

void EditorCursorController::moveToScreenTop()
{
    *editor.cursorY = *editor.offsetY;
    if(*editor.cursorY >= (int)editor.lines->size())
        *editor.cursorY = editor.lines->size() - 1;
    moveToFirstNonBlank();
}

void EditorCursorController::moveToScreenMiddle()
{
    *editor.cursorY = *editor.offsetY + (editor.contentRows() - 2) / 2;
    if(*editor.cursorY >= (int)editor.lines->size())
        *editor.cursorY = editor.lines->size() - 1;
    moveToFirstNonBlank();
}

void EditorCursorController::moveToScreenBottom()
{
    *editor.cursorY = *editor.offsetY + editor.contentRows() - 3;
    if(*editor.cursorY >= (int)editor.lines->size())
        *editor.cursorY = editor.lines->size() - 1;
    moveToFirstNonBlank();
}

void EditorCursorController::adjustViewport()
{
    // uvim can start in FILE_BROWSER mode with skipInitialBuffer=true.
    // If the user exits the browser without opening a file, buffer pointers
    // (editor.lines/editor.cursorX/editor.cursorY/editor.offsetX/editor.offsetY)
    // may still be unset. Recover by ensuring a valid current buffer exists.
    if(!editor.lines || !editor.cursorX || !editor.cursorY || !editor.offsetX ||
       !editor.offsetY)
    {
        if(editor.buffers.empty())
        {
            editor.createNewBuffer();
        }
        else
        {
            if(editor.currentBufferIndex < 0 ||
               editor.currentBufferIndex >=
                   static_cast<int>(editor.buffers.size()))
                editor.currentBufferIndex = 0;
            editor.updateCurrentBufferPointers();
        }
    }

    // If recovery failed for any reason, don't crash.
    if(!editor.lines || !editor.cursorX || !editor.cursorY || !editor.offsetX ||
       !editor.offsetY)
        return;

    if(editor.lines->empty())
    {
        *editor.cursorX = 0;
        *editor.cursorY = 0;
        *editor.offsetX = 0;
        *editor.offsetY = 0;
        return;
    }

    if(editor.splitActive)
    {
        Editor::PaneLayout layout = editor.getPaneLayout(editor.activePane);
        int rows = std::max(1, layout.rows - editor.tabBarRows());
        int cols = std::max(1, layout.cols - editor.gutterWidth());
        adjustViewportForPane(editor.splitPanes[editor.activePane], rows, cols);
        return;
    }

    const int n = line_count(*editor.lines);

    int& cx = *editor.cursorX;
    int& cy = *editor.cursorY;
    int& ox = *editor.offsetX;
    int& oy = *editor.offsetY;

    // Keep cursor in a sane range before we compute the viewport.
    cy = std::clamp(cy, 0, n - 1);
    cx = std::clamp(cx, 0, line_len(*editor.lines, cy));

    // Screen rows/cols are the drawable area (status+msg bars already
    // subtracted).
    const int rows = std::max(1, editor.contentRows());
    const int cols = std::max(1, editor.screenCols - editor.gutterWidth());

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

void EditorCursorController::adjustViewportForPane(Editor::PaneState& pane,
                                                   int rows, int cols)
{
    if(!editor.lines || editor.lines->empty())
    {
        pane.cursorX = 0;
        pane.cursorY = 0;
        pane.offsetX = 0;
        pane.offsetY = 0;
        pane.wantedX = 0;
        return;
    }

    const int n = line_count(*editor.lines);

    pane.cursorY = std::clamp(pane.cursorY, 0, n - 1);
    pane.cursorX =
        std::clamp(pane.cursorX, 0, line_len(*editor.lines, pane.cursorY));

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

void EditorCursorController::centerScreen()
{
    // Same recovery logic as adjustViewport() to avoid null deref when
    // called without an initialized buffer (e.g., exiting file browser).
    if(!editor.lines || !editor.cursorY || !editor.offsetY)
    {
        if(editor.buffers.empty())
        {
            editor.createNewBuffer();
        }
        else
        {
            if(editor.currentBufferIndex < 0 ||
               editor.currentBufferIndex >=
                   static_cast<int>(editor.buffers.size()))
                editor.currentBufferIndex = 0;
            editor.updateCurrentBufferPointers();
        }
    }

    if(!editor.lines || !editor.cursorY || !editor.offsetY)
        return;

    if(editor.lines->empty())
    {
        *editor.cursorY = 0;
        *editor.offsetY = 0;
        return;
    }

    const int n = line_count(*editor.lines);
    const int rows = std::max(1, editor.contentRows());
    const int maxOffsetY = std::max(0, n - rows);

    *editor.cursorY = std::clamp(*editor.cursorY, 0, n - 1);
    *editor.offsetY = std::clamp(*editor.cursorY - rows / 2, 0, maxOffsetY);
}
