#include "editor.h"
#include "lsp_client.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

// Very small snippet “desugaring”: turns clangd snippets into plain insert
// text.
// - removes $0, $1 ...
// - turns ${1:foo} -> foo
// - removes ${1}
static std::string stripSnippet(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for(size_t i = 0; i < s.size(); ++i)
    {
        char c = s[i];
        if(c != '$')
        {
            out.push_back(c);
            continue;
        }

        if(i + 1 >= s.size())
            continue;

        char n = s[i + 1];
        if(std::isdigit((unsigned char)n))
        {
            // $0, $1 ...
            i += 1;
            while(i + 1 < s.size() && std::isdigit((unsigned char)s[i + 1]))
                i++;
            continue;
        }

        if(n == '{')
        {
            // ${1:foo} or ${1}
            size_t end = s.find('}', i + 2);
            if(end == std::string::npos)
                continue;

            std::string inner = s.substr(i + 2, end - (i + 2));
            // inner might be "1:foo" or "1"
            size_t colon = inner.find(':');
            if(colon != std::string::npos)
            {
                out += inner.substr(colon + 1);
            }
            // else: just a placeholder number → ignore
            i = end;
            continue;
        }

        // Unknown $-sequence → drop '$' and keep the next char
        // (so "$$" becomes "$", etc.)
        out.push_back(n);
        i += 1;
    }
    return out;
}

static inline void appendUtf8Repeat(std::string& out, const char* glyph,
                                    int count)
{
    for(int i = 0; i < count; ++i)
        out += glyph;
}

static inline bool isAnsiStart(const std::string& s, size_t i)
{
    return i + 1 < s.size() && s[i] == '\x1b' && s[i + 1] == '[';
}

static inline size_t skipAnsi(const std::string& s, size_t i)
{
    // Skip ESC[ ... <letter>
    i += 2;
    while(i < s.size())
    {
        char c = s[i++];
        if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
            break;
    }
    return i;
}

// Approximate terminal display width.
// - strips ANSI escapes
// - counts UTF-8 codepoints as width 1 (good enough for our popup)
static inline int displayWidth(const std::string& s)
{
    int w = 0;
    for(size_t i = 0; i < s.size();)
    {
        if(isAnsiStart(s, i))
        {
            i = skipAnsi(s, i);
            continue;
        }

        unsigned char c = (unsigned char)s[i];
        if(c < 0x80)
        {
            ++w;
            ++i;
            continue;
        }

        // UTF-8: skip continuation bytes
        if((c & 0xE0) == 0xC0)
            i += 2;
        else if((c & 0xF0) == 0xE0)
            i += 3;
        else if((c & 0xF8) == 0xF0)
            i += 4;
        else
            ++i;
        ++w;
    }
    return w;
}

static inline int fuzzyScore(const std::string& text,
                             const std::string& pattern)
{
    if(pattern.empty())
        return 0;

    auto lower = [](unsigned char ch) -> unsigned char
    {
        if(ch >= 'A' && ch <= 'Z')
            return (unsigned char)(ch - 'A' + 'a');
        return ch;
    };

    int score = 0;
    int ti = 0;
    int consecutive = 0;

    for(int pi = 0; pi < (int)pattern.size(); ++pi)
    {
        unsigned char pc = lower((unsigned char)pattern[pi]);
        bool found = false;

        while(ti < (int)text.size())
        {
            unsigned char tc = (unsigned char)text[ti];
            unsigned char ltc = lower(tc);
            if(ltc == pc)
            {
                // base points + consecutive bonus
                score += 10;
                score += consecutive * 5;

                // token-boundary bonus
                if(ti == 0)
                    score += 8;
                else
                {
                    char prev = text[ti - 1];
                    if(prev == '_' || prev == ':' || prev == ' ' ||
                       prev == '\t' || prev == '-')
                        score += 8;
                }

                ++consecutive;
                ++ti;
                found = true;
                break;
            }
            else
            {
                consecutive = 0;
                ++ti;
            }
        }
        if(!found)
            return -1;
    }

    return score;
}

void Editor::requestCompletion()
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    if(!isClangdLspEnabled())
    {
        setStatusMessage("clangd completion: OFF");
        return;
    }
    if(!isCppFile())
    {
        setStatusMessage("clangd completion: only for C/C++");
        return;
    }

    // Compute prefix/anchor (identifier chars).
    const std::string& line = (*lines)[*cursorY];
    int ax = *cursorX;
    while(ax > 0 && text_utils::isIdent(line[ax - 1]))
        --ax;

    completionAnchorX = ax;
    completionAnchorY = *cursorY;

    // Sync buffer text.
    std::string text;
    text.reserve(lines->size() * 80);
    for(size_t i = 0; i < lines->size(); ++i)
    {
        text += (*lines)[i];
        if(i + 1 < lines->size())
            text.push_back('\n');
    }
    lspClient->didChange(currentBuffer->filename, text);

    auto items =
        lspClient->completion(currentBuffer->filename, *cursorY, *cursorX);
    completionAll.clear();
    completionAll.reserve(items.size());

    for(const auto& ci : items)
    {
        CompletionEntry e;
        e.label = ci.label;
        e.insertText = ci.insertText;
        e.isSnippet = ci.isSnippet;
        completionAll.push_back(std::move(e));
    }

    if(completionAll.empty())
    {
        cancelCompletion();
        setStatusMessage("clangd completion: no results");
        return;
    }

    completionActive = true;
    completionSelected = 0;
    completionScroll = 0;
    rebuildCompletionFilter();
    needsFullRedraw = true;
#else
    setStatusMessage("clangd completion: not compiled in");
#endif
}

void Editor::cancelCompletion()
{
    completionActive = false;
    completionAll.clear();
    completionFiltered.clear();
    completionSelected = 0;
    completionScroll = 0;
    completionQuery.clear();
}

void Editor::completionNext()
{
    if(!completionActive || completionFiltered.empty())
        return;
    completionSelected =
        (completionSelected + 1) % (int)completionFiltered.size();

    const int win = std::min(8, (int)completionFiltered.size());
    if(completionSelected < completionScroll)
        completionScroll = completionSelected;
    else if(completionSelected >= completionScroll + win)
        completionScroll = completionSelected - win + 1;

    needsFullRedraw = true;
}

void Editor::completionPrev()
{
    if(!completionActive || completionFiltered.empty())
        return;
    completionSelected = (completionSelected - 1);
    if(completionSelected < 0)
        completionSelected = (int)completionFiltered.size() - 1;

    const int win = std::min(8, (int)completionFiltered.size());
    if(completionSelected < completionScroll)
        completionScroll = completionSelected;
    else if(completionSelected >= completionScroll + win)
        completionScroll = completionSelected - win + 1;

    needsFullRedraw = true;
}

void Editor::acceptCompletion()
{
    if(!completionActive || completionFiltered.empty())
        return;

    // Ensure anchor is on the current line (if the user moved, recompute).
    completionAnchorY = *cursorY;
    const std::string& line = (*lines)[*cursorY];
    int ax = *cursorX;
    while(ax > 0 && text_utils::isIdent(line[ax - 1]))
        --ax;
    completionAnchorX = ax;

    const CompletionEntry& it =
        completionAll[completionFiltered[completionSelected]];
    std::string insert = it.insertText.empty() ? it.label : it.insertText;
    if(it.isSnippet)
        insert = stripSnippet(insert);

    // Replace prefix [anchorX, cursorX)
    std::string& mutableLine = (*lines)[*cursorY];
    int start =
        std::max(0, std::min(completionAnchorX, (int)mutableLine.size()));
    int end = std::max(0, std::min(*cursorX, (int)mutableLine.size()));
    if(end < start)
        std::swap(start, end);

    mutableLine.replace(start, end - start, insert);
    *cursorX = start + (int)insert.size();
    *wantedX = *cursorX;
    *dirty = true;

    cancelCompletion();
    needsFullRedraw = true;
}

void Editor::rebuildCompletionFilter()
{
    if(!completionActive)
        return;

    // Cancel if cursor moved away from the anchor line or before anchor.
    if(*cursorY != completionAnchorY || *cursorX < completionAnchorX)
    {
        cancelCompletion();
        needsFullRedraw = true;
        return;
    }

    const std::string& line = (*lines)[*cursorY];
    int a = std::max(0, std::min(completionAnchorX, (int)line.size()));
    int b = std::max(0, std::min(*cursorX, (int)line.size()));
    if(b < a)
        b = a;
    completionQuery = line.substr(a, b - a);

    struct Scored
    {
        int idx;
        int score;
    };
    std::vector<Scored> scored;
    scored.reserve(completionAll.size());

    for(int i = 0; i < (int)completionAll.size(); ++i)
    {
        const auto& e = completionAll[i];
        // Use the completion-popup fuzzy matcher (simple subsequence).
        // Unqualified name would resolve to Editor::fuzzyScore (3-arg) used by
        // file/buffer pickers, so qualify explicitly.
        int s = ::fuzzyScore(e.label, completionQuery);
        if(s >= 0)
            scored.push_back({i, s});
    }

    std::stable_sort(scored.begin(), scored.end(),
                     [](const Scored& x, const Scored& y)
                     { return x.score > y.score; });

    completionFiltered.clear();
    completionFiltered.reserve(scored.size());
    for(const auto& s : scored)
        completionFiltered.push_back(s.idx);

    if(completionFiltered.empty())
    {
        cancelCompletion();
        needsFullRedraw = true;
        return;
    }

    if(completionSelected >= (int)completionFiltered.size())
        completionSelected = (int)completionFiltered.size() - 1;
    if(completionSelected < 0)
        completionSelected = 0;

    const int win = std::min(8, (int)completionFiltered.size());
    if(completionSelected < completionScroll)
        completionScroll = completionSelected;
    else if(completionSelected >= completionScroll + win)
        completionScroll = completionSelected - win + 1;

    if(completionScroll < 0)
        completionScroll = 0;

    // Ensure the popup updates immediately as the user types.
    needsFullRedraw = true;
}

void Editor::drawCompletionPopup(std::string& output) const
{
    if(!completionActive || completionFiltered.empty())
        return;
    if(currentMode != INSERT)
        return;

    // Visible rows
    const int maxRows =
        std::min({8, (int)completionFiltered.size(), screenRows - 2});
    if(maxRows <= 0)
        return;

    // Determine cursor position on screen
    // Use the editor's viewport offsets.
    int cy = (*cursorY - *offsetY) + 1;
    int cx = (*cursorX - *offsetX) + 1;
    if(cy < 1)
        cy = 1;
    if(cy > screenRows)
        cy = screenRows;
    if(cx < 1)
        cx = 1;
    if(cx > screenCols)
        cx = screenCols;

    // Compute width from all filtered results (so the window doesn't
    // resize when scrolling).
    int maxW = 0;
    // Cap work for extremely large result sets.
    const int cap = std::min((int)completionFiltered.size(), 500);
    for(int i = 0; i < cap; ++i)
    {
        const auto& e = completionAll[completionFiltered[i]];
        maxW = std::max(maxW, displayWidth(e.label));
    }
    // Add padding inside box
    int innerW = std::max(12, maxW);
    // Box consumes innerW + 2 padding + 2 borders
    int totalW = innerW + 4;
    if(totalW > screenCols)
    {
        totalW = screenCols;
        innerW = std::max(4, totalW - 4);
    }

    // Place below cursor if possible, otherwise above
    int totalH = maxRows + 2;
    int top = cy + 1;
    if(top + totalH - 1 > screenRows)
        top = cy - totalH;
    if(top < 1)
        top = 1;

    int left = cx;
    if(left + totalW - 1 > screenCols)
        left = std::max(1, screenCols - totalW + 1);

    auto moveTo = [&](int r, int c) { output += Terminal::cursorPos(r, c); };

    // Top border
    moveTo(top, left);
    text_utils::appendU8(output, u8"┌");
    text_utils::appendUtf8Repeat(output, u8"─", innerW + 2);
    text_utils::appendU8(output, u8"┐");

    auto kindColor = [](int kind) -> const char*
    {
        // LSP CompletionItemKind colors (rough semantic mapping)
        switch(kind)
        {
        case 2:  // Method
        case 3:  // Function
        case 4:  // Constructor
        case 23: // Event
            return Terminal::FG_BRIGHT_BLUE;
        case 5:  // Field
        case 6:  // Variable
        case 10: // Property
        case 18: // Reference
        case 25: // TypeParameter
            return Terminal::FG_CYAN;
        case 7:  // Class
        case 8:  // Interface
        case 13: // Enum
        case 22: // Struct
            return Terminal::FG_MAGENTA;
        case 14: // Keyword
            return Terminal::FG_BRIGHT_MAGENTA;
        case 11: // Unit
        case 12: // Value
        case 20: // EnumMember
        case 21: // Constant
            return Terminal::FG_YELLOW;
        case 24: // Operator
            return Terminal::FG_BRIGHT_YELLOW;
        case 15: // Snippet
        case 16: // Color
            return Terminal::FG_GREEN;
        case 17: // File
        case 19: // Folder
            return Terminal::FG_BRIGHT_BLACK;
        default:
            return Terminal::FG_DEFAULT;
        }
    };

    auto appendSyntaxRow = [&](const std::string& text, bool selected,
                               int kind)
    {
        if(!isCppFile())
        {
            if(selected)
                output += Terminal::STYLE_SELECTION;
            output += kindColor(kind);
            output += text;
            output += Terminal::ESC_RESET_ALL;
            return;
        }

        bool inBlockComment = false;
        std::vector<Token> tokens = tokenizeLine(text, inBlockComment);
        std::vector<TokenType> colors(text.size(), TOKEN_NORMAL);
        bool hasColor = false;

        for(const auto& token : tokens)
        {
            if(token.type != TOKEN_NORMAL)
                hasColor = true;
            int tokenEnd = token.start + token.length;
            for(int pos = token.start; pos < tokenEnd && pos < (int)text.size();
                pos++)
            {
                colors[pos] = token.type;
            }
        }

        if(selected)
            output += Terminal::STYLE_SELECTION;

        if(!hasColor)
        {
            output += kindColor(kind);
            output += text;
            output += Terminal::ESC_RESET_ALL;
            return;
        }

        TokenType current = TOKEN_NORMAL;
        for(size_t i = 0; i < text.size(); ++i)
        {
            if(colors[i] != current)
            {
                current = colors[i];
                output += getColorCode(current);
            }
            output += text[i];
        }

        output += Terminal::ESC_RESET_ALL;
    };

    // Rows
    for(int i = 0; i < maxRows; ++i)
    {
        int fidx = completionScroll + i;
        if(fidx >= (int)completionFiltered.size())
            break;
        const auto& e = completionAll[completionFiltered[fidx]];

        moveTo(top + 1 + i, left);
        text_utils::appendU8(output, u8"│");
        output += " ";

        bool sel = (fidx == completionSelected);

        std::string row = e.label;
        // Trim to fit
        while(displayWidth(row) > innerW)
            row.pop_back();
        appendSyntaxRow(row, sel, e.kind);

        // Pad to width
        int pad = innerW - displayWidth(row);
        if(pad > 0)
            output += std::string(pad, ' ');

        if(sel)
            output += Terminal::ESC_RESET_ALL;

        output += " ";
        text_utils::appendU8(output, u8"│");
    }

    // Bottom border
    moveTo(top + 1 + maxRows, left);
    text_utils::appendU8(output, u8"└");
    text_utils::appendUtf8Repeat(output, u8"─", innerW + 2);
    text_utils::appendU8(output, u8"┐");
}
