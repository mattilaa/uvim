#include "ascii.h"
#include "editor.h"
#include "emoji_list.h"
#include "lsp_client.h"
#include "terminal.h"
#include "text_utils.h"
#include "widgets/completion_popup.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <unordered_map>
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
            if(text_utils::is_not_found(end))
                continue;

            std::string inner = s.substr(i + 2, end - (i + 2));
            // inner might be "1:foo" or "1"
            size_t colon = inner.find(':');
            if(text_utils::is_found(colon))
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

static bool cursorIsInsideDelimitedExpression(std::string_view left)
{
    int parenDepth = 0;
    int bracketDepth = 0;
    for(char ch : left)
    {
        if(ch == '(')
            ++parenDepth;
        else if(ch == ')' && parenDepth > 0)
            --parenDepth;
        else if(ch == '[')
            ++bracketDepth;
        else if(ch == ']' && bracketDepth > 0)
            --bracketDepth;
    }
    return parenDepth > 0 || bracketDepth > 0;
}

static bool completionCallShouldEndStatement(const std::string& line,
                                             int cursorPos)
{
    cursorPos = std::clamp(cursorPos, 0, (int)line.size());
    if(cursorIsInsideDelimitedExpression(
           std::string_view(line).substr(0, (size_t)cursorPos)))
        return false;

    for(int i = cursorPos; i < (int)line.size(); ++i)
    {
        if(line[i] != ' ' && line[i] != '\t')
            return false;
    }

    return true;
}

static std::string mlangBuiltinDetail(std::string_view label)
{
    if(label == "int")
        return "builtin type alias (int32_t)";
    if(label == "i32")
        return "builtin 32-bit signed integer";
    if(label == "i64")
        return "builtin 64-bit signed integer";
    if(label == "u32")
        return "builtin 32-bit unsigned integer";
    if(label == "u64")
        return "builtin 64-bit unsigned integer";
    if(label == "bool")
        return "builtin boolean type";
    if(label == "string")
        return "builtin UTF-8 string type";
    if(label == "fn")
        return "keyword: function definition";
    if(label == "let")
        return "keyword: immutable binding";
    if(label == "var")
        return "keyword: mutable binding";
    if(label == "if")
        return "keyword: conditional branch";
    if(label == "else")
        return "keyword: alternate branch";
    if(label == "for")
        return "keyword: loop over range/iterable";
    if(label == "return")
        return "keyword: return from function";
    if(label == "struct")
        return "keyword: struct type declaration";
    if(label == "enum")
        return "keyword: enum type declaration";
    if(label == "mod")
        return "keyword: module declaration";
    if(label == "use")
        return "keyword: import symbol/module";
    if(label == "match")
        return "keyword: pattern matching";
    if(label == "impl")
        return "keyword: implementation block";
    if(label == "extern")
        return "keyword: external symbol declaration";
    if(label == "pub")
        return "keyword: public visibility";
    if(label == "println!")
        return "builtin macro: print line to stdout";
    if(label == "print!")
        return "builtin macro: print to stdout";
    if(label == "eprintln!")
        return "builtin macro: print line to stderr";
    if(label == "eprint!")
        return "builtin macro: print to stderr";
    if(label == "debug!")
        return "builtin macro: debug print";
    if(label == "format!")
        return "builtin macro: format string";
    if(label == "assert_eq!")
        return "builtin macro: assert equality";
    return {};
}

static std::string mlangBuiltinDocumentation(std::string_view label)
{
    if(label == "int")
        return "Alias for a 32-bit signed integer. Maps to C int32_t in the "
               "runtime ABI.";
    if(label == "i32")
        return "Signed 32-bit integer type for arithmetic and integer APIs.";
    if(label == "i64")
        return "Signed 64-bit integer type for larger integer values.";
    if(label == "u32")
        return "Unsigned 32-bit integer type.";
    if(label == "u64")
        return "Unsigned 64-bit integer type.";
    if(label == "bool")
        return "Boolean type with true/false values.";
    if(label == "string")
        return "UTF-8 string type used by stdlib and formatting macros.";
    if(label == "fn")
        return "Starts a function declaration: fn name(params) -> ReturnType { "
               "... }";
    if(label == "if")
        return "Conditional branch. Executes block when condition is true.";
    if(label == "else")
        return "Alternate branch executed when the preceding if-condition is "
               "false.";
    if(label == "let")
        return "Introduce an immutable local binding.";
    if(label == "var")
        return "Introduce a mutable local binding.";
    if(label == "for")
        return "Loop construct, commonly used with ranges: for i in 0..n { ... "
               "}";
    if(label == "return")
        return "Return a value from the current function.";
    if(label == "match")
        return "Pattern matching expression over enum/option/result and "
               "literals.";
    if(label == "println!")
        return "Print formatted text to stdout and append a newline.";
    if(label == "print!")
        return "Print formatted text to stdout without a trailing newline.";
    if(label == "eprintln!")
        return "Print formatted text to stderr and append a newline.";
    if(label == "eprint!")
        return "Print formatted text to stderr without a trailing newline.";
    if(label == "debug!")
        return "Debug macro for development-time diagnostics.";
    if(label == "format!")
        return "Format macro that returns a string.";
    if(label == "assert_eq!")
        return "Assert that two expressions are equal.";
    return {};
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

static std::string stripEmojiSelectors(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for(size_t i = 0; i < s.size();)
    {
        unsigned char c = (unsigned char)s[i];
        int codepoint = 0;
        int len = 1;
        if(c < 0x80)
        {
            codepoint = c;
            len = 1;
        }
        else if((c & 0xE0) == 0xC0 && i + 1 < s.size())
        {
            codepoint = ((c & 0x1F) << 6) | ((unsigned char)s[i + 1] & 0x3F);
            len = 2;
        }
        else if((c & 0xF0) == 0xE0 && i + 2 < s.size())
        {
            codepoint = ((c & 0x0F) << 12) |
                        (((unsigned char)s[i + 1] & 0x3F) << 6) |
                        ((unsigned char)s[i + 2] & 0x3F);
            len = 3;
        }
        else if((c & 0xF8) == 0xF0 && i + 3 < s.size())
        {
            codepoint = ((c & 0x07) << 18) |
                        (((unsigned char)s[i + 1] & 0x3F) << 12) |
                        (((unsigned char)s[i + 2] & 0x3F) << 6) |
                        ((unsigned char)s[i + 3] & 0x3F);
            len = 4;
        }
        else
        {
            codepoint = c;
            len = 1;
        }

        if(codepoint != 0xFE0F && codepoint != 0xFE0E)
            out.append(s, i, len);
        i += len;
    }
    return out;
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
    if(text_utils::is_not_found(includePos))
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
    if(text_utils::is_found(closePos) && cursorX > (int)closePos)
        return std::nullopt;

    int pathStart = openPos + 1;
    int pathEnd = (text_utils::is_not_found(closePos))
                      ? cursorX
                      : std::min(cursorX, (int)closePos);
    if(pathEnd < pathStart)
        pathEnd = pathStart;

    std::string typed = line.substr(pathStart, pathEnd - pathStart);
    size_t lastSlash = typed.find_last_of("/\\");

    IncludeContext ctx;
    ctx.isSystem = isSystem;
    if(text_utils::is_not_found(lastSlash))
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
    static constexpr std::string_view kMlangBuiltins[] = {
        "int",       "i32",     "i64",    "u32",     "u64",        "bool",
        "string",    "fn",      "let",    "var",     "if",         "else",
        "for",       "return",  "struct", "enum",    "mod",        "use",
        "match",     "impl",    "extern", "pub",     "println!",   "print!",
        "eprintln!", "eprint!", "debug!", "format!", "assert_eq!",
    };
    auto keywordFallback =
        [&](const std::vector<std::string_view>& words, std::string_view label)
    {
        completionAll.clear();
        completionFiltered.clear();
        completionSelected = 0;
        completionScroll = 0;
        completionFromLsp = false;

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
        completionFromLsp = false;
        rebuildCompletionFilter();
        needsFullRedraw = true;
        setStatusMessage(std::string(label) + " completion: keywords");
    };
    auto bufferWordFallback = [&](std::string_view label)
    {
        completionAll.clear();
        completionFiltered.clear();
        completionSelected = 0;
        completionScroll = 0;
        completionFromLsp = false;

        auto isWordChar = [](char c)
        { return text_utils::isIdent(c) || c == '-' || c == '.'; };

        std::unordered_set<std::string> seen;
        completionAll.reserve(lines->size() * 2);

        for(const auto& l : *lines)
        {
            size_t i = 0;
            while(i < l.size())
            {
                while(i < l.size() && !isWordChar(l[i]))
                    ++i;
                size_t start = i;
                while(i < l.size() && isWordChar(l[i]))
                    ++i;
                if(start >= i)
                    continue;

                std::string word = l.substr(start, i - start);
                if(seen.insert(word).second)
                {
                    CompletionEntry e;
                    e.label = std::move(word);
                    completionAll.push_back(std::move(e));
                }
            }
        }

        if(completionAll.empty())
        {
            cancelCompletion();
            setStatusMessage("completion: no words");
            return;
        }

        completionActive = true;
        completionFromLsp = false;
        rebuildCompletionFilter();
        needsFullRedraw = true;
        setStatusMessage(std::string(label) + " completion: buffer words");
    };

    LspClient* client = nullptr;
    std::string label;
    std::string languageId;
    if(isFileType<FileType::Robot>())
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
    else if(isFileType<FileType::Python>())
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
    else if(isFileType<FileType::Mla>())
    {
        if(!isMlangLspEnabled())
        {
            setStatusMessage("mlang LSP: OFF");
            const std::string& line = (*lines)[*cursorY];
            completionAnchorX = computeCompletionAnchor(line, *cursorX);
            completionAnchorY = *cursorY;
            keywordFallback(
                {std::begin(kMlangBuiltins), std::end(kMlangBuiltins)},
                "mlang");
            return;
        }
        client = mlangLspClient.get();
        label = "mlang";
        languageId = "mlang";
    }
    else if(isFileType<FileType::Html>())
    {
        if(!isHtmlLspEnabled())
        {
            setStatusMessage("html LSP: OFF");
            const std::string& line = (*lines)[*cursorY];
            completionAnchorX = computeCompletionAnchor(line, *cursorX);
            completionAnchorY = *cursorY;
            bufferWordFallback("buffer");
            return;
        }
        client = htmlLspClient.get();
        label = "html";
        languageId = "html";
    }
    else if(isFileType<FileType::Css>())
    {
        if(!isCssLspEnabled())
        {
            setStatusMessage("css LSP: OFF");
            const std::string& line = (*lines)[*cursorY];
            completionAnchorX = computeCompletionAnchor(line, *cursorX);
            completionAnchorY = *cursorY;
            bufferWordFallback("buffer");
            return;
        }
        client = cssLspClient.get();
        label = "css";
        languageId = "css";
    }
    else if(isFileType<FileType::Json>())
    {
        if(!isJsonLspEnabled())
        {
            setStatusMessage("json LSP: OFF");
            const std::string& line = (*lines)[*cursorY];
            completionAnchorX = computeCompletionAnchor(line, *cursorX);
            completionAnchorY = *cursorY;
            bufferWordFallback("buffer");
            return;
        }
        client = jsonLspClient.get();
        label = "json";
        languageId = "json";
    }
    else if(isFileType<FileType::JavaScript>() ||
            isFileType<FileType::TypeScript>())
    {
        if(!isTsLspEnabled())
        {
            setStatusMessage("ts LSP: OFF");
            const std::string& line = (*lines)[*cursorY];
            completionAnchorX = computeCompletionAnchor(line, *cursorX);
            completionAnchorY = *cursorY;
            bufferWordFallback("buffer");
            return;
        }
        client = tsLspClient.get();
        label = "ts";
        languageId =
            isFileType<FileType::TypeScript>() ? "typescript" : "javascript";
    }
    else if(isFileType<FileType::Cpp>())
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
        const std::string& line = (*lines)[*cursorY];
        completionAnchorX = computeCompletionAnchor(line, *cursorX);
        completionAnchorY = *cursorY;
        bufferWordFallback("buffer");
        return;
    }

    const std::string& line = (*lines)[*cursorY];
    int ax = computeCompletionAnchor(line, *cursorX);

    completionAnchorX = ax;
    completionAnchorY = *cursorY;

    if(isFileType<FileType::Cpp>())
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
                static constexpr std::string_view systemPaths[] = {
#ifdef __APPLE__
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

                for(auto base : systemPaths)
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
                    if(text_utils::is_found(lastSlash))
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
            completionFromLsp = false;
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

    int triggerKind = 1;
    char triggerChar = '\0';
    if(*cursorX > 0 && line[*cursorX - 1] == '.')
    {
        triggerKind = 2;
        triggerChar = '.';
    }
    else if(*cursorX > 1 && line[*cursorX - 1] == ':' &&
            line[*cursorX - 2] == ':')
    {
        triggerKind = 2;
        triggerChar = ':';
    }
    else if(*cursorX > 1 && line[*cursorX - 1] == '>' &&
            line[*cursorX - 2] == '-')
    {
        triggerKind = 2;
        triggerChar = '>';
    }
    else if(completionActive && completionFromLsp)
    {
        triggerKind = 3;
    }

    auto items = client->completion(currentBuffer->filename, *cursorY, *cursorX,
                                    line, triggerKind, triggerChar);
    completionFromLsp = true;
    completionAll.clear();
    completionAll.reserve(items.size());

    for(const auto& ci : items)
    {
        CompletionEntry e;
        e.label = ci.label;
        e.insertText = ci.insertText;
        e.isSnippet = ci.isSnippet;
        e.kind = ci.kind;
        e.detail = ci.detail;
        e.labelDetail = ci.labelDetail;
        e.labelDescription = ci.labelDescription;
        e.documentation = ci.documentation;
        if(e.detail.empty())
            e.detail = mlangBuiltinDetail(e.label);
        if(e.documentation.empty())
            e.documentation = mlangBuiltinDocumentation(e.label);
        completionAll.push_back(std::move(e));
    }

    if(isFileType<FileType::Mla>())
    {
        std::unordered_set<std::string> seen;
        seen.reserve(completionAll.size() * 2 + 8);
        for(const auto& e : completionAll)
            seen.insert(e.label);
        for(const auto& kw : kMlangBuiltins)
        {
            std::string labelStr(kw);
            if(seen.insert(labelStr).second)
            {
                CompletionEntry e;
                e.label = std::move(labelStr);
                e.detail = mlangBuiltinDetail(e.label);
                e.documentation = mlangBuiltinDocumentation(e.label);
                completionAll.push_back(std::move(e));
            }
        }
    }

    if(completionAll.empty())
    {
        cancelCompletion();
        if(isFileType<FileType::Robot>())
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
        if(isFileType<FileType::Python>())
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
        if(isFileType<FileType::Mla>())
        {
            keywordFallback(
                {std::begin(kMlangBuiltins), std::end(kMlangBuiltins)},
                "mlang");
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
    completionFromLsp = false;
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

    bool functionLike = completionAutoParens && !it.isSnippet;
    if(functionLike)
    {
        bool hasParen = text_utils::contains(it.label, '(') ||
                        text_utils::contains(insert, '(');
        functionLike = hasParen;
    }
    if(functionLike)
    {
        auto baseFrom = [&](const std::string& text) -> std::string
        {
            size_t pos = text.find('(');
            if(text_utils::is_not_found(pos))
                return text;
            return text.substr(0, pos);
        };
        std::string name = baseFrom(insert);
        if(name == insert && text_utils::contains(it.label, '('))
            name = baseFrom(it.label);
        if(!name.empty())
            insert = name;
        else
            functionLike = false;
    }

    // Replace prefix [anchorX, cursorX)
    std::string& mutableLine = (*lines)[*cursorY];
    int start =
        std::max(0, std::min(completionAnchorX, (int)mutableLine.size()));
    int end = std::max(0, std::min(*cursorX, (int)mutableLine.size()));
    if(end < start)
        std::swap(start, end);

    if(!insert.empty() && std::isspace((unsigned char)insert.front()))
    {
        bool prevIsSpace =
            start > 0 &&
            std::isspace((unsigned char)mutableLine[start - 1]) != 0;
        if(!prevIsSpace)
        {
            size_t first = insert.find_first_not_of(" \t");
            if(text_utils::is_found(first))
                insert.erase(0, first);
        }
    }

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

    if(functionLike)
    {
        int cursorPos = *cursorX;
        bool appendSemicolon =
            completionCallShouldEndStatement(mutableLine, cursorPos);
        if(cursorPos < (int)mutableLine.size() && mutableLine[cursorPos] == '(')
        {
            cursorPos += 1;
        }
        else
        {
            mutableLine.insert(cursorPos, appendSemicolon ? "();" : "()");
            cursorPos += 1;
        }

        int closePos = -1;
        for(int i = cursorPos; i < (int)mutableLine.size(); ++i)
        {
            if(mutableLine[i] == ')')
            {
                closePos = i;
                break;
            }
        }
        if(closePos >= 0)
        {
            bool hasNonSpaceAfter = false;
            for(int i = closePos + 1; i < (int)mutableLine.size(); ++i)
            {
                char ch = mutableLine[i];
                if(ch != ' ' && ch != '\t')
                {
                    hasNonSpaceAfter = true;
                    break;
                }
            }
            if(appendSemicolon && !hasNonSpaceAfter)
            {
                if(closePos + 1 >= (int)mutableLine.size() ||
                   mutableLine[closePos + 1] != ';')
                {
                    mutableLine.insert(closePos + 1, ";");
                }
            }
        }
        *cursorX = cursorPos;
        *wantedX = *cursorX;
    }
    else if(!insert.empty() && *cursorX < (int)mutableLine.size())
    {
        const char last = insert.back();
        if((last == ')' || last == ']' || last == '}') &&
           mutableLine[*cursorX] == last)
        {
            mutableLine.erase(*cursorX, 1);
        }
    }

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
        // Qualify explicitly instead of shared file-list scoring.
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
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
    widgets::drawCompletionPopup(output, *this);
#else
    (void)output;
#endif
}

void Editor::openEmojiPopup()
{
    if(emojiEntries.empty())
    {
        emojiEntries.reserve(std::size(kEmojiList));
        for(const auto& e : kEmojiList)
        {
            Editor::EmojiPopupEntry entry;
            entry.emoji = e.emoji;
            entry.name = e.name;
            entry.emojiDisplay = stripEmojiSelectors(entry.emoji);
            entry.label = entry.emojiDisplay + std::string(" ") + entry.name;
            emojiEntries.push_back(std::move(entry));
        }
    }

    if(emojiEntries.empty())
    {
        setStatusMessage("emoji: no entries");
        return;
    }

    emojiPopupActive = true;
    emojiQuery.clear();
    rebuildEmojiFilter();
}

void Editor::cancelEmojiPopup()
{
    emojiPopupActive = false;
    emojiQuery.clear();
    emojiFiltered.clear();
    emojiSelected = 0;
    emojiScroll = 0;
    emojiPopupLastValid = false;
    needsFullRedraw = true;
}

void Editor::acceptEmoji()
{
    if(!emojiPopupActive || emojiFiltered.empty())
        return;

    const std::string& emoji = emojiEntries[emojiFiltered[emojiSelected]].emoji;
    if(lines)
    {
        if(*cursorY >= (int)lines->size())
            lines->push_back("");

        std::string& line = (*lines)[*cursorY];
        int insertPos = std::clamp(*cursorX, 0, (int)line.size());

        // In normal mode, insert after the current character (like `a`).
        if(currentMode == NORMAL && insertPos < (int)line.size())
        {
            if(utf8Mode)
                insertPos = text_utils::nextUtf8CharStart(line, insertPos);
            else
                ++insertPos;
        }

        line.insert(insertPos, emoji);
        if(currentMode == NORMAL)
            *cursorX = insertPos;
        else
            *cursorX = insertPos + (int)emoji.size();
        *dirty = true;
        saveState();
        currentBuffer->lspSyncNeeded = true;
        needsFullRedraw = true;
        *wantedX = *cursorX;
    }

    cancelEmojiPopup();
}

void Editor::emojiNext()
{
    if(!emojiPopupActive || emojiFiltered.empty())
        return;
    emojiSelected = (emojiSelected + 1) % (int)emojiFiltered.size();

    const int win = std::min(8, (int)emojiFiltered.size());
    if(emojiSelected < emojiScroll)
        emojiScroll = emojiSelected;
    else if(emojiSelected >= emojiScroll + win)
        emojiScroll = emojiSelected - win + 1;
}

void Editor::emojiPrev()
{
    if(!emojiPopupActive || emojiFiltered.empty())
        return;
    emojiSelected = (emojiSelected - 1);
    if(emojiSelected < 0)
        emojiSelected = (int)emojiFiltered.size() - 1;

    const int win = std::min(8, (int)emojiFiltered.size());
    if(emojiSelected < emojiScroll)
        emojiScroll = emojiSelected;
    else if(emojiSelected >= emojiScroll + win)
        emojiScroll = emojiSelected - win + 1;
}

void Editor::rebuildEmojiFilter()
{
    emojiFiltered.clear();
    emojiSelected = 0;
    emojiScroll = 0;

    if(emojiEntries.empty())
        return;

    struct Scored
    {
        int idx;
        int score;
    };

    std::vector<Scored> scored;
    scored.reserve(emojiEntries.size());

    for(int i = 0; i < (int)emojiEntries.size(); ++i)
    {
        const auto& e = emojiEntries[i];
        int s = ::fuzzyScore(e.name, emojiQuery);
        if(s >= 0)
            scored.push_back({i, s});
    }

    std::stable_sort(scored.begin(), scored.end(),
                     [](const Scored& x, const Scored& y)
                     { return x.score > y.score; });

    emojiFiltered.reserve(scored.size());
    for(const auto& s : scored)
        emojiFiltered.push_back(s.idx);
}

void Editor::drawEmojiPopup(std::string& output) const
{
    if(!emojiPopupActive || currentMode != NORMAL)
        return;

    const int totalItems = (int)emojiFiltered.size();
    const int maxRows = std::min({8, std::max(1, totalItems), screenRows - 3});
    if(maxRows <= 0)
        return;

    int cy = (*cursorY - *offsetY) + 1 + tabBarRows();
    int cx = (*cursorX - *offsetX) + 1 + gutterWidth();
    if(cy < 1)
        cy = 1;
    if(cy > screenRows)
        cy = screenRows;
    if(cx < 1)
        cx = 1;
    if(cx > screenCols)
        cx = screenCols;

    const std::string queryLabel = "emoji: " + emojiQuery;
    int innerW = kEmojiPopupMaxWidth;
    int totalW = innerW + 4;
    if(totalW > screenCols)
    {
        totalW = screenCols;
        innerW = std::max(4, totalW - 4);
    }

    const int innerRows = maxRows + 1; // header + list
    int totalH = innerRows + 2;
    int top = cy + 1;
    if(top + totalH - 1 > screenRows)
        top = cy - totalH + 1;
    if(top < 1)
        top = 1;

    int left = cx;
    if(left + totalW - 1 > screenCols)
        left = std::max(1, screenCols - totalW + 1);

    auto moveTo = [&](int r, int c) { output += Terminal::cursorPos(r, c); };

    if(emojiPopupLastValid)
    {
        for(int r = 0; r < emojiPopupLastHeight; ++r)
        {
            moveTo(emojiPopupLastTop + r, emojiPopupLastLeft);
            output.append(emojiPopupLastWidth, ' ');
        }
    }

    for(int r = 0; r < totalH; ++r)
    {
        moveTo(top + r, left);
        output.append(totalW, ' ');
    }

    moveTo(top, left);
    text_utils::appendU8(output, ascii::BOX_LIGHT_TOP_LEFT);
    text_utils::appendUtf8Repeat(output, ascii::BOX_LIGHT_HORIZONTAL,
                                 innerW + 2);
    text_utils::appendU8(output, ascii::BOX_LIGHT_TOP_RIGHT);

    moveTo(top + 1, left);
    text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
    output += " ";
    std::string header = queryLabel;
    while(displayWidth(header) > innerW)
        header.pop_back();
    output += header;
    moveTo(top + 1, left + totalW - 1);
    text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);

    for(int i = 0; i < maxRows; ++i)
    {
        int idx = emojiScroll + i;
        if(idx >= totalItems && !emojiFiltered.empty())
            break;
        const bool hasSelection = !emojiFiltered.empty();
        const int entryIndex = hasSelection ? emojiFiltered[idx] : -1;

        moveTo(top + 2 + i, left);
        text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
        output += " ";

        bool sel = hasSelection && (idx == emojiSelected);
        if(sel)
            output += theme.selection();

        std::string row;
        if(hasSelection)
        {
            const auto& e = emojiEntries[entryIndex];
            int nameAvail = innerW - (emojiGlyphWidth(e.emoji.c_str()) + 1);
            if(nameAvail <= 0)
            {
                row = e.emojiDisplay;
            }
            else
            {
                std::string name = e.name;
                if((int)name.size() > nameAvail)
                    name = name.substr(0, nameAvail);
                row = e.emojiDisplay + std::string(" ") + name;
            }
        }
        else
        {
            row = "no matches";
        }

        output += row;

        if(sel)
            output += theme.reset();

        moveTo(top + 2 + i, left + totalW - 1);
        text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
    }

    moveTo(top + 1 + innerRows, left);
    text_utils::appendU8(output, ascii::BOX_LIGHT_BOTTOM_LEFT);
    text_utils::appendUtf8Repeat(output, ascii::BOX_LIGHT_HORIZONTAL,
                                 innerW + 2);
    text_utils::appendU8(output, ascii::BOX_LIGHT_BOTTOM_RIGHT);

    emojiPopupLastTop = top;
    emojiPopupLastLeft = left;
    emojiPopupLastWidth = totalW;
    emojiPopupLastHeight = totalH;
    emojiPopupLastValid = true;
}

std::unordered_map<int, Editor::LspDiagnosticSummary>
Editor::getClangdDiagnosticsByLine() const
{
    std::unordered_map<int, LspDiagnosticSummary> out;
#ifdef UVIM_ENABLE_CLANGD_LSP
    if(showGitBlame)
        return out;
    if(currentMode == INSERT)
        return out;
    if(!isClangdLspEnabled() || !lspClient || !currentBuffer)
        return out;
    if(currentBuffer->filename.empty())
        return out;

    std::vector<LspClient::Diagnostic> diagnostics =
        lspClient->diagnostics(currentBuffer->filename);
    for(const auto& diag : diagnostics)
    {
        if(diag.severity <= 0 || diag.severity > 2)
            continue;
        auto& slot = out[diag.line];
        if(slot.severity == 0 || diag.severity < slot.severity ||
           (diag.severity == slot.severity && diag.character < slot.character))
        {
            slot.severity = diag.severity;
            slot.character = diag.character;
            slot.message = diag.message;
        }
    }
#endif
    return out;
}

std::optional<Editor::LspDiagnosticSummary>
Editor::getClangdDiagnosticForLine(int line) const
{
    std::unordered_map<int, LspDiagnosticSummary> byLine =
        getClangdDiagnosticsByLine();
    auto it = byLine.find(line);
    if(it == byLine.end())
        return std::nullopt;
    return it->second;
}

static inline int diagnosticDisplayWidth(const std::string& s)
{
    int w = 0;
    for(size_t i = 0; i < s.size();)
    {
        unsigned char c = (unsigned char)s[i];
        if(c < 0x80)
        {
            ++w;
            ++i;
            continue;
        }
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

void Editor::drawDiagnosticPopup(std::string& output) const
{
    if(showGitBlame)
        return;
    if(currentMode == COMMAND || currentMode == SEARCH_FORWARD ||
       currentMode == SEARCH_BACKWARD)
        return;
    if(completionActive && currentMode == INSERT)
        return;
    if(!currentBuffer)
        return;
    if(!diagnosticPopupActive)
        return;
    if(diagnosticPopupData.severity <= 0 || diagnosticPopupData.severity > 2)
        return;

    std::string label =
        (diagnosticPopupData.severity == 1) ? "error" : "warning";
    std::string message = diagnosticPopupData.message;
    if(message.empty())
        return;

    std::vector<std::string> rows;
    rows.push_back(label + ": " + message);
    if(!diagnosticPopupFixes.empty())
    {
        rows.push_back("fixes:");
        int maxFixRows = std::min(6, (int)diagnosticPopupFixes.size());
        int start = std::clamp(
            diagnosticPopupFixScroll, 0,
            std::max(0, (int)diagnosticPopupFixes.size() - maxFixRows));
        int end =
            std::min((int)diagnosticPopupFixes.size(), start + maxFixRows);
        for(int i = start; i < end; ++i)
        {
            rows.push_back(diagnosticPopupFixes[i].title);
        }
    }

    int innerW = 0;
    for(const auto& row : rows)
        innerW = std::max(innerW, diagnosticDisplayWidth(row));
    int maxInner = std::max(10, screenCols - 4);
    if(innerW > maxInner)
    {
        int trim = std::max(0, maxInner - 3);
        for(auto& row : rows)
        {
            if(trim > 0 && trim < (int)row.size())
            {
                row = row.substr(0, (size_t)trim) + "...";
            }
        }
        innerW = 0;
        for(const auto& row : rows)
            innerW = std::max(innerW, diagnosticDisplayWidth(row));
    }

    int totalW = innerW + 4;
    if(totalW > screenCols)
    {
        totalW = screenCols;
        innerW = std::max(1, totalW - 4);
    }
    if(!rows.empty() && diagnosticDisplayWidth(rows[0]) > innerW)
    {
        int trim = std::max(0, innerW - 3);
        for(auto& row : rows)
        {
            if(trim > 0 && trim < (int)row.size())
            {
                row = row.substr(0, (size_t)trim) + "...";
            }
        }
        innerW = 0;
        for(const auto& row : rows)
            innerW = std::max(innerW, diagnosticDisplayWidth(row));
    }

    int totalH = (int)rows.size() + 2;
    int cy = (*cursorY - *offsetY) + 1 + tabBarRows();
    int cx = (*cursorX - *offsetX) + 1 + gutterWidth();
    if(cy < 1)
        cy = 1;
    if(cy > screenRows)
        cy = screenRows;
    if(cx < 1)
        cx = 1;
    if(cx > screenCols)
        cx = screenCols;

    int top = cy + 1;
    if(top + totalH - 1 > screenRows)
        top = cy - totalH;
    if(top < 1)
        top = 1;

    int left = cx;
    if(left + totalW - 1 > screenCols)
        left = std::max(1, screenCols - totalW + 1);

    auto moveTo = [&](int r, int c) { output += Terminal::cursorPos(r, c); };
    const std::string& color = (diagnosticPopupData.severity == 1)
                                   ? theme.uiError()
                                   : theme.uiWarning();

    moveTo(top, left);
    text_utils::appendU8(output, ascii::BOX_LIGHT_TOP_LEFT);
    text_utils::appendUtf8Repeat(output, ascii::BOX_LIGHT_HORIZONTAL,
                                 innerW + 2);
    text_utils::appendU8(output, ascii::BOX_LIGHT_TOP_RIGHT);

    moveTo(top + 1, left);
    for(size_t i = 0; i < rows.size(); ++i)
    {
        text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
        output += " ";
        if(i == 0)
        {
            output += color;
        }
        else if(i == 1 && !diagnosticPopupFixes.empty())
        {
            output += theme.uiDim();
        }
        else if(!diagnosticPopupFixes.empty())
        {
            int fixIndex = (int)i - 2 + diagnosticPopupFixScroll;
            if(fixIndex == diagnosticPopupFixIndex)
                output += theme.selection();
            else
                output += theme.uiInfo();
        }

        output += rows[i];
        output += theme.reset();
        int pad = innerW - diagnosticDisplayWidth(rows[i]);
        if(pad > 0)
            output.append(pad, ' ');
        output += " ";
        text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
        if(i + 1 < rows.size())
            moveTo(top + 1 + (int)i + 1, left);
    }

    moveTo(top + 1 + (int)rows.size(), left);
    text_utils::appendU8(output, ascii::BOX_LIGHT_BOTTOM_LEFT);
    text_utils::appendUtf8Repeat(output, ascii::BOX_LIGHT_HORIZONTAL,
                                 innerW + 2);
    text_utils::appendU8(output, ascii::BOX_LIGHT_BOTTOM_RIGHT);
}

void Editor::drawSymbolPopup(std::string& output) const
{
    if(currentMode == COMMAND || currentMode == SEARCH_FORWARD ||
       currentMode == SEARCH_BACKWARD)
        return;
    if(completionActive && currentMode == INSERT)
        return;
    if(!currentBuffer)
        return;
    if(!symbolPopupActive || symbolPopupText.empty())
        return;

    struct PopupRow
    {
        std::string text;
        bool padding = false;
        size_t colon = std::string::npos;
    };

    auto makeRow = [](std::string text)
    {
        PopupRow row;
        row.text = std::move(text);
        row.colon = row.text.find(':');
        const size_t firstNonSpace = row.text.find_first_not_of(' ');
        row.padding = firstNonSpace != std::string::npos &&
                      row.text.rfind("pad:", firstNonSpace) == firstNonSpace;
        return row;
    };

    std::vector<PopupRow> rows;
    size_t start = 0;
    while(start <= symbolPopupText.size())
    {
        size_t end = symbolPopupText.find('\n', start);
        if(end == std::string::npos)
        {
            rows.push_back(makeRow(symbolPopupText.substr(start)));
            break;
        }
        rows.push_back(makeRow(symbolPopupText.substr(start, end - start)));
        start = end + 1;
    }
    if(rows.empty())
        return;

    int innerW = 0;
    for(const PopupRow& row : rows)
        innerW = std::max(innerW, text_utils::displayWidth(row.text));

    int maxInner = std::max(10, screenCols - 4);
    if(innerW > maxInner)
    {
        for(PopupRow& row : rows)
        {
            if(text_utils::displayWidth(row.text) <= maxInner)
                continue;
            int trim = std::max(0, maxInner - 3);
            if(trim > 0 && trim < (int)row.text.size())
            {
                row.text = row.text.substr(0, (size_t)trim) + "...";
                row.colon = std::string::npos;
                row.padding = false;
            }
        }
        innerW = 0;
        for(const PopupRow& row : rows)
            innerW = std::max(innerW, text_utils::displayWidth(row.text));
    }

    int totalW = innerW + 4;
    if(totalW > screenCols)
    {
        totalW = screenCols;
        innerW = std::max(1, totalW - 4);
    }

    int maxRows = std::max(1, screenRows - 2);
    if(symbolPopupScroll > 0 && (int)rows.size() > maxRows)
    {
        const int firstRow =
            std::min(symbolPopupScroll, (int)rows.size() - maxRows);
        rows.erase(rows.begin(), rows.begin() + firstRow);
    }
    if((int)rows.size() > maxRows)
        rows.resize((size_t)maxRows);

    int totalH = (int)rows.size() + 2;
    int cy = (*cursorY - *offsetY) + 1 + tabBarRows();
    int cx = (*cursorX - *offsetX) + 1 + gutterWidth();
    if(cy < 1)
        cy = 1;
    if(cy > screenRows)
        cy = screenRows;
    if(cx < 1)
        cx = 1;
    if(cx > screenCols)
        cx = screenCols;

    int top = cy + 1;
    if(top + totalH - 1 > screenRows)
        top = cy - totalH;
    if(top < 1)
        top = 1;

    int left = cx;
    if(left + totalW - 1 > screenCols)
        left = std::max(1, screenCols - totalW + 1);

    auto moveTo = [&](int r, int c) { output += Terminal::cursorPos(r, c); };

    moveTo(top, left);
    text_utils::appendU8(output, ascii::BOX_LIGHT_TOP_LEFT);
    text_utils::appendUtf8Repeat(output, ascii::BOX_LIGHT_HORIZONTAL,
                                 innerW + 2);
    text_utils::appendU8(output, ascii::BOX_LIGHT_TOP_RIGHT);

    for(size_t i = 0; i < rows.size(); ++i)
    {
        moveTo(top + 1 + (int)i, left);
        text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
        output += " ";
        output += theme.panel();
        const PopupRow& row = rows[i];
        if(row.colon != std::string::npos)
        {
            output +=
                row.padding ? theme.uiWarning() : theme.syntax(TOKEN_MEMBER);
            output += row.text.substr(0, row.colon);
            output += theme.panel();
            output += row.text.substr(row.colon, 2);
            output += theme.baseFg();
            output += row.text.substr(std::min(row.colon + 2, row.text.size()));
        }
        else
        {
            output += row.text;
        }
        output += theme.reset();
        int pad = innerW - text_utils::displayWidth(row.text);
        if(pad > 0)
            output.append(pad, ' ');
        output += " ";
        text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
    }

    moveTo(top + 1 + (int)rows.size(), left);
    text_utils::appendU8(output, ascii::BOX_LIGHT_BOTTOM_LEFT);
    text_utils::appendUtf8Repeat(output, ascii::BOX_LIGHT_HORIZONTAL,
                                 innerW + 2);
    text_utils::appendU8(output, ascii::BOX_LIGHT_BOTTOM_RIGHT);
}

void Editor::openDiagnosticPopupForCursor()
{
    diagnosticPopupActive = false;
    diagnosticPopupLine = -1;
    diagnosticPopupCursorX = -1;
    diagnosticPopupCursorY = -1;
    diagnosticPopupData = {};
    diagnosticPopupFixes.clear();
    diagnosticPopupFixIndex = 0;
    diagnosticPopupFixScroll = 0;

    if(!currentBuffer)
        return;

    syncClangdDiagnosticsIfNeeded(true);

    std::optional<LspDiagnosticSummary> diag =
        getClangdDiagnosticForLine(*cursorY);
    if(!diag || diag->severity <= 0 || diag->severity > 2)
    {
        setStatusMessage("No diagnostics on this line");
        needsFullRedraw = true;
        return;
    }

    diagnosticPopupActive = true;
    diagnosticPopupLine = *cursorY;
    diagnosticPopupCursorX = *cursorX;
    diagnosticPopupCursorY = *cursorY;
    diagnosticPopupData = *diag;

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(isClangdLspEnabled() && lspClient && currentBuffer &&
       !currentBuffer->filename.empty())
    {
        std::vector<LspClient::Diagnostic> diags =
            lspClient->diagnostics(currentBuffer->filename);
        std::vector<LspClient::Diagnostic> lineDiags;
        for(const auto& d : diags)
        {
            if(d.line == *cursorY && (d.severity == 1 || d.severity == 2))
                lineDiags.push_back(d);
        }
        if(!lineDiags.empty())
        {
            std::string_view lineText;
            if(*cursorY >= 0 && *cursorY < (int)lines->size())
                lineText = (*lines)[*cursorY];
            std::vector<LspClient::CodeAction> actions = lspClient->codeActions(
                currentBuffer->filename, *cursorY, lineText, lineDiags);
            for(const auto& action : actions)
            {
                DiagnosticFix fix;
                fix.title = action.title;
                for(const auto& edit : action.edits)
                {
                    DiagnosticFixEdit e;
                    e.startLine = edit.startLine;
                    e.startCharacter = edit.startCharacter;
                    e.endLine = edit.endLine;
                    e.endCharacter = edit.endCharacter;
                    e.newText = edit.newText;
                    fix.edits.push_back(std::move(e));
                }
                fix.command = action.command;
                fix.commandArgsJson = action.commandArgsJson;
                if(!fix.edits.empty() || !fix.command.empty())
                    diagnosticPopupFixes.push_back(std::move(fix));
            }
        }
    }
#endif

    needsFullRedraw = true;
}

void Editor::closeDiagnosticPopup()
{
    if(!diagnosticPopupActive)
        return;
    diagnosticPopupActive = false;
    diagnosticPopupLine = -1;
    diagnosticPopupCursorX = -1;
    diagnosticPopupCursorY = -1;
    diagnosticPopupData = {};
    diagnosticPopupFixes.clear();
    diagnosticPopupFixIndex = 0;
    diagnosticPopupFixScroll = 0;
    needsFullRedraw = true;
}

static int utf16ToUtf8ByteOffset(const std::string& line, int utf16Offset)
{
    if(utf16Offset <= 0)
        return 0;
    int u16 = 0;
    int i = 0;
    while(i < (int)line.size())
    {
        unsigned char c = (unsigned char)line[i];
        int codepoint = 0;
        int len = 1;

        if(c < 0x80)
        {
            codepoint = c;
            len = 1;
        }
        else if((c & 0xE0) == 0xC0 && i + 1 < (int)line.size())
        {
            codepoint = ((c & 0x1F) << 6) | ((unsigned char)line[i + 1] & 0x3F);
            len = 2;
        }
        else if((c & 0xF0) == 0xE0 && i + 2 < (int)line.size())
        {
            codepoint = ((c & 0x0F) << 12) |
                        (((unsigned char)line[i + 1] & 0x3F) << 6) |
                        ((unsigned char)line[i + 2] & 0x3F);
            len = 3;
        }
        else if((c & 0xF8) == 0xF0 && i + 3 < (int)line.size())
        {
            codepoint = ((c & 0x07) << 18) |
                        (((unsigned char)line[i + 1] & 0x3F) << 12) |
                        (((unsigned char)line[i + 2] & 0x3F) << 6) |
                        ((unsigned char)line[i + 3] & 0x3F);
            len = 4;
        }

        int u16len = (codepoint <= 0xFFFF) ? 1 : 2;
        if(u16 + u16len > utf16Offset)
            break;

        u16 += u16len;
        i += len;
    }
    return i;
}

static std::string joinLinesForLsp(const std::vector<std::string>& source)
{
    std::string text;
    for(size_t i = 0; i < source.size(); ++i)
    {
        text += source[i];
        if(i + 1 < source.size())
            text.push_back('\n');
    }
    return text;
}

static bool applyRenameEditToLines(std::vector<std::string>& targetLines,
                                   const Editor::RenameEdit& edit)
{
    if(edit.startLine < 0 || edit.startLine >= (int)targetLines.size())
        return false;
    if(edit.endLine < 0 || edit.endLine >= (int)targetLines.size())
        return false;

    std::string& startLine = targetLines[edit.startLine];
    std::string& endLine = targetLines[edit.endLine];
    int startByte = utf16ToUtf8ByteOffset(startLine, edit.startCharacter);
    int endByte = utf16ToUtf8ByteOffset(endLine, edit.endCharacter);

    if(edit.startLine == edit.endLine)
    {
        startLine = startLine.substr(0, startByte) + edit.newText +
                    endLine.substr(endByte);
        return true;
    }

    std::string prefix = startLine.substr(0, startByte);
    std::string suffix = endLine.substr(endByte);
    std::string combined = prefix + edit.newText + suffix;

    std::vector<std::string> newLines;
    size_t pos = 0;
    while(pos <= combined.size())
    {
        size_t next = combined.find('\n', pos);
        if(text_utils::is_not_found(next))
        {
            newLines.push_back(combined.substr(pos));
            break;
        }
        newLines.push_back(combined.substr(pos, next - pos));
        pos = next + 1;
    }

    targetLines.erase(targetLines.begin() + edit.startLine,
                      targetLines.begin() + edit.endLine + 1);
    targetLines.insert(targetLines.begin() + edit.startLine, newLines.begin(),
                       newLines.end());
    if(targetLines.empty())
        targetLines.push_back("");
    return true;
}

static std::vector<std::string> readFileLinesForRename(const std::string& path)
{
    std::vector<std::string> out;
    std::ifstream file(path);
    std::string line;
    while(std::getline(file, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        out.push_back(line);
    }
    if(out.empty())
        out.push_back("");
    return out;
}

static bool writeFileLinesForRename(const std::string& path,
                                    const std::vector<std::string>& lines)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if(!file)
        return false;
    for(size_t i = 0; i < lines.size(); ++i)
    {
        file << lines[i];
        if(i + 1 < lines.size())
            file << '\n';
    }
    return true;
}

Buffer* findBufferForPath(Editor& editor, const std::string& path)
{
    std::error_code targetEc;
    auto target = std::filesystem::weakly_canonical(path, targetEc);
    for(auto& buffer : editor.buffers)
    {
        if(!buffer)
            continue;
        std::error_code ec;
        auto candidate =
            std::filesystem::weakly_canonical(buffer->filename, ec);
        if((!targetEc && !ec && candidate == target) ||
           buffer->filename == path)
            return buffer.get();
    }
    return nullptr;
}

static std::string renameDisplayPath(const Editor& editor,
                                     const std::string& path)
{
    if(!editor.projectRoot.empty() && path.rfind(editor.projectRoot, 0) == 0)
    {
        size_t skip = editor.projectRoot.size();
        if(skip < path.size() && (path[skip] == '/' || path[skip] == '\\'))
            ++skip;
        return path.substr(skip);
    }
    return path;
}

static void captureRenameUndoSnapshot(Editor& editor)
{
    if(editor.renameUndoAvailable)
        return;

    editor.clearRenameUndoSnapshot();
    for(const auto& file : editor.renameFiles)
    {
        Editor::RenameUndoFileSnapshot snapshot;
        snapshot.path = file.path;

        if(Buffer* buffer = findBufferForPath(editor, file.path))
        {
            snapshot.hadOpenBuffer = true;
            snapshot.fileExisted = true;
            snapshot.lines = buffer->lines;
            snapshot.dirty = buffer->dirty;
            snapshot.cursorX = buffer->cursorX;
            snapshot.cursorY = buffer->cursorY;
            snapshot.offsetX = buffer->offsetX;
            snapshot.offsetY = buffer->offsetY;
        }
        else
        {
            snapshot.fileExisted = std::filesystem::exists(file.path);
            if(snapshot.fileExisted)
                snapshot.lines = readFileLinesForRename(file.path);
        }

        editor.renameUndoFiles.push_back(std::move(snapshot));
    }
    editor.renameUndoAvailable = !editor.renameUndoFiles.empty();
}

void Editor::closeRenamePopup()
{
    renamePopupActive = false;
    renamePopupReady = false;
    renameOriginal.clear();
    renameInput.clear();
    renameStatus.clear();
    renameCursorX = -1;
    renameCursorY = -1;
    renameCurrentFile = 0;
    renameCurrentEdit = 0;
    renameFiles.clear();
    needsFullRedraw = true;
}

void Editor::openRenamePopupForCursor()
{
    closeRenamePopup();
    clearRenameUndoSnapshot();
    if(!currentBuffer || !lines || !cursorX || !cursorY)
    {
        setStatusMessage("rn: no buffer");
        return;
    }
#ifdef UVIM_ENABLE_CLANGD_LSP
    if(!isClangdLspEnabled() || !isFileType<FileType::Cpp>() ||
       isFileType<FileType::Mla>() || !lspClient)
    {
        setStatusMessage("rn: clangd rename unavailable");
        return;
    }
#else
    setStatusMessage("rn: clangd rename unavailable");
    return;
#endif

    renameOriginal = getSymbolUnderCursor();
    if(renameOriginal.empty())
    {
        setStatusMessage("rn: no symbol under cursor");
        return;
    }

    renameInput = renameOriginal;
    renameCursorX = *cursorX;
    renameCursorY = *cursorY;
    renamePopupActive = true;
    renamePopupReady = false;
    renameStatus = "Enter new name";
    needsFullRedraw = true;
}

static int renameRemainingEditCount(const Editor::RenameFileEdits& file)
{
    return (int)std::count_if(file.edits.begin(), file.edits.end(),
                              [](const Editor::RenameEdit& edit)
                              { return !edit.applied; });
}

static int
renameTotalRemainingEditCount(const std::vector<Editor::RenameFileEdits>& files)
{
    int total = 0;
    for(const auto& file : files)
        total += renameRemainingEditCount(file);
    return total;
}

static bool renameSelectNextPending(Editor& editor)
{
    for(int f = 0; f < (int)editor.renameFiles.size(); ++f)
    {
        if(renameRemainingEditCount(editor.renameFiles[f]) <= 0)
            continue;
        editor.renameCurrentFile = f;
        for(int e = 0; e < (int)editor.renameFiles[f].edits.size(); ++e)
        {
            if(!editor.renameFiles[f].edits[e].applied)
            {
                editor.renameCurrentEdit = e;
                return true;
            }
        }
    }
    return false;
}

static bool applyRenameEdit(Editor& editor, Editor::RenameEdit& edit)
{
    if(edit.applied)
        return false;

    if(Buffer* buffer = findBufferForPath(editor, edit.path))
    {
        bool ok = applyRenameEditToLines(buffer->lines, edit);
        if(!ok)
            return false;
        buffer->dirty = true;
        buffer->lspSyncNeeded = true;
        buffer->blameValid = false;
        edit.applied = true;
        return true;
    }

    std::vector<std::string> fileLines = readFileLinesForRename(edit.path);
    bool ok = applyRenameEditToLines(fileLines, edit);
    if(!ok)
        return false;
    if(!writeFileLinesForRename(edit.path, fileLines))
        return false;
    edit.applied = true;
    return true;
}

static int applyRenameFile(Editor& editor, Editor::RenameFileEdits& file)
{
    int applied = 0;
    for(auto& edit : file.edits)
    {
        if(!edit.applied && applyRenameEdit(editor, edit))
            ++applied;
    }
    return applied;
}

static int applyRenameAll(Editor& editor)
{
    int applied = 0;
    for(auto& file : editor.renameFiles)
        applied += applyRenameFile(editor, file);
    return applied;
}

static void finishRenameIfDone(Editor& editor)
{
    if(renameTotalRemainingEditCount(editor.renameFiles) > 0)
    {
        renameSelectNextPending(editor);
        return;
    }
    int files = (int)editor.renameFiles.size();
    editor.closeRenamePopup();
    editor.setStatusMessage("rn: rename applied in " + std::to_string(files) +
                            " file(s)");
}

static bool prepareRenameEdits(Editor& editor)
{
    if(editor.renameInput.empty())
    {
        editor.renameStatus = "New name is empty";
        return false;
    }
    if(editor.renameInput == editor.renameOriginal)
    {
        editor.renameStatus = "Name unchanged";
        return false;
    }

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(!editor.currentBuffer || !editor.lines || !editor.lspClient)
        return false;

    std::string text = joinLinesForLsp(*editor.lines);
    editor.lspClient->didChange(editor.currentBuffer->filename, text, "cpp");
    editor.currentBuffer->lspSyncNeeded = false;

    std::string lineText;
    if(*editor.cursorY >= 0 && *editor.cursorY < (int)editor.lines->size())
        lineText = (*editor.lines)[*editor.cursorY];

    auto edits = editor.lspClient->renameSymbol(
        editor.currentBuffer->filename, *editor.cursorY, *editor.cursorX,
        lineText, editor.renameInput);

    editor.renameFiles.clear();
    for(auto& file : edits)
    {
        Editor::RenameFileEdits grouped;
        grouped.path = file.path;
        for(auto& edit : file.edits)
        {
            Editor::RenameEdit local;
            local.path = file.path;
            local.startLine = edit.startLine;
            local.startCharacter = edit.startCharacter;
            local.endLine = edit.endLine;
            local.endCharacter = edit.endCharacter;
            local.newText = edit.newText;
            grouped.edits.push_back(std::move(local));
        }
        std::sort(grouped.edits.begin(), grouped.edits.end(),
                  [](const Editor::RenameEdit& a, const Editor::RenameEdit& b)
                  {
                      if(a.startLine != b.startLine)
                          return a.startLine > b.startLine;
                      return a.startCharacter > b.startCharacter;
                  });
        if(!grouped.edits.empty())
            editor.renameFiles.push_back(std::move(grouped));
    }

    if(editor.renameFiles.empty())
    {
        editor.renameStatus = "No rename edits returned";
        return false;
    }
    editor.renamePopupReady = true;
    editor.renameStatus = "p patch, y file, a all, n cancel";
    renameSelectNextPending(editor);
    return true;
#else
    editor.renameStatus = "clangd LSP not built";
    return false;
#endif
}

bool Editor::handleRenamePopupKey(int key)
{
    if(!renamePopupActive)
        return false;

    int c = keyCode(key);
    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(control::ControlKey::CTRL_C))
    {
        closeRenamePopup();
        setStatusMessage("rn: cancelled");
        return true;
    }

    if(!renamePopupReady)
    {
        if(c == keyCode(control::ControlKey::ENTER))
        {
            renameStatus = "Requesting clangd rename...";
            needsFullRedraw = true;
            refreshScreen();
            prepareRenameEdits(*this);
            needsFullRedraw = true;
            return true;
        }
        if(c == keyCode(control::ControlKey::BACKSPACE) ||
           c == keyCode(control::ControlKey::DEL) ||
           c == keyCode(control::ControlKey::CTRL_H) || c == 127)
        {
            if(!renameInput.empty())
                renameInput.pop_back();
            needsFullRedraw = true;
            return true;
        }
        if(c >= 32 && c < 127)
        {
            renameInput.push_back(static_cast<char>(c));
            needsFullRedraw = true;
            return true;
        }
        return true;
    }

    if(c == keyCode(typed::TypedKey::KEY_N))
    {
        closeRenamePopup();
        setStatusMessage("rn: cancelled");
        return true;
    }
    if(c == keyCode(typed::TypedKey::KEY_A))
    {
        captureRenameUndoSnapshot(*this);
        int applied = applyRenameAll(*this);
        if(currentBuffer)
        {
            if(dirty)
                *dirty = currentBuffer->dirty;
            currentBuffer->lspSyncNeeded = true;
        }
        if(applied <= 0)
            clearRenameUndoSnapshot();
        adjustViewport();
        closeRenamePopup();
        setStatusMessage("rn: applied " + std::to_string(applied) + " edit(s)");
        return true;
    }
    if(c == keyCode(typed::TypedKey::KEY_Y))
    {
        if(renameCurrentFile >= 0 &&
           renameCurrentFile < (int)renameFiles.size())
        {
            captureRenameUndoSnapshot(*this);
            int applied =
                applyRenameFile(*this, renameFiles[renameCurrentFile]);
            if(currentBuffer && dirty)
                *dirty = currentBuffer->dirty;
            if(applied <= 0)
                clearRenameUndoSnapshot();
            renameStatus =
                "Applied " + std::to_string(applied) + " edit(s) in file";
            finishRenameIfDone(*this);
            needsFullRedraw = true;
        }
        return true;
    }
    if(c == keyCode(typed::TypedKey::KEY_P))
    {
        if(renameCurrentFile >= 0 &&
           renameCurrentFile < (int)renameFiles.size())
        {
            auto& file = renameFiles[renameCurrentFile];
            if(renameCurrentEdit >= 0 &&
               renameCurrentEdit < (int)file.edits.size())
            {
                captureRenameUndoSnapshot(*this);
                bool ok = applyRenameEdit(*this, file.edits[renameCurrentEdit]);
                if(currentBuffer && dirty)
                    *dirty = currentBuffer->dirty;
                if(!ok)
                    clearRenameUndoSnapshot();
                renameStatus = ok ? "Applied patch" : "Patch failed";
                finishRenameIfDone(*this);
                needsFullRedraw = true;
            }
        }
        return true;
    }

    return true;
}

void Editor::drawRenamePopup(std::string& output) const
{
    if(!renamePopupActive || !currentBuffer)
        return;

    std::vector<std::string> rows;
    rows.push_back("rename: " + renameOriginal);
    rows.push_back("new: " + renameInput + "_");
    if(renamePopupReady)
    {
        int remaining = renameTotalRemainingEditCount(renameFiles);
        rows.push_back("remaining: " + std::to_string(remaining) + " edit(s)");
        if(renameCurrentFile >= 0 &&
           renameCurrentFile < (int)renameFiles.size())
        {
            const auto& file = renameFiles[renameCurrentFile];
            rows.push_back("file: " + renameDisplayPath(*this, file.path));
            rows.push_back("file edits: " +
                           std::to_string(renameRemainingEditCount(file)));
        }
        rows.push_back("[p] patch  [y] file  [a] all  [n/esc] cancel");
    }
    if(!renameStatus.empty())
        rows.push_back(renameStatus);

    int innerW = 0;
    for(const auto& row : rows)
        innerW = std::max(innerW, text_utils::displayWidth(row));
    int maxInner = std::max(20, screenCols - 6);
    for(auto& row : rows)
    {
        while(text_utils::displayWidth(row) > maxInner && row.size() > 3)
            row.pop_back();
        if(text_utils::displayWidth(row) > maxInner)
            row = row.substr(0, std::min<size_t>(row.size(), maxInner));
    }
    innerW = 0;
    for(const auto& row : rows)
        innerW = std::max(innerW, text_utils::displayWidth(row));

    int totalW = std::min(screenCols, innerW + 4);
    innerW = std::max(1, totalW - 4);
    int totalH = (int)rows.size() + 2;

    int cy = (*cursorY - *offsetY) + 1 + tabBarRows();
    int cx = (*cursorX - *offsetX) + 1 + gutterWidth();
    cy = std::clamp(cy, 1, std::max(1, screenRows));
    cx = std::clamp(cx, 1, std::max(1, screenCols));

    int top = cy + 1;
    if(top + totalH - 1 > screenRows)
        top = cy - totalH;
    if(top < 1)
        top = 1;
    int left = cx;
    if(left + totalW - 1 > screenCols)
        left = std::max(1, screenCols - totalW + 1);

    auto moveTo = [&](int r, int c) { output += Terminal::cursorPos(r, c); };
    moveTo(top, left);
    text_utils::appendU8(output, ascii::BOX_LIGHT_TOP_LEFT);
    text_utils::appendUtf8Repeat(output, ascii::BOX_LIGHT_HORIZONTAL,
                                 innerW + 2);
    text_utils::appendU8(output, ascii::BOX_LIGHT_TOP_RIGHT);

    for(size_t i = 0; i < rows.size(); ++i)
    {
        moveTo(top + 1 + (int)i, left);
        text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
        output += " ";
        output += i == 0 ? theme.uiAccent() : theme.panel();
        output += rows[i];
        output += theme.reset();
        int pad = innerW - text_utils::displayWidth(rows[i]);
        if(pad > 0)
            output.append(pad, ' ');
        output += " ";
        text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
    }

    moveTo(top + 1 + (int)rows.size(), left);
    text_utils::appendU8(output, ascii::BOX_LIGHT_BOTTOM_LEFT);
    text_utils::appendUtf8Repeat(output, ascii::BOX_LIGHT_HORIZONTAL,
                                 innerW + 2);
    text_utils::appendU8(output, ascii::BOX_LIGHT_BOTTOM_RIGHT);
}

void Editor::applyDiagnosticFix(int index)
{
    if(!currentBuffer)
        return;
    if(index < 0 || index >= (int)diagnosticPopupFixes.size())
        return;

    std::vector<DiagnosticFixEdit> edits = diagnosticPopupFixes[index].edits;
    if(edits.empty() && !diagnosticPopupFixes[index].command.empty())
    {
#ifdef UVIM_ENABLE_CLANGD_LSP
        if(isClangdLspEnabled() && lspClient)
        {
            std::vector<LspClient::TextEdit> lspEdits =
                lspClient->executeCommand(
                    diagnosticPopupFixes[index].command,
                    diagnosticPopupFixes[index].commandArgsJson,
                    currentBuffer->filename);
            for(const auto& edit : lspEdits)
            {
                DiagnosticFixEdit local;
                local.startLine = edit.startLine;
                local.startCharacter = edit.startCharacter;
                local.endLine = edit.endLine;
                local.endCharacter = edit.endCharacter;
                local.newText = edit.newText;
                edits.push_back(std::move(local));
            }
        }
#endif
    }
    if(edits.empty())
        return;
    std::sort(edits.begin(), edits.end(),
              [](const DiagnosticFixEdit& a, const DiagnosticFixEdit& b)
              {
                  if(a.startLine != b.startLine)
                      return a.startLine > b.startLine;
                  return a.startCharacter > b.startCharacter;
              });

    for(const auto& edit : edits)
    {
        if(edit.startLine < 0 || edit.startLine >= (int)lines->size())
            continue;
        if(edit.endLine < 0 || edit.endLine >= (int)lines->size())
            continue;

        std::string& startLine = (*lines)[edit.startLine];
        std::string& endLine = (*lines)[edit.endLine];
        int startByte = utf16ToUtf8ByteOffset(startLine, edit.startCharacter);
        int endByte = utf16ToUtf8ByteOffset(endLine, edit.endCharacter);

        if(edit.startLine == edit.endLine)
        {
            startLine = startLine.substr(0, startByte) + edit.newText +
                        endLine.substr(endByte);
            continue;
        }

        std::string prefix = startLine.substr(0, startByte);
        std::string suffix = endLine.substr(endByte);
        std::string combined = prefix + edit.newText + suffix;

        std::vector<std::string> newLines;
        size_t pos = 0;
        while(pos <= combined.size())
        {
            size_t next = combined.find('\n', pos);
            if(text_utils::is_not_found(next))
            {
                newLines.push_back(combined.substr(pos));
                break;
            }
            newLines.push_back(combined.substr(pos, next - pos));
            pos = next + 1;
        }

        lines->erase(lines->begin() + edit.startLine,
                     lines->begin() + edit.endLine + 1);
        lines->insert(lines->begin() + edit.startLine, newLines.begin(),
                      newLines.end());
    }

    *dirty = true;
    saveState();
    currentBuffer->lspSyncNeeded = true;
    adjustViewport();
    closeDiagnosticPopup();
#ifdef UVIM_ENABLE_CLANGD_LSP
    syncClangdDiagnosticsIfNeeded(true);
#endif
}
