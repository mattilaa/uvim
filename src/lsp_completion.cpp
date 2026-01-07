#include "editor.h"
#include "lsp_client.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <unordered_set>

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

struct IncludeContext
{
    bool isSystem = false;
    int anchor = 0;
    std::string dirPrefix;
    std::string filePrefix;
};

static std::optional<IncludeContext> findIncludeContext(const std::string& line,
                                                        int cursorX)
{
    size_t includePos = line.find("#include");
    if(includePos == std::string::npos)
        return std::nullopt;

    size_t pos = includePos + 8; // skip "#include"
    while(pos < line.size() && std::isspace((unsigned char)line[pos]))
        pos++;

    if(pos >= line.size())
        return std::nullopt;

    char openDelim = line[pos];
    char closeDelim = 0;
    bool isSystem = false;

    if(openDelim == '<')
    {
        isSystem = true;
        closeDelim = '>';
    }
    else if(openDelim == '"')
    {
        closeDelim = '"';
    }
    else
    {
        return std::nullopt;
    }

    int openPos = (int)pos;
    if(cursorX <= openPos)
        return std::nullopt;

    size_t closePos = line.find(closeDelim, pos + 1);
    if(closePos != std::string::npos && cursorX > (int)closePos)
        return std::nullopt;

    int pathStart = openPos + 1;
    int pathEnd = (closePos == std::string::npos)
                      ? cursorX
                      : std::min(cursorX, (int)closePos);
    if(pathEnd < pathStart)
        pathEnd = pathStart;

    std::string typed = line.substr(pathStart, pathEnd - pathStart);
    size_t lastSlash = typed.find_last_of("/\\");

    IncludeContext ctx;
    ctx.isSystem = isSystem;
    if(lastSlash == std::string::npos)
    {
        ctx.dirPrefix = "";
        ctx.filePrefix = typed;
        ctx.anchor = pathStart;
    }
    else
    {
        ctx.dirPrefix = typed.substr(0, lastSlash + 1);
        ctx.filePrefix = typed.substr(lastSlash + 1);
        ctx.anchor = pathStart + (int)lastSlash + 1;
    }

    return ctx;
}

static int computeCompletionAnchor(const std::string& line, int cursorX)
{
    auto includeCtx = findIncludeContext(line, cursorX);
    if(includeCtx)
        return includeCtx->anchor;

    int ax = cursorX;
    while(ax > 0 && text_utils::isIdent(line[ax - 1]))
        --ax;
    return ax;
}

static void appendIncludeEntries(const std::filesystem::path& baseDir,
                                 const std::string& dirPrefix,
                                 const std::string& filePrefix,
                                 std::vector<CompletionEntry>& out,
                                 std::unordered_set<std::string>& seen)
{
    std::error_code ec;
    std::filesystem::path target = baseDir;
    if(!dirPrefix.empty())
        target /= dirPrefix;

    if(!std::filesystem::exists(target, ec) ||
       !std::filesystem::is_directory(target, ec))
        return;

    for(const auto& entry : std::filesystem::directory_iterator(target, ec))
    {
        if(ec)
            break;

        std::string name = entry.path().filename().string();
        if(!filePrefix.empty() &&
           name.rfind(filePrefix, 0) != 0) // prefix match
            continue;

        std::string label = dirPrefix + name;
        std::string insertText = name;

        if(entry.is_directory(ec))
        {
            label += "/";
            insertText += "/";
        }

        if(seen.insert(label).second)
        {
            CompletionEntry e;
            e.label = label;
            e.insertText = insertText;
            e.isSnippet = false;
            e.kind = entry.is_directory(ec) ? 19 : 17;
            out.push_back(std::move(e));
        }
    }
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
    auto keywordFallback =
        [&](const std::vector<std::string_view>& words, std::string_view label)
    {
        completionAll.clear();
        completionFiltered.clear();
        completionSelected = 0;
        completionScroll = 0;

        completionAll.reserve(words.size());
        for(const auto& w : words)
        {
            CompletionEntry e;
            e.label = std::string(w);
            completionAll.push_back(std::move(e));
        }

        if(completionAll.empty())
        {
            cancelCompletion();
            return;
        }

        completionActive = true;
        rebuildCompletionFilter();
        needsFullRedraw = true;
        setStatusMessage(std::string(label) + " completion: keywords");
    };

    LspClient* client = nullptr;
    std::string label;
    std::string languageId;
    if(isRobotFile())
    {
        if(!isRobotLspEnabled())
        {
            setStatusMessage("robot LSP: OFF");
            static constexpr std::string_view kRobotKeywords[] = {
                "*** Settings ***",
                "*** Variables ***",
                "*** Test Cases ***",
                "*** Tasks ***",
                "*** Keywords ***",
                "*** Comments ***",
                "Run Keyword",
                "Run Keyword And Return Status",
                "Run Keyword If",
                "Run Keywords",
                "Should Be Equal",
                "Should Contain",
                "Log",
                "Sleep",
                "FOR",
                "END",
                "IF",
                "ELSE",
                "ELSE IF",
                "TRY",
                "EXCEPT",
                "FINALLY",
            };
            const std::string& line = (*lines)[*cursorY];
            completionAnchorX = computeCompletionAnchor(line, *cursorX);
            completionAnchorY = *cursorY;
            keywordFallback(
                {std::begin(kRobotKeywords), std::end(kRobotKeywords)},
                "robot");
            return;
        }
        client = robotLspClient.get();
        label = "robot";
        languageId = "robotframework";
    }
    else if(isPythonFile())
    {
        if(!isPythonLspEnabled())
        {
            setStatusMessage("python LSP: OFF");
            static constexpr std::string_view kPythonKeywords[] = {
                "and",    "as",       "assert",   "async", "await",  "break",
                "class",  "continue", "def",      "del",   "elif",   "else",
                "except", "False",    "finally",  "for",   "from",   "global",
                "if",     "import",   "in",       "is",    "lambda", "match",
                "case",   "None",     "nonlocal", "not",   "or",     "pass",
                "raise",  "return",   "True",     "try",   "while",  "with",
                "yield",
            };
            const std::string& line = (*lines)[*cursorY];
            completionAnchorX = computeCompletionAnchor(line, *cursorX);
            completionAnchorY = *cursorY;
            keywordFallback(
                {std::begin(kPythonKeywords), std::end(kPythonKeywords)},
                "python");
            return;
        }
        client = pythonLspClient.get();
        label = "python";
        languageId = "python";
    }
    else if(isCppFile())
    {
        if(!isClangdLspEnabled())
        {
            setStatusMessage("clangd completion: OFF");
            return;
        }
        client = lspClient.get();
        label = "clangd";
        languageId = "cpp";
    }
    else
    {
        setStatusMessage("completion: no LSP for filetype");
        return;
    }

    const std::string& line = (*lines)[*cursorY];
    int ax = computeCompletionAnchor(line, *cursorX);

    completionAnchorX = ax;
    completionAnchorY = *cursorY;

    if(isCppFile())
    {
        if(auto includeCtx = findIncludeContext(line, *cursorX))
        {
            completionAll.clear();
            completionFiltered.clear();
            completionSelected = 0;
            completionScroll = 0;

            std::unordered_set<std::string> seen;
            completionAll.reserve(256);

            if(includeCtx->isSystem)
            {
                std::vector<std::string> systemPaths;
#ifdef __APPLE__
                systemPaths = {
                    "/Applications/Xcode.app/Contents/Developer/Platforms/"
                    "MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include/c++/"
                    "v1",
                    "/Applications/Xcode.app/Contents/Developer/Toolchains/"
                    "XcodeDefault.xctoolchain/usr/lib/clang/17/include",
                    "/Applications/Xcode.app/Contents/Developer/Platforms/"
                    "MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include",
                    "/usr/local/include",
                    "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/"
                    "include/"
                    "c++/v1",
                    "/Library/Developer/CommandLineTools/usr/include/c++/v1",
                };
#else
                systemPaths = {
                    "/usr/include/c++/13",
                    "/usr/include/c++/12",
                    "/usr/include/c++/11",
                    "/usr/include/x86_64-linux-gnu/c++/13",
                    "/usr/include/x86_64-linux-gnu/c++/12",
                    "/usr/include/x86_64-linux-gnu/c++/11",
                    "/usr/include",
                    "/usr/local/include",
                };
#endif

                for(const auto& base : systemPaths)
                {
                    appendIncludeEntries(base, includeCtx->dirPrefix,
                                         includeCtx->filePrefix, completionAll,
                                         seen);
                }
            }
            else
            {
                std::string currentDir = ".";
                if(currentBuffer && !currentBuffer->filename.empty())
                {
                    size_t lastSlash = currentBuffer->filename.rfind('/');
                    if(lastSlash != std::string::npos)
                        currentDir =
                            currentBuffer->filename.substr(0, lastSlash);
                }

                appendIncludeEntries(currentDir, includeCtx->dirPrefix,
                                     includeCtx->filePrefix, completionAll,
                                     seen);
            }

            if(completionAll.empty())
            {
                cancelCompletion();
                setStatusMessage("include completion: no results");
                return;
            }

            completionActive = true;
            rebuildCompletionFilter();
            needsFullRedraw = true;
            return;
        }
    }

    // Sync buffer text.
    std::string text;
    text.reserve(lines->size() * 80);
    for(size_t i = 0; i < lines->size(); ++i)
    {
        text += (*lines)[i];
        if(i + 1 < lines->size())
            text.push_back('\n');
    }
    client->didChange(currentBuffer->filename, text, languageId);

    auto items =
        client->completion(currentBuffer->filename, *cursorY, *cursorX);
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
        if(isRobotFile())
        {
            static constexpr std::string_view kRobotKeywords[] = {
                "*** Settings ***",
                "*** Variables ***",
                "*** Test Cases ***",
                "*** Tasks ***",
                "*** Keywords ***",
                "*** Comments ***",
                "Run Keyword",
                "Run Keyword And Return Status",
                "Run Keyword If",
                "Run Keywords",
                "Should Be Equal",
                "Should Contain",
                "Log",
                "Sleep",
                "FOR",
                "END",
                "IF",
                "ELSE",
                "ELSE IF",
                "TRY",
                "EXCEPT",
                "FINALLY",
            };
            keywordFallback(
                {std::begin(kRobotKeywords), std::end(kRobotKeywords)},
                "robot");
            return;
        }
        if(isPythonFile())
        {
            static constexpr std::string_view kPythonKeywords[] = {
                "and",    "as",       "assert",   "async", "await",  "break",
                "class",  "continue", "def",      "del",   "elif",   "else",
                "except", "False",    "finally",  "for",   "from",   "global",
                "if",     "import",   "in",       "is",    "lambda", "match",
                "case",   "None",     "nonlocal", "not",   "or",     "pass",
                "raise",  "return",   "True",     "try",   "while",  "with",
                "yield",
            };
            keywordFallback(
                {std::begin(kPythonKeywords), std::end(kPythonKeywords)},
                "python");
            return;
        }
        setStatusMessage(label + " completion: no results");
        return;
    }

    completionActive = true;
    completionSelected = 0;
    completionScroll = 0;
    rebuildCompletionFilter();
    needsFullRedraw = true;
#else
    setStatusMessage("LSP completion: not compiled in");
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
    completionAnchorX = computeCompletionAnchor(line, *cursorX);

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

    if(!insert.empty() && end < (int)mutableLine.size())
    {
        const char last = insert.back();
        if((last == ')' || last == ']' || last == '}') &&
           mutableLine[end] == last)
        {
            insert.pop_back(); // avoid double-closing when auto-braces inserted
        }
    }

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

    auto kindColor = [&](int kind) -> const std::string&
    {
        // LSP CompletionItemKind colors (rough semantic mapping)
        switch(kind)
        {
        case 2:  // Method
        case 3:  // Function
        case 4:  // Constructor
        case 23: // Event
            return theme.syntax(TOKEN_FUNCTION);
        case 5:  // Field
        case 6:  // Variable
        case 10: // Property
        case 18: // Reference
        case 25: // TypeParameter
            return theme.uiInfo();
        case 7:  // Class
        case 8:  // Interface
        case 13: // Enum
        case 22: // Struct
            return theme.syntax(TOKEN_TYPE);
        case 14: // Keyword
            return theme.syntax(TOKEN_KEYWORD);
        case 11: // Unit
        case 12: // Value
        case 20: // EnumMember
        case 21: // Constant
            return theme.uiWarning();
        case 24: // Operator
            return theme.syntax(TOKEN_OPERATOR);
        case 15: // Snippet
        case 16: // Color
            return theme.uiSuccess();
        case 17: // File
        case 19: // Folder
            return theme.uiDim();
        default:
            return theme.baseFg();
        }
    };

    auto appendSyntaxRow = [&](const std::string& text, bool selected, int kind)
    {
        if(!isCppFile())
        {
            if(selected)
                output += theme.selection();
            output += kindColor(kind);
            output += text;
            output += theme.reset();
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
            output += theme.selection();

        if(!hasColor)
        {
            output += kindColor(kind);
            output += text;
            output += theme.reset();
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

        output += theme.reset();
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
            output += theme.reset();

        output += " ";
        text_utils::appendU8(output, u8"│");
    }

    // Bottom border
    moveTo(top + 1 + maxRows, left);
    text_utils::appendU8(output, u8"└");
    text_utils::appendUtf8Repeat(output, u8"─", innerW + 2);
    text_utils::appendU8(output, u8"┐");
}
