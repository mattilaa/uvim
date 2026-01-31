#include "editor.h"
#include "constants.h"
#include "enablelog.h"
#include "formatter.h"
#include "git_handler.h"
#include "mode_state_machine.h"
#include "stdlib_goto.h"
#include "syntax_highlighter.h"
#include "terminal.h"
#include "text_utils.h"
#include "widgets/command_history_popup.h"
#include "widgets/command_popup.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif
#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <limits.h>
#include <memory>
#include <pwd.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

// Local helpers used by gd fallbacks.
static std::string_view robot_first_cell(std::string_view line);
static bool robot_keyword_section(std::string_view line);
static bool robot_section_header(std::string_view line);
static bool robot_is_keyword_def(std::string_view line);
static bool python_def_line(std::string_view line, std::string_view symbol);
static bool find_robot_keyword_in_file(const std::string& path,
                                       std::string_view keyword, int& outY,
                                       int& outX);
static bool find_python_def_in_file(const std::string& path,
                                    std::string_view symbol, int& outY,
                                    int& outX);
static bool is_skip_dir(const std::filesystem::path& path);

namespace
{
static TokenType parse_token_type(std::string_view value, TokenType fallback)
{
    std::string v = text_utils::ascii_lower(value);
    if(v == "normal")
        return TOKEN_NORMAL;
    if(v == "keyword")
        return TOKEN_KEYWORD;
    if(v == "type")
        return TOKEN_TYPE;
    if(v == "string")
        return TOKEN_STRING;
    if(v == "char")
        return TOKEN_CHAR;
    if(v == "comment")
        return TOKEN_COMMENT;
    if(v == "preprocessor")
        return TOKEN_PREPROCESSOR;
    if(v == "number")
        return TOKEN_NUMBER;
    if(v == "operator")
        return TOKEN_OPERATOR;
    if(v == "function")
        return TOKEN_FUNCTION;
    return fallback;
}

static std::unordered_map<std::string, std::string>
parseYamlMap(const std::string& input)
{
    std::unordered_map<std::string, std::string> out;
    std::vector<std::pair<int, std::string>> stack;

    std::istringstream stream(input);
    std::string line;
    while(std::getline(stream, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();

        size_t first = 0;
        while(first < line.size() &&
              (line[first] == ' ' || line[first] == '\t'))
        {
            ++first;
        }

        if(first >= line.size())
            continue;
        if(line[first] == '#')
            continue;

        int indent = 0;
        for(size_t i = 0; i < first; ++i)
        {
            indent += (line[i] == '\t') ? 4 : 1;
        }

        std::string rest = line.substr(first);
        size_t colon = rest.find(':');
        if(colon == std::string::npos)
            continue;

        std::string key = rest.substr(0, colon);
        std::string value = rest.substr(colon + 1);
        auto trim = [](std::string& s)
        {
            size_t start = 0;
            while(start < s.size() && std::isspace((unsigned char)s[start]))
                ++start;
            size_t end = s.size();
            while(end > start && std::isspace((unsigned char)s[end - 1]))
                --end;
            s = s.substr(start, end - start);
        };
        trim(key);
        trim(value);

        if(value.empty())
        {
            while(!stack.empty() && indent <= stack.back().first)
                stack.pop_back();
            stack.emplace_back(indent, key);
            continue;
        }

        if(!value.empty() && value[0] != '#')
        {
            for(size_t i = 0; i < value.size(); ++i)
            {
                if(value[i] == '#' &&
                   (i == 0 || std::isspace((unsigned char)value[i - 1])))
                {
                    value = value.substr(0, i);
                    trim(value);
                    break;
                }
            }
        }

        if(value.size() >= 2)
        {
            char q = value.front();
            if((q == '"' || q == '\'') && value.back() == q)
            {
                value = value.substr(1, value.size() - 2);
            }
        }

        while(!stack.empty() && indent <= stack.back().first)
            stack.pop_back();

        std::string full;
        for(const auto& part : stack)
        {
            if(!full.empty())
                full += '.';
            full += part.second;
        }
        if(!full.empty())
            full += '.';
        full += key;

        out[full] = value;
    }

    return out;
}

static std::string_view trim_view(std::string_view value)
{
    while(!value.empty() && text_utils::is_space(value.front()))
        value.remove_prefix(1);
    while(!value.empty() && text_utils::is_space(value.back()))
        value.remove_suffix(1);
    return value;
}

static bool parse_int(std::string_view value, int& out)
{
    value = trim_view(value);
    if(value.empty())
        return false;
    int result = 0;
    auto* begin = value.data();
    auto* end = value.data() + value.size();
    auto [ptr, ec] = std::from_chars(begin, end, result);
    if(ec != std::errc() || ptr != end)
        return false;
    out = result;
    return true;
}

static std::string ascii_lower(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for(char c : value)
        out.push_back(text_utils::ascii_tolower(c));
    return out;
}

static bool find_mlang_builtin_type(std::string_view symbol, std::string& path,
                                    int& line)
{
    std::vector<std::filesystem::path> roots;
    if(const char* env = std::getenv("MLANG_STDLIB_PATH"))
        roots.emplace_back(env);
    if(const char* xdg = std::getenv("XDG_DATA_HOME"))
        roots.emplace_back(std::string(xdg) + "/mlang/stdlib");
    if(const char* home = std::getenv("HOME"))
        roots.emplace_back(std::string(home) + "/.local/share/mlang/stdlib");
    roots.emplace_back("/usr/local/share/mlang/stdlib");
    roots.emplace_back("/usr/share/mlang/stdlib");

    std::string needle = std::string(symbol);
    std::string needleLower = ascii_lower(symbol);

    for(const auto& root : roots)
    {
        if(root.empty())
            continue;
        std::filesystem::path p = root / "types.mla";
        std::error_code ec;
        if(!std::filesystem::exists(p, ec))
            continue;
        std::ifstream in(p);
        if(!in)
            continue;
        std::string lineStr;
        int lineNo = 0;
        while(std::getline(in, lineStr))
        {
            ++lineNo;
            const std::string marker = "// @builtin ";
            if(lineStr.rfind(marker, 0) != 0)
                continue;
            std::string name = lineStr.substr(marker.size());
            if(name.empty())
                continue;
            if(name == needle || ascii_lower(name) == needleLower)
            {
                path = p.string();
                line = lineNo - 1;
                return true;
            }
        }
    }
    return false;
}

static bool find_mlang_builtin_macro(std::string_view symbol, std::string& path,
                                     int& line)
{
    std::vector<std::filesystem::path> roots;
    if(const char* env = std::getenv("MLANG_STDLIB_PATH"))
        roots.emplace_back(env);
    if(const char* xdg = std::getenv("XDG_DATA_HOME"))
        roots.emplace_back(std::string(xdg) + "/mlang/stdlib");
    if(const char* home = std::getenv("HOME"))
        roots.emplace_back(std::string(home) + "/.local/share/mlang/stdlib");
    roots.emplace_back("/usr/local/share/mlang/stdlib");
    roots.emplace_back("/usr/share/mlang/stdlib");

    std::string needle = std::string(symbol);
    std::string needleLower = ascii_lower(symbol);

    for(const auto& root : roots)
    {
        if(root.empty())
            continue;
        std::filesystem::path p = root / "macros.mla";
        std::error_code ec;
        if(!std::filesystem::exists(p, ec))
            continue;
        std::ifstream in(p);
        if(!in)
            continue;
        std::string lineStr;
        int lineNo = 0;
        while(std::getline(in, lineStr))
        {
            ++lineNo;
            const std::string marker = "// @builtin_macro ";
            if(lineStr.rfind(marker, 0) != 0)
                continue;
            std::string name = lineStr.substr(marker.size());
            if(name.empty())
                continue;
            if(name == needle || ascii_lower(name) == needleLower)
            {
                path = p.string();
                line = lineNo - 1;
                return true;
            }
        }
    }
    return false;
}

static std::vector<std::string> split_csv(std::string_view input)
{
    std::vector<std::string> out;
    size_t start = 0;
    while(start <= input.size())
    {
        size_t comma = input.find(',', start);
        size_t end = (comma == std::string_view::npos) ? input.size() : comma;
        std::string_view part = trim_view(input.substr(start, end - start));
        if(!part.empty())
            out.emplace_back(part);
        if(comma == std::string_view::npos)
            break;
        start = comma + 1;
    }
    return out;
}

static std::unordered_set<std::string> default_robot_keywords()
{
    static constexpr std::string_view kKeywords[] = {
        "if",       "else",     "end",       "for",           "while",
        "try",      "except",   "finally",   "return",        "break",
        "continue", "skip",     "fail",      "run",           "keyword",
        "library",  "resource", "variables", "documentation", "tags",
        "metadata", "setup",    "teardown",  "suite",         "test",
        "task",     "template", "timeout",   "default",       "force",
    };
    std::unordered_set<std::string> out;
    out.reserve(std::size(kKeywords));
    for(auto kw : kKeywords)
        out.insert(ascii_lower(kw));
    return out;
}

static std::unordered_set<std::string> default_robot_custom_keywords()
{
    return {};
}

static std::unordered_set<std::string> default_robot_settings()
{
    static constexpr std::string_view kSettings[] = {
        "resource",      "library",      "variables",      "documentation",
        "metadata",      "suite setup",  "suite teardown", "test setup",
        "test teardown", "task setup",   "task teardown",  "test template",
        "task template", "test timeout", "task timeout",   "force tags",
        "default tags",
    };
    std::unordered_set<std::string> out;
    out.reserve(std::size(kSettings));
    for(auto setting : kSettings)
        out.insert(ascii_lower(setting));
    return out;
}
} // namespace

#if defined(UVIM_TERMINAL_POSIX)
static volatile sig_atomic_t g_pending_resize = 0;

static void handle_sigwinch(int)
{
    g_pending_resize = 1;
}
#endif

static bool isIdent(char c)
{
    return std::isalnum((unsigned char)c) || c == '_';
}

static std::string_view robot_first_cell(std::string_view line)
{
    line = trim_view(line);
    if(line.empty())
        return {};

    size_t i = 0;
    int spaceRun = 0;
    for(; i < line.size(); ++i)
    {
        if(line[i] == '\t')
            break;
        if(line[i] == ' ')
        {
            spaceRun++;
            if(spaceRun >= 2)
                break;
        }
        else
        {
            spaceRun = 0;
        }
    }

    size_t end = i;
    if(end > 0 && line[end - 1] == ' ')
        --end;
    return line.substr(0, end);
}

static bool robot_keyword_section(std::string_view line)
{
    line = trim_view(line);
    if(!(line.starts_with("***") && line.ends_with("***")))
        return false;

    line.remove_prefix(3);
    line.remove_suffix(3);
    line = trim_view(line);
    return text_utils::iequals_ascii(line, "Keywords");
}

static bool robot_section_header(std::string_view line)
{
    line = trim_view(line);
    return line.starts_with("***") && line.ends_with("***");
}

static bool robot_is_keyword_def(std::string_view line)
{
    if(line.empty())
        return false;
    if(text_utils::is_space(line.front()))
        return false;
    line = trim_view(line);
    if(line.empty())
        return false;
    if(line.starts_with("***"))
        return false;
    if(line.starts_with("["))
        return false;
    if(line.starts_with("#"))
        return false;
    return true;
}

static bool python_def_line(std::string_view line, std::string_view symbol)
{
    line = trim_view(line);
    if(line.starts_with("def "))
        line.remove_prefix(4);
    else if(line.starts_with("class "))
        line.remove_prefix(6);
    else
        return false;

    if(line.empty())
        return false;
    size_t i = 0;
    while(i < line.size() && isIdent(line[i]))
        ++i;
    if(i == 0)
        return false;
    std::string_view name = line.substr(0, i);
    return text_utils::iequals_ascii(name, symbol);
}

// Check if a line is likely a variable/parameter declaration for the symbol
// Returns the column position of the symbol if found, -1 otherwise
static int findLocalDeclaration(const std::string& line,
                                const std::string& symbol)
{
    // Skip pure comment lines
    size_t firstNonSpace = line.find_first_not_of(" \t");
    if(firstNonSpace != std::string::npos &&
       line.substr(firstNonSpace, 2) == "//")
        return -1;

    // Get effective line (before any comment)
    size_t commentPos = line.find("//");
    std::string effectiveLine =
        (commentPos != std::string::npos) ? line.substr(0, commentPos) : line;

    // Find the symbol in the line
    size_t pos = 0;
    while((pos = effectiveLine.find(symbol, pos)) != std::string::npos)
    {
        // Make sure it's a whole word match
        bool validStart = (pos == 0 || !isIdent(effectiveLine[pos - 1]));
        bool validEnd = (pos + symbol.length() >= effectiveLine.length() ||
                         !isIdent(effectiveLine[pos + symbol.length()]));

        if(!validStart || !validEnd)
        {
            pos++;
            continue;
        }

        // Check what comes after the symbol
        size_t afterSymbol = pos + symbol.length();
        while(afterSymbol < effectiveLine.length() &&
              std::isspace((unsigned char)effectiveLine[afterSymbol]))
            afterSymbol++;

        // Check what comes before the symbol (skipping spaces and qualifiers)
        int beforeSymbol = pos - 1;
        while(beforeSymbol >= 0 &&
              std::isspace((unsigned char)effectiveLine[beforeSymbol]))
            beforeSymbol--;

        // Common patterns for variable declarations:
        // type name;
        // type name =
        // type name,
        // type name)  - for function parameters
        // type& name
        // type* name
        // const type name
        // auto name

        if(afterSymbol < effectiveLine.length())
        {
            char nextChar = effectiveLine[afterSymbol];
            // If followed by =, ;, ,, ), [ then it's likely a declaration
            // NOT if followed by ( which would be a function call
            if(nextChar == '=' || nextChar == ';' || nextChar == ',' ||
               nextChar == ')' || nextChar == '[')
            {
                // Check that there's something before (a type)
                if(beforeSymbol >= 0)
                {
                    char prevChar = effectiveLine[beforeSymbol];
                    // Common chars before a variable name in declaration:
                    // identifier char (end of type name), >, *, &, ]
                    if(isIdent(prevChar) || prevChar == '>' ||
                       prevChar == '*' || prevChar == '&' || prevChar == ']')
                    {
                        return (int)pos;
                    }
                }
            }
        }
        // End of line after symbol (like in "int x")
        else if(beforeSymbol >= 0)
        {
            char prevChar = effectiveLine[beforeSymbol];
            if(isIdent(prevChar) || prevChar == '>' || prevChar == '*' ||
               prevChar == '&' || prevChar == ']')
            {
                return (int)pos;
            }
        }

        pos++;
    }

    return -1;
}

// Search backwards from current position for local variable declaration
static bool searchLocalDefinition(const std::vector<std::string>& lines,
                                  const std::string& symbol, int startY,
                                  int startX, int& outY, int& outX)
{
    // Track brace depth to stay within current scope
    int braceDepth = 0;
    bool foundOpenBrace = false;

    // Start from the line before cursor (or current line if cursor is past the
    // symbol)
    for(int y = startY; y >= 0; y--)
    {
        const std::string& line = lines[y];

        // Count braces in this line (from end to start for backwards search)
        for(int i = (int)line.length() - 1; i >= 0; i--)
        {
            // Skip if we're on the starting line and past start position
            if(y == startY && i >= startX)
                continue;

            char c = line[i];
            if(c == '}')
            {
                braceDepth++;
            }
            else if(c == '{')
            {
                if(braceDepth > 0)
                    braceDepth--;
                else
                    foundOpenBrace = true; // Found enclosing scope start
            }
        }

        // Don't search past the opening brace of current scope
        // (but do search the line with the opening brace for parameters)

        // Check if this line has a declaration of our symbol
        // Only search if we're at same or lower brace depth (within scope)
        if(braceDepth == 0)
        {
            int col = findLocalDeclaration(line, symbol);
            if(col >= 0)
            {
                // Make sure it's before our cursor position if on same line
                if(y < startY || col < startX)
                {
                    outY = y;
                    outX = col;
                    return true;
                }
            }
        }

        // If we've exited our function scope, stop searching
        if(foundOpenBrace && braceDepth == 0)
        {
            // Check this line one more time (function parameters are on/before
            // opening brace)
            int col = findLocalDeclaration(line, symbol);
            if(col >= 0)
            {
                outY = y;
                outX = col;
                return true;
            }

            // Also check the line above for multi-line function signatures
            if(y > 0)
            {
                col = findLocalDeclaration(lines[y - 1], symbol);
                if(col >= 0)
                {
                    outY = y - 1;
                    outX = col;
                    return true;
                }
            }
            break;
        }
    }

    return false;
}

// Search for member variable declaration in class/struct
static bool searchMemberDefinition(const std::vector<std::string>& lines,
                                   const std::string& symbol, int& outY,
                                   int& outX)
{
    // Look for struct/class definitions and their members
    bool inClassOrStruct = false;
    int classStartLine = -1;
    int braceDepth = 0;

    for(int y = 0; y < (int)lines.size(); y++)
    {
        const std::string& line = lines[y];

        // Check for class/struct keyword
        if(line.find("class ") != std::string::npos ||
           line.find("struct ") != std::string::npos)
        {
            inClassOrStruct = true;
            classStartLine = y;
            braceDepth = 0;
        }

        // Track braces
        for(char c : line)
        {
            if(c == '{')
                braceDepth++;
            else if(c == '}')
            {
                braceDepth--;
                if(braceDepth == 0 && inClassOrStruct)
                {
                    inClassOrStruct = false;
                }
            }
        }

        // If we're inside a class/struct at depth 1, look for member
        // declarations
        if(inClassOrStruct && braceDepth == 1)
        {
            int col = findLocalDeclaration(line, symbol);
            if(col >= 0)
            {
                outY = y;
                outX = col;
                return true;
            }
        }
    }

    return false;
}

static bool isHeaderFile(const std::string& path)
{
    return path == ".h" || path == ".hpp";
}

static bool isSourceFile(const std::string& path)
{
    return path == ".c" || path == ".cpp" || path == ".cc";
}

Editor::Editor(bool skipInitialBuffer, const std::string& configPath)
{
    Terminal::enableRawMode();
    Terminal::getWindowSize(screenRows, screenCols);
    screenRows -= 2; // Status bar and message bar
    theme = Theme::defaults();
    this->configPath = configPath;
    theme.loadFromFile(configPath);
    robotKeywordSet = default_robot_keywords();
    robotCustomKeywordSet = default_robot_custom_keywords();
    robotSettingSet = default_robot_settings();
    mlangTokenCache = std::make_shared<MlangTokenCache>();
    if(!configPath.empty())
    {
        std::ifstream in(configPath);
        if(in.is_open())
        {
            std::ostringstream buf;
            buf << in.rdbuf();
            auto values = parseYamlMap(buf.str());
            auto it = values.find("editor.tabspaces");
            if(it == values.end())
                it = values.find("settings.tabspaces");
            if(it == values.end())
                it = values.find("tabspaces");
            if(it != values.end())
            {
                try
                {
                    int v = std::stoi(it->second);
                    if(v >= 1 && v <= 16)
                        tabSpaces = v;
                }
                catch(...)
                {
                }
            }
            auto itb = values.find("editor.autobraces");
            if(itb == values.end())
                itb = values.find("settings.autobraces");
            if(itb == values.end())
                itb = values.find("autobraces");
            if(itb != values.end())
            {
                std::string v = itb->second;
                if(v == "true" || v == "1" || v == "on")
                    autoBraces = true;
                else if(v == "false" || v == "0" || v == "off")
                    autoBraces = false;
            }
            auto itq = values.find("editor.autoquotes");
            if(itq == values.end())
                itq = values.find("settings.autoquotes");
            if(itq == values.end())
                itq = values.find("autoquotes");
            if(itq != values.end())
            {
                std::string v = itq->second;
                if(v == "true" || v == "1" || v == "on")
                    autoQuotes = true;
                else if(v == "false" || v == "0" || v == "off")
                    autoQuotes = false;
            }
            auto itbs = values.find("editor.autobracesinstrings");
            if(itbs == values.end())
                itbs = values.find("settings.autobracesinstrings");
            if(itbs == values.end())
                itbs = values.find("autobracesinstrings");
            if(itbs != values.end())
            {
                std::string v = itbs->second;
                if(v == "true" || v == "1" || v == "on")
                    autoBracesInStrings = true;
                else if(v == "false" || v == "0" || v == "off")
                    autoBracesInStrings = false;
            }
            auto ittg = values.find("editor.autotags");
            if(ittg == values.end())
                ittg = values.find("settings.autotags");
            if(ittg == values.end())
                ittg = values.find("autotags");
            if(ittg != values.end())
            {
                std::string v = ittg->second;
                if(v == "true" || v == "1" || v == "on")
                    autoTags = true;
                else if(v == "false" || v == "0" || v == "off")
                    autoTags = false;
            }
            auto itc = values.find("editor.autocomplete");
            if(itc == values.end())
                itc = values.find("settings.autocomplete");
            if(itc == values.end())
                itc = values.find("autocomplete");
            if(itc != values.end())
            {
                std::string v = itc->second;
                if(v == "true" || v == "1" || v == "on")
                    autoCompletion = true;
                else if(v == "false" || v == "0" || v == "off")
                    autoCompletion = false;
            }
            auto itcap = values.find("editor.completionautoparens");
            if(itcap == values.end())
                itcap = values.find("settings.completionautoparens");
            if(itcap == values.end())
                itcap = values.find("completionautoparens");
            if(itcap != values.end())
            {
                std::string v = itcap->second;
                if(v == "true" || v == "1" || v == "on")
                    completionAutoParens = true;
                else if(v == "false" || v == "0" || v == "off")
                    completionAutoParens = false;
            }
            auto itsc = values.find("editor.usesystemclipboard");
            if(itsc == values.end())
                itsc = values.find("settings.usesystemclipboard");
            if(itsc == values.end())
                itsc = values.find("usesystemclipboard");
            if(itsc != values.end())
            {
                std::string v = itsc->second;
                if(v == "true" || v == "1" || v == "on")
                    useSystemClipboard = true;
                else if(v == "false" || v == "0" || v == "off")
                    useSystemClipboard = false;
            }
            auto itutf = values.find("editor.utf8");
            if(itutf == values.end())
                itutf = values.find("settings.utf8");
            if(itutf == values.end())
                itutf = values.find("utf8");
            if(itutf != values.end())
            {
                std::string v = itutf->second;
                if(v == "true" || v == "1" || v == "on")
                    utf8Mode = true;
                else if(v == "false" || v == "0" || v == "off")
                    utf8Mode = false;
            }
            auto itt = values.find("editor.showtabs");
            if(itt == values.end())
                itt = values.find("settings.showtabs");
            if(itt == values.end())
                itt = values.find("showtabs");
            if(itt != values.end())
            {
                std::string v = itt->second;
                if(v == "true" || v == "1" || v == "on")
                    showTabs = true;
                else if(v == "false" || v == "0" || v == "off")
                    showTabs = false;
            }
            auto itrn = values.find("editor.relativenumber");
            if(itrn == values.end())
                itrn = values.find("settings.relativenumber");
            if(itrn == values.end())
                itrn = values.find("relativenumber");
            if(itrn != values.end())
            {
                std::string v = itrn->second;
                if(v == "true" || v == "1" || v == "on")
                    showRelativeLineNumbers = true;
                else if(v == "false" || v == "0" || v == "off")
                    showRelativeLineNumbers = false;
            }
            auto itgc = values.find("editor.gitdefaultcolors");
            if(itgc == values.end())
                itgc = values.find("settings.gitdefaultcolors");
            if(itgc == values.end())
                itgc = values.find("gitdefaultcolors");
            if(itgc != values.end())
            {
                std::string v = itgc->second;
                if(v == "true" || v == "1" || v == "on")
                    gitUseDefaultColors = true;
                else if(v == "false" || v == "0" || v == "off")
                    gitUseDefaultColors = false;
            }
            auto itctp = values.find("editor.commenttogglepartial");
            if(itctp == values.end())
                itctp = values.find("settings.commenttogglepartial");
            if(itctp == values.end())
                itctp = values.find("commenttogglepartial");
            if(itctp != values.end())
            {
                std::string v = itctp->second;
                if(v == "true" || v == "1" || v == "on")
                    commentTogglePartial = true;
                else if(v == "false" || v == "0" || v == "off")
                    commentTogglePartial = false;
            }
            auto itfol = values.find("editor.formatoninsertleave");
            if(itfol == values.end())
                itfol = values.find("settings.formatoninsertleave");
            if(itfol == values.end())
                itfol = values.find("formatoninsertleave");
            if(itfol != values.end())
            {
                std::string v = itfol->second;
                if(v == "true" || v == "1" || v == "on")
                    formatOnInsertLeave = true;
                else if(v == "false" || v == "0" || v == "off")
                    formatOnInsertLeave = false;
            }
            auto itfs = values.find("editor.formatonsave");
            if(itfs == values.end())
                itfs = values.find("settings.formatonsave");
            if(itfs == values.end())
                itfs = values.find("formatonsave");
            if(itfs != values.end())
            {
                std::string v = itfs->second;
                if(v == "true" || v == "1" || v == "on")
                    formatOnSave = true;
                else if(v == "false" || v == "0" || v == "off")
                    formatOnSave = false;
            }
            auto itfmt = values.find("editor.formatondoubleesctimeoutms");
            if(itfmt == values.end())
                itfmt = values.find("settings.formatondoubleesctimeoutms");
            if(itfmt == values.end())
                itfmt = values.find("formatondoubleesctimeoutms");
            if(itfmt != values.end())
            {
                std::string v = itfmt->second;
                try
                {
                    int ms = std::stoi(v);
                    if(ms > 0 && ms <= 5000)
                        formatOnDoubleEscTimeoutMs = ms;
                }
                catch(...)
                {
                }
            }
            auto itgdc = values.find("editor.gdcenter");
            if(itgdc == values.end())
                itgdc = values.find("settings.gdcenter");
            if(itgdc == values.end())
                itgdc = values.find("gdcenter");
            if(itgdc != values.end())
            {
                std::string v = itgdc->second;
                if(v == "true" || v == "1" || v == "on")
                    gdCenterScreen = true;
                else if(v == "false" || v == "0" || v == "off")
                    gdCenterScreen = false;
            }
            auto itj = values.find("editor.syntax.json");
            if(itj == values.end())
                itj = values.find("syntax.json");
            if(itj != values.end())
            {
                std::string v = itj->second;
                syntaxJson = !(v == "false" || v == "0" || v == "off");
            }
            auto ity = values.find("editor.syntax.yaml");
            if(ity == values.end())
                ity = values.find("syntax.yaml");
            if(ity != values.end())
            {
                std::string v = ity->second;
                syntaxYaml = !(v == "false" || v == "0" || v == "off");
            }
            auto itrk = values.find("editor.syntax.robot.keywords");
            if(itrk == values.end())
                itrk = values.find("syntax.robot.keywords");
            if(itrk != values.end())
            {
                std::string v = itrk->second;
                std::string lower = ascii_lower(v);
                if(lower == "false" || lower == "0" || lower == "off" ||
                   lower == "none")
                {
                    syntaxRobotKeywords = false;
                    robotKeywordSet.clear();
                }
                else
                {
                    auto list = split_csv(v);
                    if(!list.empty())
                    {
                        syntaxRobotKeywords = true;
                        robotKeywordSet.clear();
                        for(const auto& item : list)
                            robotKeywordSet.insert(ascii_lower(item));
                    }
                }
            }
            auto itrkt = values.find("editor.syntax.robot.highlight_titles");
            if(itrkt == values.end())
                itrkt = values.find("syntax.robot.highlight_titles");
            if(itrkt != values.end())
            {
                std::string v = ascii_lower(itrkt->second);
                syntaxRobotHighlightTitles =
                    !(v == "false" || v == "0" || v == "off");
            }
            auto itrkc = values.find("editor.syntax.robot.highlight_calls");
            if(itrkc == values.end())
                itrkc = values.find("syntax.robot.highlight_calls");
            if(itrkc != values.end())
            {
                std::string v = ascii_lower(itrkc->second);
                syntaxRobotHighlightCalls =
                    !(v == "false" || v == "0" || v == "off");
            }
            auto itcm = values.find("editor.syntax.cpp.highlight_members");
            if(itcm == values.end())
                itcm = values.find("syntax.cpp.highlight_members");
            if(itcm != values.end())
            {
                std::string v = ascii_lower(itcm->second);
                syntaxCppHighlightMembers =
                    !(v == "false" || v == "0" || v == "off");
            }
            auto itct = values.find("editor.syntax.cpp.highlight_type_names");
            if(itct == values.end())
                itct = values.find("syntax.cpp.highlight_type_names");
            if(itct != values.end())
            {
                std::string v = ascii_lower(itct->second);
                syntaxCppHighlightTypeNames =
                    !(v == "false" || v == "0" || v == "off");
            }
            auto itci =
                values.find("editor.syntax.cpp.highlight_implicit_members");
            if(itci == values.end())
                itci = values.find("syntax.cpp.highlight_implicit_members");
            if(itci != values.end())
            {
                std::string v = ascii_lower(itci->second);
                syntaxCppHighlightImplicitMembers =
                    !(v == "false" || v == "0" || v == "off");
            }
            auto itcp = values.find("editor.syntax.cpp.highlight_param_types");
            if(itcp == values.end())
                itcp = values.find("syntax.cpp.highlight_param_types");
            if(itcp != values.end())
            {
                std::string v = ascii_lower(itcp->second);
                syntaxCppHighlightParamTypes =
                    !(v == "false" || v == "0" || v == "off");
            }
            auto itcs =
                values.find("editor.syntax.cpp.highlight_system_includes");
            if(itcs == values.end())
                itcs = values.find("syntax.cpp.highlight_system_includes");
            if(itcs != values.end())
            {
                std::string v = ascii_lower(itcs->second);
                syntaxCppHighlightSystemIncludes =
                    !(v == "false" || v == "0" || v == "off");
            }
            auto itrc = values.find("editor.syntax.robot.custom_keywords");
            if(itrc == values.end())
                itrc = values.find("syntax.robot.custom_keywords");
            if(itrc != values.end())
            {
                auto list = split_csv(itrc->second);
                robotCustomKeywordSet.clear();
                for(const auto& item : list)
                    robotCustomKeywordSet.insert(ascii_lower(item));
            }
            auto itrs = values.find("editor.syntax.robot.settings");
            if(itrs == values.end())
                itrs = values.find("syntax.robot.settings");
            if(itrs != values.end())
            {
                auto list = split_csv(itrs->second);
                if(!list.empty())
                {
                    robotSettingSet.clear();
                    for(const auto& item : list)
                        robotSettingSet.insert(ascii_lower(item));
                }
            }
            auto itpf = values.find("editor.python.formatter");
            if(itpf == values.end())
                itpf = values.find("python.formatter");
            if(itpf != values.end())
            {
                std::string v = ascii_lower(itpf->second);
                if(v == "black" || v == "ruff")
                    pythonFormatter = v;
            }

            auto get = [&](std::string_view key) -> std::optional<std::string>
            {
                auto it = values.find(std::string(key));
                if(it != values.end())
                    return it->second;
                return std::nullopt;
            };

            auto set_token = [&](std::string_view key, TokenType& target)
            {
                auto v = get(key);
                if(v)
                    target = parse_token_type(*v, target);
            };

            set_token("editor.syntax.markup.heading_token", markupHeadingToken);
            set_token("syntax.markup.heading_token", markupHeadingToken);
            set_token("editor.syntax.markup.bold_token", markupBoldToken);
            set_token("syntax.markup.bold_token", markupBoldToken);
            set_token("editor.syntax.markup.italic_token", markupItalicToken);
            set_token("syntax.markup.italic_token", markupItalicToken);
            set_token("editor.syntax.markup.code_token", markupCodeToken);
            set_token("syntax.markup.code_token", markupCodeToken);
            set_token("editor.syntax.markup.blockquote_token",
                      markupBlockquoteToken);
            set_token("syntax.markup.blockquote_token", markupBlockquoteToken);
            set_token("editor.syntax.markup.fence_token", markupFenceToken);
            set_token("syntax.markup.fence_token", markupFenceToken);
            set_token("editor.syntax.markup.link_text_token",
                      markupLinkTextToken);
            set_token("syntax.markup.link_text_token", markupLinkTextToken);
            set_token("editor.syntax.markup.link_url_token",
                      markupLinkUrlToken);
            set_token("syntax.markup.link_url_token", markupLinkUrlToken);
            set_token("editor.syntax.markup.link_title_token",
                      markupLinkTitleToken);
            set_token("syntax.markup.link_title_token", markupLinkTitleToken);
            set_token("editor.syntax.markup.rdoc_topic_token",
                      markupRdocTopicToken);
            set_token("syntax.markup.rdoc_topic_token", markupRdocTopicToken);
        }
    }

    // No buffers on start unless files are explicitly opened.
    (void)skipInitialBuffer;

#if defined(UVIM_TERMINAL_POSIX)
    std::signal(SIGWINCH, handle_sigwinch);
#endif

    modeStateMachine =
        std::make_unique<ModeStateMachine>(createModeContext(this));
    syntaxHighlighter = std::make_unique<SyntaxHighlighter>(this);
    formatter = std::make_unique<Formatter>(this);
    gitHandler = std::make_unique<GitHandler>(this);
}

#ifdef UVIM_TESTING
Editor::Editor(TestTag /* tag */, int rows, int cols)
{
    screenRows = std::max(1, rows - 2);
    screenCols = std::max(1, cols);
    theme = Theme::defaults();
    configPath.clear();
    robotKeywordSet = default_robot_keywords();
    robotCustomKeywordSet = default_robot_custom_keywords();
    robotSettingSet = default_robot_settings();
    mlangTokenCache = std::make_shared<MlangTokenCache>();
    syntaxHighlighter = std::make_unique<SyntaxHighlighter>(this);
    formatter = std::make_unique<Formatter>(this);
    gitHandler = std::make_unique<GitHandler>(this);
}

Editor Editor::createForTests(int rows, int cols)
{
    return Editor(TestTag{}, rows, cols);
}
#endif

bool Editor::isFileType(FileType type) const
{
    if(!syntaxHighlighter)
        return false;
    return syntaxHighlighter->isFileType(type);
}

size_t Editor::byteOffsetForPosition(int y, int x) const
{
    if(!formatter)
        return 0;
    return formatter->byteOffsetForPosition(y, x);
}

bool Editor::clangFormatWithArgs(const std::string& extraArgs,
                                 const std::string& successMessage)
{
    if(!formatter)
        return false;
    return formatter->clangFormatWithArgs(extraArgs, successMessage);
}

bool Editor::pythonFormatBuffer()
{
    if(!formatter)
        return false;
    return formatter->pythonFormatBuffer();
}

void Editor::pythonLintBuffer()
{
    if(formatter)
        formatter->pythonLintBuffer();
}

bool Editor::robotFormatBuffer()
{
    if(!formatter)
        return false;
    return formatter->robotFormatBuffer();
}

bool Editor::jsonFormatBuffer()
{
    if(!formatter)
        return false;
    return formatter->jsonFormatBuffer();
}

bool Editor::yamlFormatBuffer()
{
    if(!formatter)
        return false;
    return formatter->yamlFormatBuffer();
}

void Editor::clangFormatVisualSelection()
{
    if(formatter)
        formatter->clangFormatVisualSelection();
}

void Editor::clangFormatVisualBlockSelection()
{
    if(formatter)
        formatter->clangFormatVisualBlockSelection();
}

void Editor::ensureMlangTokensLoaded() const
{
    if(syntaxHighlighter)
        syntaxHighlighter->ensureMlangTokensLoaded();
}

std::optional<TokenType>
Editor::lookupMlangTokenType(std::string_view word) const
{
    if(!syntaxHighlighter)
        return std::nullopt;
    return syntaxHighlighter->lookupMlangTokenType(word);
}

std::string Editor::getColorCode(TokenType type) const
{
    if(!syntaxHighlighter)
        return {};
    return syntaxHighlighter->getColorCode(type);
}

std::vector<Token>
Editor::tokenizeLine(const std::string& line, bool& inBlockComment,
                     bool& inTomlMultiline, char& tomlQuote,
                     bool& inMarkupFence, char& markupFenceChar,
                     bool inCppMethodContext, bool inCppFunctionContext,
                     bool inCppParamListContext) const
{
    if(!syntaxHighlighter)
        return {};
    return syntaxHighlighter->tokenizeLine(
        line, inBlockComment, inTomlMultiline, tomlQuote, inMarkupFence,
        markupFenceChar, inCppMethodContext, inCppFunctionContext,
        inCppParamListContext);
}

void Editor::renderLineWithSyntax(std::string& output, const std::string& line,
                                  int start, int len, int fileRow)
{
    if(syntaxHighlighter)
        syntaxHighlighter->renderLineWithSyntax(output, line, start, len,
                                                fileRow);
}

bool Editor::isRobotKeyword(std::string_view word) const
{
    if(!syntaxRobotKeywords || robotKeywordSet.empty())
        return false;
    return robotKeywordSet.find(ascii_lower(word)) != robotKeywordSet.end();
}

bool Editor::isRobotCustomKeyword(std::string_view word) const
{
    if(robotCustomKeywordSet.empty())
        return false;
    return robotCustomKeywordSet.find(ascii_lower(word)) !=
           robotCustomKeywordSet.end();
}

bool Editor::isRobotSetting(std::string_view cell) const
{
    if(robotSettingSet.empty())
        return false;
    return robotSettingSet.find(ascii_lower(cell)) != robotSettingSet.end();
}

Editor::~Editor()
{
    Terminal::clearScreen();
    Terminal::moveCursor(1, 1);
}

void Editor::enableClangdLsp(bool enable, const std::string& compileCommandsDir,
                             const std::string& clangdPath,
                             const std::string& queryDriverAllowList)
{
    clangdLspEnabled = false;
    clangdLspCompileCommandsDir = compileCommandsDir;
    clangdLspPath = clangdPath;
    clangdLspQueryDriverAllowList = queryDriverAllowList;

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(!enable)
    {
        if(lspClient)
        {
            lspClient->stop();
            lspClient.reset();
        }
        return;
    }

    std::string rootDir = ".";
    if(!projectRoot.empty())
    {
        rootDir = projectRoot;
    }
    else
    {
        char cwd[PATH_MAX];
        if(getcwd(cwd, sizeof(cwd)))
            rootDir = std::string(cwd);
    }

    // Auto-detect compile_commands.json if caller didn't specify --ccdir
    std::string ccdir = clangdLspCompileCommandsDir;
    auto exists = [](const std::string& p)
    {
        struct stat st;
        return stat(p.c_str(), &st) == 0;
    };

    if(ccdir.empty())
    {
        if(exists(rootDir + "/compile_commands.json"))
            ccdir = rootDir;
        else if(exists(rootDir + "/build/compile_commands.json"))
            ccdir = rootDir + "/build";
    }

    // If not provided, use a conservative default query-driver allowlist so
    // clangd can discover system include paths (standard library headers etc)
    // from common compilers referenced in compile_commands.json. Users with
    // custom toolchains can pass:
    //   --query-driver "/opt/toolchain/bin/*g++*,/opt/toolchain/bin/*gcc*"
    std::string qd = clangdLspQueryDriverAllowList;
    if(qd.empty())
    {
        // Only allow executing compilers from typical system locations.
        // clangd expects a comma-separated list of globs/paths.
        qd =
            "/usr/bin/*clang*,/usr/bin/*clang++*,/usr/bin/*gcc*,/usr/bin/*g++*,"
            "/bin/*gcc*,/bin/*g++*,"
            "/usr/local/bin/*clang*,/usr/local/bin/*clang++*,/usr/local/bin/"
            "*gcc*,/usr/local/bin/*g++*,"
            "/opt/homebrew/bin/*clang*,/opt/homebrew/bin/*clang++*,/opt/"
            "homebrew/bin/*gcc*,/opt/homebrew/bin/*g++*";
    }

    lspClient = std::make_unique<LspClient>();
    if(!lspClient->start(clangdLspPath, rootDir, ccdir, qd))
    {
        lspClient.reset();
        setStatusMessage("clangd LSP: failed to start");
        return;
    }

    clangdLspEnabled = true;
    clangdLspCompileCommandsDir = ccdir;
#else
    (void)enable;
    (void)compileCommandsDir;
    (void)clangdPath;
    setStatusMessage("clangd LSP: not compiled in");
#endif
}

bool Editor::isClangdLspEnabled() const
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    return clangdLspEnabled && lspClient && lspClient->running();
#else
    return false;
#endif
}

void Editor::enableRobotLsp(bool enable, const std::string& robotLspPath,
                            const std::vector<std::string>& robotLspArgs)
{
    robotLspEnabled = false;
    this->robotLspPath = robotLspPath;
    this->robotLspArgs = robotLspArgs;

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(!enable)
    {
        if(robotLspClient)
        {
            robotLspClient->stop();
            robotLspClient.reset();
        }
        return;
    }

    std::string rootDir = ".";
    if(!projectRoot.empty())
    {
        rootDir = projectRoot;
    }
    else
    {
        char cwd[PATH_MAX];
        if(getcwd(cwd, sizeof(cwd)))
            rootDir = std::string(cwd);
    }

    std::vector<std::string> args = this->robotLspArgs;
    if(args.empty())
    {
        args.push_back("--stdio");
    }

    robotLspClient = std::make_unique<LspClient>();
    if(!robotLspClient->startServer(this->robotLspPath, rootDir, args))
    {
        LOG_ERROR(LOG, "Robot LSP failed to start, LSP path: {}",
                  this->robotLspPath.c_str());
        robotLspClient.reset();
        return;
    }

    robotLspEnabled = true;
    LOG_DEBUG(LOG, "Robot LSP enabled");
#else
    (void)enable;
    (void)robotLspPath;
    (void)robotLspArgs;
    LOG_ERROR(LOG, "Robot LSP is not compiled in");
#endif
}

bool Editor::isRobotLspEnabled() const
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    return robotLspEnabled && robotLspClient && robotLspClient->running();
#else
    return false;
#endif
}

void Editor::enablePythonLsp(bool enable, const std::string& pythonLspPath,
                             const std::vector<std::string>& pythonLspArgs)
{
    pythonLspEnabled = false;
    this->pythonLspPath = pythonLspPath;
    this->pythonLspArgs = pythonLspArgs;

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(!enable)
    {
        if(pythonLspClient)
        {
            pythonLspClient->stop();
            pythonLspClient.reset();
        }
        return;
    }

    std::string rootDir = ".";
    if(!projectRoot.empty())
    {
        rootDir = projectRoot;
    }
    else
    {
        char cwd[PATH_MAX];
        if(getcwd(cwd, sizeof(cwd)))
            rootDir = std::string(cwd);
    }

    std::vector<std::string> args = this->pythonLspArgs;
    if(args.empty())
    {
        args.push_back("--stdio");
    }

    pythonLspClient = std::make_unique<LspClient>();
    if(!pythonLspClient->startServer(this->pythonLspPath, rootDir, args))
    {
        pythonLspClient.reset();

        LOG_ERROR(LOG, "Python LSP failed to start. Python LSP path: {}",
                  this->pythonLspPath);
        return;
    }

    pythonLspEnabled = true;
    LOG_DEBUG(LOG, "Python LSP enabled");
#else
    (void)enable;
    (void)pythonLspPath;
    (void)pythonLspArgs;
    LOG_ERROR(LOG, "python LSP support is not compiled");
#endif
}

bool Editor::isPythonLspEnabled() const
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    return pythonLspEnabled && pythonLspClient && pythonLspClient->running();
#else
    return false;
#endif
}

void Editor::enableMlangLsp(bool enable, const std::string& mlangLspPath,
                            const std::vector<std::string>& mlangLspArgs)
{
    mlangLspEnabled = false;
    this->mlangLspPath = mlangLspPath;
    this->mlangLspArgs = mlangLspArgs;

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(!enable)
    {
        if(mlangLspClient)
        {
            mlangLspClient->stop();
            mlangLspClient.reset();
        }
        return;
    }

    std::string rootDir = ".";
    if(!projectRoot.empty())
    {
        rootDir = projectRoot;
    }
    else
    {
        char cwd[PATH_MAX];
        if(getcwd(cwd, sizeof(cwd)))
            rootDir = std::string(cwd);
    }

    std::vector<std::string> args = this->mlangLspArgs;
    if(args.empty())
        args.push_back("--stdio");

    mlangLspClient = std::make_unique<LspClient>();
    if(!mlangLspClient->startServer(this->mlangLspPath, rootDir, args))
    {
        mlangLspClient.reset();
        LOG_ERROR(LOG, "Mlang LSP failed to start. LSP path: {}",
                  this->mlangLspPath);
        return;
    }

    mlangLspEnabled = true;
    LOG_DEBUG(LOG, "Mlang LSP enabled");
#else
    (void)enable;
    (void)mlangLspPath;
    (void)mlangLspArgs;
    LOG_ERROR(LOG, "Mlang LSP is not compiled");
#endif
}

bool Editor::isMlangLspEnabled() const
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    return mlangLspEnabled && mlangLspClient && mlangLspClient->running();
#else
    return false;
#endif
}

void Editor::enterOperatorPending(char op)
{
    pendingOperator = op;
    pendingAwaitingObject = false;
    pendingObjectType = 0;
    pendingCount = std::max(1, repeatCount);
    commandBuffer.clear(); // keep UI tidy
    setStatusMessage(std::string("Operator: ") + op);
    setMode(OP_PENDING);
}

bool Editor::getTextObjectRange(char objChar, bool around, int& outStartY,
                                int& outStartX, int& outEndY, int& outEndX)
{
    // Current position
    int y = *cursorY;
    int x = *cursorX;

    // Support bracket pairs
    auto findEnclosing = [&](char openc, char closec) -> bool
    {
        // search left for the nearest openc
        int ly = y, lx = x;
        bool foundOpen = false;
        for(;;)
        {
            const std::string& line = (*lines)[ly];
            for(int i = lx; i >= 0; --i)
            {
                if(line[i] == openc)
                {
                    // try to find matching close from here
                    int matchY = ly, matchX = i;
                    // simulate bracket match forward
                    int depth = 0;
                    int ty = matchY, tx = matchX;
                    for(;;)
                    {
                        // move one char forward
                        tx++;
                        while(ty < lines->size() && tx >= (*lines)[ty].length())
                        {
                            ty++;
                            tx = 0;
                            if(ty >= lines->size())
                                break;
                        }
                        if(ty >= lines->size())
                            break;
                        char ch = (*lines)[ty][tx];
                        if(ch == openc)
                            depth++;
                        else if(ch == closec)
                        {
                            if(depth == 0)
                            {
                                // match found at ty,tx
                                outStartY = ly;
                                outStartX = i;
                                outEndY = ty;
                                outEndX = tx;
                                // adjust for 'inner' vs 'around'
                                if(!around)
                                {
                                    // inner: exclude the brackets themselves
                                    // move start forward one char
                                    if(outStartX + 1 <=
                                       (*lines)[outStartY].length())
                                    {
                                        outStartX = outStartX + 1;
                                    }
                                    else
                                    {
                                        // move to next position
                                        outStartY++;
                                        outStartX = 0;
                                    }
                                    // move end back one char
                                    if(outEndX - 1 >= 0)
                                    {
                                        outEndX = outEndX - 1;
                                    }
                                    else
                                    {
                                        // move to previous line end
                                        outEndY--;
                                        outEndX =
                                            (*lines)[outEndY].length() - 1;
                                    }
                                }
                                return true;
                            }
                            else
                            {
                                depth--;
                            }
                        }
                    }
                }
            }
            // move to previous line
            if(ly == 0)
                break;
            ly--;
            if(ly >= 0)
                lx = (*lines)[ly].length() - 1;
        }
        return false;
    };

    if(objChar == '(' || objChar == ')')
    {
        if(findEnclosing('(', ')'))
            return true;
    }
    if(objChar == '{' || objChar == '}')
    {
        if(findEnclosing('{', '}'))
            return true;
    }
    if(objChar == '[' || objChar == ']')
    {
        if(findEnclosing('[', ']'))
            return true;
    }

    // Quotes: find nearest pair of quotes in current line (simple)
    if(objChar == '"' || objChar == '\'')
    {
        const std::string& line = (*lines)[y];
        // search left for quote
        int lpos = -1, rpos = -1;
        for(int i = x; i >= 0; --i)
            if(line[i] == objChar)
            {
                lpos = i;
                break;
            }
        for(int i = x; i < line.length(); ++i)
            if(line[i] == objChar)
            {
                rpos = i;
                break;
            }

        if(lpos >= 0 && rpos >= 0 && lpos < rpos)
        {
            outStartY = y;
            outEndY = y;
            if(around)
            {
                outStartX = lpos;
                outEndX = rpos;
            }
            else
            {
                outStartX = lpos + 1;
                outEndX = rpos - 1;
            }
            return true;
        }
    }

    // Word objects: iw / aw
    if(objChar == 'w')
    {
        // For inner word -> find word boundaries around cursor on same line
        const std::string& line = (*lines)[y];
        int L = x, R = x;
        // If cursor at end-of-line and not in word, try next char
        if(L >= line.length())
            L = line.length() - 1;
        // move L to start of word
        while(L > 0 && !isWordChar(line[L]))
            L--;
        while(L > 0 && isWordChar(line[L - 1]))
            L--;
        // move R to end of word
        while(R < (int)line.length() && isWordChar(line[R]))
            R++;
        if(R <= L)
            return false;
        outStartY = y;
        outEndY = y;
        if(around)
        {
            outStartX = L;
            outEndX = R - 1;
        } // 'aw' includes trailing space? keep simple: word only
        else
        {
            outStartX = L;
            outEndX = R - 1;
        }
        return true;
    }

    // Paragraph 'p' (simple: blank-line separated)
    if(objChar == 'p')
    {
        int sy = y, ey = y;
        // find paragraph start
        while(sy > 0 && !(*lines)[sy].empty())
            sy--;
        if((*lines)[sy].empty() && sy < y)
            sy++;
        // find paragraph end
        while(ey < lines->size() - 1 && !(*lines)[ey].empty())
            ey++;
        if((*lines)[ey].empty() && ey > y)
            ey--;
        outStartY = sy;
        outEndY = ey;
        outStartX = 0;
        outEndX = (*lines)[outEndY].length() - 1;
        return true;
    }

    return false;
}

void Editor::applyOperatorToRange(char op, int startY, int startX, int endY,
                                  int endX)
{
    // Normalize bounds
    if(startY > endY || (startY == endY && startX > endX))
    {
        std::swap(startY, endY);
        std::swap(startX, endX);
    }

    // Yank if 'y' or for 'd' we fill yankBuffer
    if(op == 'y' || op == 'd' || op == 'c')
    {
        yankRange(startY, startX, endY, endX);
    }

    if(op == 'd' || op == 'c')
    {
        deleteRange(startY, startX, endY, endX);
        saveState();
    }

    if(op == '=')
    {
        // For indent operator, we indent all lines in the range
        // For line-wise motions or when the range spans multiple lines
        autoIndentRange(startY, endY);

        int linesIndented = endY - startY + 1;
        setStatusMessage(std::to_string(linesIndented) + " line" +
                         (linesIndented > 1 ? "s" : "") + " indented");
        saveState();
    }

    if(op == 'c')
    {
        // After change, enter insert mode at start
        *cursorY = startY;
        *cursorX = startX;
    }
    else
    {
        // Place cursor at start of affected range (or keep it for indent)
        if(op != '=')
        {
            *cursorY = startY;
            *cursorX = startX;
        }
    }

    needsFullRedraw = true;
    *dirty = true;
}

// yankRange and deleteRange are now in text_operations.cpp

void Editor::setMode(Mode mode)
{
    ensureBufferForMode(mode);
    currentMode = mode;
    needsFullRedraw = true;

    if(!modeStateMachine)
    {
        if(mode == INSERT)
        {
            Terminal::setCursorBarBlinking();
        }
        else
        {
            Terminal::setCursorBlock();
        }
        return;
    }

    switch(mode)
    {
    case WELCOME:
        modeStateMachine->transitionTo(WelcomeMode{});
        break;
    case NORMAL:
        modeStateMachine->transitionTo(NormalMode{});
        break;
    case INSERT:
        modeStateMachine->transitionTo(InsertMode{});
        break;
    case REPLACE:
        modeStateMachine->transitionTo(ReplaceMode{});
        break;
    case VISUAL:
        modeStateMachine->transitionTo(VisualMode{});
        break;
    case VISUAL_LINE:
        modeStateMachine->transitionTo(VisualLineMode{});
        break;
    case VISUAL_BLOCK:
        modeStateMachine->transitionTo(VisualBlockMode{});
        break;
    case COMMAND:
        modeStateMachine->transitionTo(CommandMode{});
        break;
    case SEARCH_FORWARD:
        modeStateMachine->transitionTo(SearchForwardMode{});
        break;
    case SEARCH_BACKWARD:
        modeStateMachine->transitionTo(SearchBackwardMode{});
        break;
    case FILE_BROWSER:
        modeStateMachine->transitionTo(FileBrowserMode{});
        break;
    case FUZZY_FIND:
        modeStateMachine->transitionTo(FuzzyFindMode{});
        break;
    case BUFFER_BROWSER:
        modeStateMachine->transitionTo(BufferBrowserMode{});
        break;
    case GREP_SEARCH:
        modeStateMachine->transitionTo(GrepSearchMode{});
        break;
    case OP_PENDING:
        modeStateMachine->transitionTo(
            OperatorPendingMode{pendingOperator, pendingCount});
        break;
    case REFERENCES:
        modeStateMachine->transitionTo(ReferencesMode{});
        break;
    case LSP_INFO:
        modeStateMachine->transitionTo(LspInfoMode{});
        break;
    case HELP:
        modeStateMachine->transitionTo(HelpMode{});
        break;
    case GIT_SHOW:
        modeStateMachine->transitionTo(GitShowCommitMode{});
        break;
    case GIT_LOG:
        modeStateMachine->transitionTo(GitLogMode{});
        break;
    }

    syncModeFromStateMachine();
}

std::string Editor::getModeString() const
{
    switch(currentMode)
    {
    case WELCOME:
        return "WELCOME";
    case NORMAL:
        return "NORMAL";
    case INSERT:
        return "INSERT";
    case REPLACE:
        return "REPLACE";
    case VISUAL:
        return "VISUAL";
    case VISUAL_LINE:
        return "VISUAL LINE";
    case VISUAL_BLOCK:
        return "VISUAL BLOCK";
    case COMMAND:
        return "COMMAND";
    case SEARCH_FORWARD:
        return "/";
    case SEARCH_BACKWARD:
        return "?";
    case FILE_BROWSER:
        return "BROWSE";
    case FUZZY_FIND:
        return "FUZZY";
    case BUFFER_BROWSER:
        return "BUFFERS";
    case GREP_SEARCH:
        return "GREP";
    case LSP_INFO:
        return "LSP INFO";
    case REFERENCES:
        return "REFERENCES";
    case OP_PENDING:
        return "OP_PENDING";
    case HELP:
        return "HELP";
    case GIT_SHOW:
        return "GITSHOW";
    case GIT_LOG:
        return "GITLOG";
    }
    return "";
}

void Editor::openFile(std::string_view fname)
{
    // Normalize path (CRITICAL for buffer matching)
    std::string path(fname);
    try
    {
        path = std::filesystem::canonical(std::string(fname)).string();
    }
    catch(...)
    {
        // fallback if file doesn't exist yet
        path = fname;
    }

    // Check if file already open
    int existing = findBufferByFilename(path);
    if(existing >= 0)
    {
        switchToBuffer(existing);
        //        setStatusMessage("Buffer " + std::to_string(existing + 1) +
        //        "/" +
        //                         std::to_string(buffers.size()));
        return;
    }

    // Reuse a clean unnamed buffer if it exists, otherwise create a new one.
    bool reused = false;
    for(size_t i = 0; i < buffers.size(); ++i)
    {
        Buffer* buf = buffers[i].get();
        if(buf->filename.empty() && !buf->dirty && buf->lines.size() == 1 &&
           buf->lines[0].empty())
        {
            switchToBuffer((int)i);
            reused = true;
            break;
        }
    }
    if(!reused)
        createNewBuffer();

    *filename = path;
    lines->clear();

    std::ifstream file(path);
    if(file.is_open())
    {
        std::string line;
        while(std::getline(file, line))
        {
            if(!line.empty() && line.back() == '\r')
                line.pop_back();
            lines->push_back(line);
        }
        file.close();
    }

    if(lines->empty())
        lines->push_back("");

    *dirty = false;
    *cursorX = *cursorY = 0;
    *offsetX = *offsetY = 0;
    currentBuffer->lspSyncNeeded = false;
    currentBuffer->lspHashValid = false;
    currentBuffer->lspDiagnosticsSeenValid = false;
    currentBuffer->lspDiagnosticsSeenRevision = 0;
    currentBuffer->clangIndentWidthValid = false;
    currentBuffer->clangIndentWidth = -1;
    currentBuffer->clangBraceStyleValid = false;
    currentBuffer->clangBraceNewLine = false;

    // Record file modification time for external change detection
    std::error_code ec;
    auto ftime = std::filesystem::last_write_time(path, ec);
    if(!ec)
    {
        currentBuffer->lastModificationTime = ftime;
    }

    // Reset undo state cleanly
    currentBuffer->undoStack.clear();
    currentBuffer->undoIndex = -1;
    saveState();
    currentBuffer->savedUndoIndex = currentBuffer->undoIndex;

    needsFullRedraw = true;

#ifdef UVIM_ENABLE_CLANGD_LSP
    // Notify LSP about the newly opened file so gd works from system headers
    if(isClangdLspEnabled() && isFileType<FileType::Cpp>() &&
       !isFileType<FileType::Mla>() && lspClient)
    {
        // Build text content from loaded lines
        std::string text;
        text.reserve(lines->size() * 80);
        for(size_t i = 0; i < lines->size(); ++i)
        {
            text += (*lines)[i];
            if(i + 1 < lines->size())
                text.push_back('\n');
        }
        // didChange will call didOpen if needed
        lspClient->didChange(path, text, "cpp");
        currentBuffer->lspSyncNeeded = false;
    }
    if(isRobotLspEnabled() && isFileType<FileType::Robot>() && robotLspClient)
    {
        std::string text;
        text.reserve(lines->size() * 80);
        for(size_t i = 0; i < lines->size(); ++i)
        {
            text += (*lines)[i];
            if(i + 1 < lines->size())
                text.push_back('\n');
        }
        robotLspClient->didChange(path, text, "robotframework");
    }
    if(isPythonLspEnabled() && isFileType<FileType::Python>() &&
       pythonLspClient)
    {
        std::string text;
        text.reserve(lines->size() * 80);
        for(size_t i = 0; i < lines->size(); ++i)
        {
            text += (*lines)[i];
            if(i + 1 < lines->size())
                text.push_back('\n');
        }
        pythonLspClient->didChange(path, text, "python");
    }
    if(isMlangLspEnabled() && isFileType<FileType::Mla>() && mlangLspClient)
    {
        std::string text;
        text.reserve(lines->size() * 80);
        for(size_t i = 0; i < lines->size(); ++i)
        {
            text += (*lines)[i];
            if(i + 1 < lines->size())
                text.push_back('\n');
        }
        mlangLspClient->didChange(path, text, "mlang");
    }
#endif

    //    setStatusMessage("Buffer " + std::to_string(currentBufferIndex + 1) +
    //    "/" +
    //                     std::to_string(buffers.size()) + " " +
    //                     std::to_string(lines->size()) + " lines");
}

void Editor::openFileBrowser(std::string_view path)
{
    std::string prev;
    if(currentMode != FILE_BROWSER && currentBuffer != nullptr && filename)
    {
        prev = *filename;
    }

    if(modeStateMachine)
    {
        modeStateMachine->transitionTo(
            FileBrowserMode{std::string(path), prev});
        syncModeFromStateMachine();
    }
    else
    {
        setMode(FILE_BROWSER);
    }
}

bool Editor::formatBufferForSave()
{
#ifdef UVIM_TESTING
    if(formatOnSaveTestHook)
        return formatOnSaveTestHook();
#endif
    if(isFileType<FileType::Python>())
        return pythonFormatBuffer();
    if(isFileType<FileType::Mla>())
        return mlangFormatBuffer();
    if(isFileType<FileType::Cpp>() ||
       (filename && !filename->empty() && isHeaderFile(*filename)))
        return clangFormatWithArgs("", "clang-format: formatted file");
    return false;
}

void Editor::saveFile()
{
    if(filename->empty())
    {
        setStatusMessage("No file name");
        return;
    }

    if(formatOnSave)
        formatBufferForSave();

    // Clean up lines before saving: convert tabs to spaces, remove trailing
    // whitespace
    int linesModified = 0;
    for(size_t lineIdx = 0; lineIdx < lines->size(); lineIdx++)
    {
        std::string& line = (*lines)[lineIdx];
        std::string original = line;

        // Convert tabs to spaces (4 spaces per tab, aligned to tab stops)
        std::string expanded;
        expanded.reserve(line.size());
        int col = 0;
        for(char c : line)
        {
            if(c == '\t')
            {
                // Add spaces to reach next tab stop (every 4 columns)
                int spacesToAdd = 4 - (col % 4);
                expanded.append(spacesToAdd, ' ');
                col += spacesToAdd;
            }
            else
            {
                expanded += c;
                col++;
            }
        }
        line = expanded;

        // Remove trailing whitespace
        size_t endPos = line.find_last_not_of(" \t");
        if(endPos != std::string::npos)
        {
            line = line.substr(0, endPos + 1);
        }
        else if(!line.empty())
        {
            // Line is all whitespace
            line.clear();
        }

        if(line != original)
        {
            linesModified++;

            // Adjust cursor if on this line and beyond the new line length
            if((int)lineIdx == *cursorY && *cursorX > (int)line.length())
            {
                *cursorX = line.length() > 0 ? line.length() - 1 : 0;
            }
        }
    }

    std::ofstream file(*filename);
    if(file.is_open())
    {
        for(const auto& line : *lines)
        {
            file << line << '\n';
        }
        file.close();
        *dirty = false;
        currentBuffer->savedUndoIndex =
            currentBuffer->undoIndex; // Mark this state as saved

        // Update file modification time after saving
        std::error_code ec;
        auto ftime = std::filesystem::last_write_time(*filename, ec);
        if(!ec)
        {
            currentBuffer->lastModificationTime = ftime;
        }

        std::string msg = "\"" + *filename + "\" " +
                          std::to_string(lines->size()) + "L written";
        if(linesModified > 0)
        {
            msg += " (" + std::to_string(linesModified) + " lines cleaned)";
            needsFullRedraw = true; // Redraw to show cleaned lines
        }
        setStatusMessage(msg);
    }
    else
    {
        setStatusMessage("Can't save! I/O error");
    }
}

void Editor::checkFileChanges()
{
    // Only check if we have a valid file and buffer
    if(!currentBuffer || filename->empty() || *dirty)
        return;

    std::error_code ec;

    // Check if file still exists
    if(!std::filesystem::exists(*filename, ec) || ec)
        return;

    // Get current modification time
    auto currentTime = std::filesystem::last_write_time(*filename, ec);
    if(ec)
        return;

    // Compare with stored modification time
    if(currentTime != currentBuffer->lastModificationTime)
    {
        // File has been modified externally, reload it
        reloadCurrentFile();
    }
}

void Editor::reloadCurrentFile()
{
    if(!currentBuffer || filename->empty())
        return;

    // Save cursor position
    int savedCursorX = *cursorX;
    int savedCursorY = *cursorY;
    int savedOffsetX = *offsetX;
    int savedOffsetY = *offsetY;

    std::string filepath = *filename;

    // Reload the file
    lines->clear();

    std::ifstream file(filepath);
    if(file.is_open())
    {
        std::string line;
        while(std::getline(file, line))
        {
            if(!line.empty() && line.back() == '\r')
                line.pop_back();
            lines->push_back(line);
        }
        file.close();
    }

    if(lines->empty())
        lines->push_back("");

    // Update modification time
    std::error_code ec;
    auto ftime = std::filesystem::last_write_time(filepath, ec);
    if(!ec)
    {
        currentBuffer->lastModificationTime = ftime;
    }

    // Restore cursor position (clamped to valid range)
    *cursorY = std::min(savedCursorY, (int)lines->size() - 1);
    *cursorX = std::min(savedCursorX, (int)(*lines)[*cursorY].length());
    *offsetX = savedOffsetX;
    *offsetY =
        std::min(savedOffsetY, std::max(0, (int)lines->size() - screenRows));

    *dirty = false;
    needsFullRedraw = true;

    setStatusMessage("File reloaded from disk");
}

// Jump between header and source file
bool Editor::fileExists(const std::string& path)
{
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

std::string Editor::getSymbolUnderCursor()
{
    if(*cursorY >= lines->size())
        return "";

    const std::string& line = (*lines)[*cursorY];
    int x = *cursorX;

    if(x >= line.size() || !isIdent(line[x]))
        return "";

    int l = x;
    int r = x;

    while(l > 0 && isIdent(line[l - 1]))
        l--;
    while(r < line.size() && isIdent(line[r]))
        r++;

    symbolPrefix.clear();
    int prefixStart = l;
    while(prefixStart >= 2 && line[prefixStart - 1] == ':' &&
          line[prefixStart - 2] == ':')
    {
        int p = prefixStart - 3;
        while(p >= 0 && isIdent(line[p]))
            p--;
        if(p + 1 >= prefixStart - 1)
            break;
        prefixStart = p + 1;
    }

    if(prefixStart < l)
    {
        symbolPrefix = line.substr(prefixStart, l - prefixStart);
    }

    return line.substr(l, r - l);
}

std::string Editor::findAlternateFile(const std::string& currentFile)
{
    if(currentFile.empty())
        return "";

    // Find the last dot to get the extension
    size_t lastDot = currentFile.find_last_of('.');
    if(lastDot == std::string::npos)
        return "";

    std::string baseName = currentFile.substr(0, lastDot);
    std::string extension = currentFile.substr(lastDot);

    // List of header extensions
    static const std::vector<std::string> headerExts = {".h", ".hpp", ".hxx",
                                                        ".H", ".HPP", ".HXX"};

    // List of source extensions
    static const std::vector<std::string> sourceExts = {
        ".cpp", ".cc", ".cxx", ".c", ".C", ".CPP", ".CC", ".CXX"};

    // Check if current file is a header
    bool isHeader = false;
    for(const auto& ext : headerExts)
    {
        if(extension == ext)
        {
            isHeader = true;
            break;
        }
    }

    // Try to find the alternate file
    std::vector<std::string> candidates;

    if(isHeader)
    {
        // Current file is a header, look for source files
        for(const auto& ext : sourceExts)
        {
            candidates.push_back(baseName + ext);
        }
    }
    else
    {
        // Current file is likely a source, look for header files
        for(const auto& ext : headerExts)
        {
            candidates.push_back(baseName + ext);
        }
    }

    // Also check in common relative directories
    size_t lastSlash = currentFile.find_last_of('/');
    std::string dir = "";
    std::string fileName = currentFile;

    if(lastSlash != std::string::npos)
    {
        dir = currentFile.substr(0, lastSlash + 1);
        fileName = currentFile.substr(lastSlash + 1);
        baseName = fileName.substr(0, fileName.find_last_of('.'));
    }

    // Common directory pairs
    std::vector<std::pair<std::string, std::string>> dirPairs = {
        {"src/", "include/"},
        {"source/", "include/"},
        {"src/", "inc/"},
        {"source/", "headers/"},
        {"lib/", "include/"},
        {"", "../include/"},
        {"", "../inc/"},
        {"include/", "../src/"},
        {"include/", "../source/"},
        {"inc/", "../src/"},
        {"headers/", "../source/"},
        {"include/", "../lib/"},
    };

    // Add candidates from related directories
    for(const auto& [srcDir, incDir] : dirPairs)
    {
        if(dir.find(srcDir) != std::string::npos && isHeader == false)
        {
            // We're in a source dir, look for headers in include dir
            std::string altDir = dir;
            size_t pos = altDir.find(srcDir);
            if(pos != std::string::npos)
            {
                altDir.replace(pos, srcDir.length(), incDir);
                for(const auto& ext : headerExts)
                {
                    candidates.push_back(altDir + baseName + ext);
                }
            }
        }
        else if(dir.find(incDir) != std::string::npos && isHeader == true)
        {
            // We're in an include dir, look for sources in source dir
            std::string altDir = dir;
            size_t pos = altDir.find(incDir);
            if(pos != std::string::npos)
            {
                altDir.replace(pos, incDir.length(), srcDir);
                for(const auto& ext : sourceExts)
                {
                    candidates.push_back(altDir + baseName + ext);
                }
            }
        }
    }

    // Check which candidate exists
    for(const auto& candidate : candidates)
    {
        if(fileExists(candidate))
        {
            return candidate;
        }
    }

    return "";
}

void Editor::jumpToAlternateFile()
{
    if(filename->empty())
    {
        setStatusMessage("No file currently open");
        return;
    }

    std::string alternate = findAlternateFile(*filename);

    if(alternate.empty())
    {
        setStatusMessage("No alternate file found for " + *filename);
        return;
    }

    // Check if the alternate file is already open in a buffer
    int bufferIndex = findBufferByFilename(alternate);

    if(bufferIndex >= 0)
    {
        // Switch to existing buffer
        switchToBuffer(bufferIndex);
        setStatusMessage("Switched to " + alternate);
    }
    else
    {
        // Open the alternate file in a new buffer
        openFile(alternate);
        setStatusMessage("Opened " + alternate);
    }
}

// Movement implementations

// Cursor movement methods (moveLeft through moveToMatchingBracket) are now in
// cursor_movement.cpp

bool Editor::isWordChar(char c) const
{
    if(std::isspace((unsigned char)c))
        return false;
    if(std::isalnum((unsigned char)c) || c == '_')
        return true; // letters/numbers
    // punctuation counts as “word” for w/dw/cw
    return true;
}

// Editing operations

// Text operations (insertChar through pasteBefore) are now in
// text_operations.cpp

void Editor::startVisualMode()
{
    setMode(VISUAL);
}

void Editor::startVisualLineMode()
{
    setMode(VISUAL_LINE);
}

void Editor::startVisualBlockMode()
{
    setMode(VISUAL_BLOCK);
    currentBuffer->visualBlockStartX = *cursorX;
    currentBuffer->visualBlockStartY = *cursorY;
    currentBuffer->visualBlockEndX = *cursorX;
    currentBuffer->visualBlockEndY = *cursorY;
    currentBuffer->visualBlockInsertText.clear();
}

void Editor::updateVisualSelection()
{
    currentBuffer->visualEndX = *cursorX;
    currentBuffer->visualEndY = *cursorY;
}

void Editor::updateVisualBlockSelection()
{
    currentBuffer->visualBlockEndX = *cursorX;
    currentBuffer->visualBlockEndY = *cursorY;
}

bool Editor::isInSelection(int row, int col)
{
    if(currentMode != VISUAL && currentMode != VISUAL_LINE)
        return false;

    if(currentMode == VISUAL_LINE)
    {
        int startY =
            std::min(currentBuffer->visualStartY, currentBuffer->visualEndY);
        int endY =
            std::max(currentBuffer->visualStartY, currentBuffer->visualEndY);
        return row >= startY && row <= endY;
    }

    int startY, startX, endY, endX;
    getSelectionBounds(startY, startX, endY, endX);

    if(row < startY || row > endY)
        return false;
    if(row == startY && row == endY)
        return col >= startX && col <= endX;
    if(row == startY)
        return col >= startX;
    if(row == endY)
        return col <= endX;
    return true;
}

bool Editor::isInVisualBlock(int row, int col)
{
    if(currentMode != VISUAL_BLOCK)
        return false;

    int startY, startX, endY, endX;
    getVisualBlockBounds(startY, startX, endY, endX);

    return row >= startY && row <= endY && col >= startX && col <= endX;
}

void Editor::getVisualBlockBounds(int& startY, int& startX, int& endY,
                                  int& endX)
{
    startY = std::min(currentBuffer->visualBlockStartY,
                      currentBuffer->visualBlockEndY);
    endY = std::max(currentBuffer->visualBlockStartY,
                    currentBuffer->visualBlockEndY);
    startX = std::min(currentBuffer->visualBlockStartX,
                      currentBuffer->visualBlockEndX);
    endX = std::max(currentBuffer->visualBlockStartX,
                    currentBuffer->visualBlockEndX);
}

void Editor::getSelectionBounds(int& startY, int& startX, int& endY, int& endX)
{
    if(currentBuffer->visualStartY < currentBuffer->visualEndY ||
       (currentBuffer->visualStartY == currentBuffer->visualEndY &&
        currentBuffer->visualStartX <= currentBuffer->visualEndX))
    {
        startY = currentBuffer->visualStartY;
        startX = currentBuffer->visualStartX;
        endY = currentBuffer->visualEndY;
        endX = currentBuffer->visualEndX;
    }
    else
    {
        startY = currentBuffer->visualEndY;
        startX = currentBuffer->visualEndX;
        endY = currentBuffer->visualStartY;
        endX = currentBuffer->visualStartX;
    }
}

// deleteVisualBlock, yankVisualBlock, changeVisualBlock,
// applyVisualBlockInsert, deleteSelection are now in text_operations.cpp

std::string Editor::toLowerCase(const std::string& str)
{
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

int Editor::getLineIndent(int line)
{
    if(line < 0 || line >= (int)lines->size())
        return 0;

    const std::string& text = (*lines)[line];
    int indent = 0;
    for(char c : text)
    {
        if(c == ' ')
            indent++;
        else if(c == '\t')
            indent += 4; // Treat tab as 4 spaces
        else
            break;
    }
    return indent;
}

void Editor::indentLine(int line, int spaces)
{
    if(line < 0 || line >= (int)lines->size())
        return;

    std::string& text = (*lines)[line];

    // Remove existing indentation
    size_t firstNonSpace = 0;
    while(firstNonSpace < text.length() &&
          (text[firstNonSpace] == ' ' || text[firstNonSpace] == '\t'))
    {
        firstNonSpace++;
    }

    // Build new indentation
    std::string newIndent(spaces, ' ');
    text = newIndent + text.substr(firstNonSpace);
    *dirty = true;
}

// Auto-indent a line based on the previous line and C++ syntax rules
void Editor::autoIndentLine(int line)
{
    if(line < 0 || line >= (int)lines->size())
        return;

    // Get the content of current line (without leading spaces)
    std::string currentLine = (*lines)[line];
    size_t firstNonSpace = currentLine.find_first_not_of(" \t");
    if(firstNonSpace != std::string::npos)
        currentLine = currentLine.substr(firstNonSpace);
    else
        currentLine = "";

    // Start with previous line's indent
    int baseIndent = 0;
    if(line > 0)
    {
        baseIndent = getLineIndent(line - 1);

        // Check if previous line ends with { or starts a block
        const std::string& prevLine = (*lines)[line - 1];
        size_t lastNonSpace = prevLine.find_last_not_of(" \t\r\n");
        if(lastNonSpace != std::string::npos)
        {
            char lastChar = prevLine[lastNonSpace];
            if(lastChar == '{')
            {
                baseIndent += 4; // Increase indent after opening brace
            }
            else if(lastChar == ':' &&
                    (prevLine.find("public") != std::string::npos ||
                     prevLine.find("private") != std::string::npos ||
                     prevLine.find("protected") != std::string::npos ||
                     prevLine.find("case") != std::string::npos ||
                     prevLine.find("default") != std::string::npos))
            {
                baseIndent += 4; // Increase indent after class access
                                 // specifiers or case labels
            }
        }
    }

    // Check if current line starts with closing brace or special keywords
    if(!currentLine.empty())
    {
        if(currentLine[0] == '}')
        {
            baseIndent = std::max(
                0, baseIndent - 4); // Decrease indent for closing brace
        }
        else if(currentLine.find("public:") == 0 ||
                currentLine.find("private:") == 0 ||
                currentLine.find("protected:") == 0)
        {
            // Access specifiers typically have less indent than class members
            if(line > 0 && baseIndent >= 4)
                baseIndent -= 4;
        }
        else if(currentLine.find("case ") == 0 ||
                currentLine.find("default:") == 0)
        {
            // Case labels typically align with switch
            if(baseIndent >= 4)
                baseIndent -= 4;
        }
    }

    indentLine(line, baseIndent);
}

// Auto-indent a range of lines
void Editor::autoIndentRange(int startLine, int endLine)
{
    if(startLine > endLine)
        std::swap(startLine, endLine);

    startLine = std::max(0, startLine);
    endLine = std::min((int)lines->size() - 1, endLine);

    for(int i = startLine; i <= endLine; i++)
    {
        autoIndentLine(i);
    }

    *dirty = true;
    needsFullRedraw = true;
}

// Helper function to extract include path from a line
static std::pair<std::string, bool> extractIncludePath(const std::string& line)
{
    // Returns: {includePath, isSystemInclude}
    // isSystemInclude = true for <>, false for ""

    size_t includePos = line.find("#include");
    if(includePos == std::string::npos)
        return {"", false};

    // Find the opening delimiter after #include
    size_t pos = includePos + 8; // skip "#include"
    while(pos < line.length() && std::isspace(line[pos]))
        pos++;

    if(pos >= line.length())
        return {"", false};

    bool isSystem = false;
    char openDelim = line[pos];
    char closeDelim;

    if(openDelim == '<')
    {
        isSystem = true;
        closeDelim = '>';
    }
    else if(openDelim == '"')
    {
        isSystem = false;
        closeDelim = '"';
    }
    else
    {
        return {"", false};
    }

    pos++; // skip opening delimiter
    size_t start = pos;
    size_t end = line.find(closeDelim, pos);

    if(end == std::string::npos)
        return {"", false};

    return {line.substr(start, end - start), isSystem};
}

// Helper function to resolve system include path
static std::string resolveSystemInclude(const std::string& includeName)
{
    // Common system include paths (will be searched in order)
    std::vector<std::string> systemPaths;

    // Try to get paths from clang/g++ compiler
    // This is a heuristic approach that works on most systems

#ifdef __APPLE__
    // macOS with Xcode
    systemPaths = {
        "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/"
        "Developer/SDKs/MacOSX.sdk/usr/include/c++/v1",
        "/Applications/Xcode.app/Contents/Developer/Toolchains/"
        "XcodeDefault.xctoolchain/usr/lib/clang/17/include",
        "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/"
        "Developer/SDKs/MacOSX.sdk/usr/include",
        "/usr/local/include",
        "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/"
        "v1",
        "/Library/Developer/CommandLineTools/usr/include/c++/v1",
    };
#else
    // Linux/Unix
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

    // Search for the file in system paths
    for(const auto& basePath : systemPaths)
    {
        std::string fullPath = basePath + "/" + includeName;
        std::error_code ec;
        if(fs::exists(fullPath, ec) && !ec)
        {
            return fullPath;
        }
    }

    return "";
}

void Editor::goToDefinition()
{
    // First, check if we're on an #include line
    if(*cursorY >= 0 && *cursorY < (int)lines->size())
    {
        const std::string& currentLine = (*lines)[*cursorY];
        auto [includePath, isSystem] = extractIncludePath(currentLine);

        if(!includePath.empty())
        {
            std::string resolvedPath;

            if(isSystem)
            {
                // System include: search in system paths
                resolvedPath = resolveSystemInclude(includePath);
            }
            else
            {
                // Local include: resolve relative to current file's directory
                std::string currentDir = ".";
                if(!filename->empty())
                {
                    size_t lastSlash = filename->rfind('/');
                    if(lastSlash != std::string::npos)
                    {
                        currentDir = filename->substr(0, lastSlash);
                    }
                }

                std::string tryPath = currentDir + "/" + includePath;
                std::error_code ec;
                if(fs::exists(tryPath, ec) && !ec)
                {
                    resolvedPath = tryPath;
                }
            }

            if(!resolvedPath.empty())
            {
                pushJumpLocation();
                openFile(resolvedPath);

                // Show appropriate message
                std::string displayPath = resolvedPath;
                if(isSystem && resolvedPath.length() > 50)
                {
                    // Show shortened path for system headers
                    size_t lastSlash = resolvedPath.rfind('/');
                    if(lastSlash != std::string::npos)
                        displayPath =
                            ".../" + resolvedPath.substr(lastSlash + 1);
                }
                setStatusMessage("gd → " + displayPath);
                return;
            }
            else
            {
                setStatusMessage("gd: include file not found: " + includePath);
                return;
            }
        }
    }

    std::string symbol = getSymbolUnderCursor();
    if(symbol.empty())
    {
        setStatusMessage("gd: no symbol");
        return;
    }

    auto apply_gd_viewport = [&]()
    {
        if(gdCenterScreen)
            centerScreen();
        else
            adjustViewport();
    };

    bool isStdSymbol = false;
    if(*cursorY >= 0 && *cursorY < (int)lines->size())
    {
        const std::string& line = (*lines)[*cursorY];
        int x = *cursorX;
        if(x >= 0 && x < (int)line.size() && isIdent(line[x]))
        {
            int l = x;
            while(l > 0 && isIdent(line[l - 1]))
                l--;
            if(l >= 5 && line.compare(l - 5, 5, "std::") == 0)
            {
                isStdSymbol = true;
            }
        }
    }

    if(isStdSymbol || symbolPrefix.rfind("std::", 0) == 0)
    {
        std::string headerName = stdlib_goto::headerForSymbol(symbol);
        if(!headerName.empty())
        {
            std::string header = resolveSystemInclude(headerName);
            if(!header.empty())
            {
                pushJumpLocation();
                openFile(header);
                setStatusMessage(std::string("gd → <sys>/") + headerName);
                return;
            }
        }
    }

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(isRobotLspEnabled() && isFileType<FileType::Robot>())
    {
        std::string text;
        text.reserve(lines->size() * 80);
        for(size_t i = 0; i < lines->size(); ++i)
        {
            text += (*lines)[i];
            if(i + 1 < lines->size())
                text.push_back('\n');
        }

        robotLspClient->didChange(currentBuffer->filename, text,
                                  "robotframework");
        auto loc = robotLspClient->definition(currentBuffer->filename, *cursorY,
                                              *cursorX);
        if(loc)
        {
            pushJumpLocation();
            openFile(loc->path);
            *cursorY = loc->line;
            *cursorX = loc->character;

            if(*cursorY >= (int)lines->size())
                *cursorY = lines->size() > 0 ? lines->size() - 1 : 0;
            if(*cursorY >= 0 && *cursorX > (int)(*lines)[*cursorY].length())
                *cursorX = (*lines)[*cursorY].length();

            apply_gd_viewport();
            setStatusMessage("gd (robot) → " + loc->path + ":" +
                             std::to_string(loc->line + 1));
            return;
        }

        std::string_view lineView;
        if(*cursorY >= 0 && *cursorY < (int)lines->size())
            lineView = (*lines)[*cursorY];
        std::string_view keyword = robot_first_cell(lineView);
        if(keyword.empty())
        {
            setStatusMessage("gd (robot): no keyword");
            return;
        }

        int defY = -1;
        int defX = 0;
        if(find_robot_keyword_in_file(currentBuffer->filename, keyword, defY,
                                      defX))
        {
            *cursorY = defY;
            *cursorX = defX;
            apply_gd_viewport();
            setStatusMessage("gd (robot) → " + *filename + ":" +
                             std::to_string(defY + 1));
            return;
        }

        std::filesystem::path root = std::filesystem::current_path();
        std::error_code ec;
        for(std::filesystem::recursive_directory_iterator it(
                root,
                std::filesystem::directory_options::skip_permission_denied, ec),
            end;
            it != end; ++it)
        {
            if(it->is_directory(ec) && is_skip_dir(it->path()))
            {
                it.disable_recursion_pending();
                continue;
            }
            if(!it->is_regular_file(ec))
                continue;
            const auto& p = it->path();
            std::string ext = p.extension().string();
            if(ext != ".robot" && ext != ".resource" &&
               ext != ".robotframework")
                continue;
            if(find_robot_keyword_in_file(p.string(), keyword, defY, defX))
            {
                pushJumpLocation();
                openFile(p.string());
                *cursorY = defY;
                *cursorX = defX;
                apply_gd_viewport();
                setStatusMessage("gd (robot) → " + p.string() + ":" +
                                 std::to_string(defY + 1));
                return;
            }
        }

        setStatusMessage("gd (robot): not found");
        return;
    }

    if(isPythonLspEnabled() && isFileType<FileType::Python>())
    {
        std::string text;
        text.reserve(lines->size() * 80);
        for(size_t i = 0; i < lines->size(); ++i)
        {
            text += (*lines)[i];
            if(i + 1 < lines->size())
                text.push_back('\n');
        }

        pythonLspClient->didChange(currentBuffer->filename, text, "python");
        auto loc = pythonLspClient->definition(currentBuffer->filename,
                                               *cursorY, *cursorX);
        if(loc)
        {
            pushJumpLocation();
            openFile(loc->path);
            *cursorY = loc->line;
            *cursorX = loc->character;

            if(*cursorY >= (int)lines->size())
                *cursorY = lines->size() > 0 ? lines->size() - 1 : 0;
            if(*cursorY >= 0 && *cursorX > (int)(*lines)[*cursorY].length())
                *cursorX = (*lines)[*cursorY].length();

            apply_gd_viewport();
            setStatusMessage("gd (python) → " + loc->path + ":" +
                             std::to_string(loc->line + 1));
            return;
        }

        std::string symbol = getSymbolUnderCursor();
        if(symbol.empty())
        {
            setStatusMessage("gd (python): no symbol");
            return;
        }

        int defY = -1;
        int defX = 0;
        if(find_python_def_in_file(currentBuffer->filename, symbol, defY, defX))
        {
            *cursorY = defY;
            *cursorX = defX;
            apply_gd_viewport();
            setStatusMessage("gd (python) → " + *filename + ":" +
                             std::to_string(defY + 1));
            return;
        }

        std::filesystem::path root = std::filesystem::current_path();
        std::error_code ec;
        for(std::filesystem::recursive_directory_iterator it(
                root,
                std::filesystem::directory_options::skip_permission_denied, ec),
            end;
            it != end; ++it)
        {
            if(it->is_directory(ec) && is_skip_dir(it->path()))
            {
                it.disable_recursion_pending();
                continue;
            }
            if(!it->is_regular_file(ec))
                continue;
            const auto& p = it->path();
            if(!constants::is_filetype<constants::no_pattern,
                                       constants::python_suffixes>(p.string()))
                continue;
            if(find_python_def_in_file(p.string(), symbol, defY, defX))
            {
                pushJumpLocation();
                openFile(p.string());
                *cursorY = defY;
                *cursorX = defX;
                apply_gd_viewport();
                setStatusMessage("gd (python) → " + p.string() + ":" +
                                 std::to_string(defY + 1));
                return;
            }
        }

        setStatusMessage("gd (python): not found");
        return;
    }

    if(isMlangLspEnabled() && isFileType<FileType::Mla>())
    {
        std::string text;
        text.reserve(lines->size() * 80);
        for(size_t i = 0; i < lines->size(); ++i)
        {
            text += (*lines)[i];
            if(i + 1 < lines->size())
                text.push_back('\n');
        }

        mlangLspClient->didChange(currentBuffer->filename, text, "mlang");
        auto loc = mlangLspClient->definition(currentBuffer->filename, *cursorY,
                                              *cursorX);
        if(loc)
        {
            pushJumpLocation();
            openFile(loc->path);
            *cursorY = loc->line;
            *cursorX = loc->character;

            if(*cursorY >= (int)lines->size())
                *cursorY = lines->size() > 0 ? lines->size() - 1 : 0;
            if(*cursorY >= 0 && *cursorX > (int)(*lines)[*cursorY].length())
                *cursorX = (*lines)[*cursorY].length();

            apply_gd_viewport();
            setStatusMessage("gd (mlang) → " + loc->path + ":" +
                             std::to_string(loc->line + 1));
            return;
        }

        if(syntaxHighlighter)
            syntaxHighlighter->ensureMlangTokensLoaded();
        if(mlangTokenCache)
        {
            std::string key = mlangTokenCache->caseInsensitive
                                  ? ascii_lower(symbol)
                                  : symbol;
            auto it = mlangTokenCache->builtinTypes.find(key);
            if(it != mlangTokenCache->builtinTypes.end())
            {
                pushJumpLocation();
                openFile(it->second.path);
                *cursorY = it->second.line;
                *cursorX = 0;
                if(*cursorY >= (int)lines->size())
                    *cursorY = lines->size() > 0 ? lines->size() - 1 : 0;
                apply_gd_viewport();
                setStatusMessage("gd (mlang builtin) → " + it->second.path +
                                 ":" + std::to_string(it->second.line + 1));
                return;
            }

            std::string macroKey = key;
            if(!macroKey.empty() && macroKey.back() == '!')
                macroKey.pop_back();
            auto mit = mlangTokenCache->builtinMacros.find(macroKey);
            if(mit != mlangTokenCache->builtinMacros.end())
            {
                pushJumpLocation();
                openFile(mit->second.path);
                *cursorY = mit->second.line;
                *cursorX = 0;
                if(*cursorY >= (int)lines->size())
                    *cursorY = lines->size() > 0 ? lines->size() - 1 : 0;
                apply_gd_viewport();
                setStatusMessage("gd (mlang macro) → " + mit->second.path +
                                 ":" + std::to_string(mit->second.line + 1));
                return;
            }
        }

        {
            std::string builtinPath;
            int builtinLine = 0;
            if(find_mlang_builtin_type(symbol, builtinPath, builtinLine))
            {
                pushJumpLocation();
                openFile(builtinPath);
                *cursorY = builtinLine;
                *cursorX = 0;
                if(*cursorY >= (int)lines->size())
                    *cursorY = lines->size() > 0 ? lines->size() - 1 : 0;
                apply_gd_viewport();
                setStatusMessage("gd (mlang builtin) → " + builtinPath + ":" +
                                 std::to_string(builtinLine + 1));
                return;
            }
            std::string macroPath;
            int macroLine = 0;
            std::string macroSym = symbol;
            if(!macroSym.empty() && macroSym.back() == '!')
                macroSym.pop_back();
            if(find_mlang_builtin_macro(macroSym, macroPath, macroLine))
            {
                pushJumpLocation();
                openFile(macroPath);
                *cursorY = macroLine;
                *cursorX = 0;
                if(*cursorY >= (int)lines->size())
                    *cursorY = lines->size() > 0 ? lines->size() - 1 : 0;
                apply_gd_viewport();
                setStatusMessage("gd (mlang macro) → " + macroPath + ":" +
                                 std::to_string(macroLine + 1));
                return;
            }
        }

        setStatusMessage("gd (mlang): not found");
        return;
    }

    // Prefer clangd definition when enabled; fallback to heuristic gd
    // otherwise.
    if(isClangdLspEnabled() && isFileType<FileType::Cpp>() &&
       !isFileType<FileType::Mla>())
    {
        // Sync buffer text (full-text change) before querying.
        std::string text;
        text.reserve(lines->size() * 80);
        for(size_t i = 0; i < lines->size(); ++i)
        {
            text += (*lines)[i];
            if(i + 1 < lines->size())
                text.push_back('\n');
        }

        // Open or change in LSP.
        // If this file hasn't been seen yet, didOpen is fine; otherwise
        // didChange updates version. We'll conservatively call didChange after
        // didOpen attempt.
        lspClient->didChange(currentBuffer->filename, text, "cpp");

        // LSP uses UTF-16 positions; lsp_client converts from utf8 byte offset.
        auto loc =
            lspClient->definition(currentBuffer->filename, *cursorY, *cursorX);
        if(loc)
        {
            pushJumpLocation();
            openFile(loc->path);

            // Set cursor position from LSP (both are 0-based)
            *cursorY = loc->line;
            *cursorX = loc->character;

            // Ensure cursor is within valid bounds
            if(*cursorY >= (int)lines->size())
                *cursorY = lines->size() > 0 ? lines->size() - 1 : 0;
            if(*cursorY >= 0 && *cursorX > (int)(*lines)[*cursorY].length())
                *cursorX = (*lines)[*cursorY].length();

            apply_gd_viewport();

            // Show a cleaner message for system headers
            std::string displayPath = loc->path;
            bool isSystemHeader =
                (loc->path.find("/usr/") == 0 || loc->path.find("/opt/") == 0 ||
                 loc->path.find("/Library/") == 0 ||
                 loc->path.find("/Applications/") == 0);
            if(isSystemHeader)
            {
                // Show just the filename for system headers
                size_t lastSlash = loc->path.rfind('/');
                if(lastSlash != std::string::npos)
                    displayPath = "<sys>/" + loc->path.substr(lastSlash + 1);
            }
            setStatusMessage("gd (clangd) → " + displayPath + ":" +
                             std::to_string(loc->line + 1));
            return;
        }
    }
#endif

    // std::symbol fallback: open matching system header when possible
    if(*cursorY >= 0 && *cursorY < (int)lines->size())
    {
        const std::string& line = (*lines)[*cursorY];
        int x = *cursorX;
        if(x >= 0 && x < (int)line.size() && isIdent(line[x]))
        {
            int l = x;
            while(l > 0 && isIdent(line[l - 1]))
                l--;

            if(l >= 5 && line.compare(l - 5, 5, "std::") == 0)
            {
                std::string header = resolveSystemInclude(symbol);
                if(!header.empty())
                {
                    pushJumpLocation();
                    openFile(header);
                    setStatusMessage("gd → <sys>/" + symbol);
                    return;
                }
            }
        }
    }

    pushJumpLocation();

    int y, x;
    std::string current = currentBuffer->filename;
    std::string alternate = findAlternateFile(current);

    // 1️⃣ First: Search for LOCAL variable/parameter declaration (backwards from
    // cursor) This handles local variables and function parameters
    if(searchLocalDefinition(*lines, symbol, *cursorY, *cursorX, y, x))
    {
        // Make sure we're not jumping to ourselves
        if(y != *cursorY || x != *cursorX)
        {
            *cursorY = y;
            *cursorX = x;
            apply_gd_viewport();
            setStatusMessage("gd → local '" + symbol + "' at " +
                             std::to_string(y + 1) + ":" +
                             std::to_string(x + 1));
            return;
        }
    }

    // 2️⃣ Search for member variable in current file (class/struct members)
    if(searchMemberDefinition(*lines, symbol, y, x))
    {
        if(y != *cursorY || x != *cursorX)
        {
            *cursorY = y;
            *cursorX = x;
            apply_gd_viewport();
            setStatusMessage("gd → member '" + symbol + "' at " +
                             std::to_string(y + 1) + ":" +
                             std::to_string(x + 1));
            return;
        }
    }

    // 3️⃣ Search for function definition in alternate file (header <-> source)
    if(!alternate.empty())
    {
        openFile(alternate);

        if(searchDefinitionInBuffer(currentBuffer, symbol, y, x))
        {
            *cursorY = y;
            *cursorX = x;
            apply_gd_viewport();
            setStatusMessage("gd → " + alternate);
            return;
        }

        // Not found → go back
        openFile(current);
    }

    // 4️⃣ Fallback: Search for function definition in current file
    if(searchDefinitionInBuffer(currentBuffer, symbol, y, x))
    {
        *cursorY = y;
        *cursorX = x;
        apply_gd_viewport();
        setStatusMessage("gd (same file)");
        return;
    }

    setStatusMessage("gd: '" + symbol +
                     "' not found (curY=" + std::to_string(*cursorY) +
                     " curX=" + std::to_string(*cursorX) + ")");
}

// adjustViewport and centerScreen are now in cursor_movement.cpp

void Editor::refreshScreen()
{
    syncModeFromStateMachine();

    if(diagnosticPopupActive && (*cursorY != diagnosticPopupCursorY ||
                                 *cursorX != diagnosticPopupCursorX))
    {
        closeDiagnosticPopup();
    }
    if(symbolPopupActive &&
       (*cursorY != symbolPopupCursorY || *cursorX != symbolPopupCursorX))
    {
        closeSymbolPopup();
    }

    if(currentMode == WELCOME)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<WelcomeMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == FILE_BROWSER)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<FileBrowserMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == FUZZY_FIND)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<FuzzyFindMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == BUFFER_BROWSER)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<BufferBrowserMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == GREP_SEARCH)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<GrepSearchMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == REFERENCES)
    {
        drawReferences();
        return;
    }
    if(currentMode == LSP_INFO)
    {
        drawLspInfo();
        return;
    }

    if(currentMode == HELP)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<HelpMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == GIT_SHOW)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<GitShowCommitMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == GIT_LOG)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<GitLogMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(currentMode != INSERT && !showGitBlame)
    {
        if(currentBuffer && isClangdLspEnabled() &&
           isFileType<FileType::Cpp>() && !isFileType<FileType::Mla>() &&
           lspClient && !currentBuffer->filename.empty())
        {
            size_t revision =
                lspClient->diagnosticsRevision(currentBuffer->filename);
            if(!currentBuffer->lspDiagnosticsSeenValid ||
               revision != currentBuffer->lspDiagnosticsSeenRevision)
            {
                currentBuffer->lspDiagnosticsSeenRevision = revision;
                currentBuffer->lspDiagnosticsSeenValid = true;
                needsFullRedraw = true;
            }
        }
        syncClangdDiagnosticsIfNeeded(false);
    }
#endif

    static int lastOffsetY = -1;
    static int lastOffsetX = -1;
    static Mode lastMode = NORMAL;
    static int lastVisualStartY = -1;
    static int lastVisualEndY = -1;
    static int lastCursorY = -1;

    int prevOffsetY = lastOffsetY;
    adjustViewport();

    if(splitActive)
    {
        drawFullScreen();
        lastOffsetY = *offsetY;
        lastOffsetX = *offsetX;
        lastMode = currentMode;
        lastCursorY = *cursorY;
        needsFullRedraw = false;
        return;
    }

    bool scrolled = (*offsetY != lastOffsetY || *offsetX != lastOffsetX);
    bool modeChanged = (currentMode != lastMode);
    int scrollDelta = *offsetY - lastOffsetY;
    bool cursorMoved = (*cursorY != lastCursorY);

    if(showGitBlame && currentBuffer && !currentBuffer->blameValid)
        updateGitBlameForVisibleRange();

    bool visualChanged = false;
    if(currentMode == VISUAL || currentMode == VISUAL_LINE ||
       currentMode == VISUAL_BLOCK)
    {
        visualChanged = (currentBuffer->visualStartY != lastVisualStartY ||
                         currentBuffer->visualEndY != lastVisualEndY);
        lastVisualStartY = currentBuffer->visualStartY;
        lastVisualEndY = currentBuffer->visualEndY;
    }
    else
    {
        lastVisualStartY = -1;
        lastVisualEndY = -1;
    }

    bool isEditingMode =
        (currentMode == INSERT || currentMode == REPLACE ||
         currentMode == COMMAND || currentMode == SEARCH_FORWARD ||
         currentMode == SEARCH_BACKWARD);

    if(modeChanged || needsFullRedraw || *offsetX != lastOffsetX ||
       abs(scrollDelta) > screenRows / 2 || visualChanged ||
       (currentMode == VISUAL || currentMode == VISUAL_LINE ||
        currentMode == VISUAL_BLOCK) ||
       isEditingMode || (showRelativeLineNumbers && cursorMoved))
    {
        drawFullScreen();
    }
    else if(scrollDelta != 0 && abs(scrollDelta) <= 5 && currentMode == NORMAL)
    {
        drawScrollUpdate(scrollDelta);
    }
    else if(scrollDelta == 0 && currentMode == NORMAL)
    {
        drawStatusBarQuick();
        drawMessageBarQuick(); // Add this
        updateCursorPosition();
    }
    else
    {
        drawFullScreen();
    }

    lastOffsetY = *offsetY;
    lastOffsetX = *offsetX;
    lastMode = currentMode;
    lastCursorY = *cursorY;
    needsFullRedraw = false;
}
void Editor::updateCursorPosition()
{
    int cursorRow, cursorCol;

    if(currentMode == COMMAND || currentMode == SEARCH_FORWARD ||
       currentMode == SEARCH_BACKWARD)
    {
        cursorRow = screenRows + 2;
        cursorCol = commandBuffer.length() + 1;
    }
    else
    {
        PaneLayout layout = getPaneLayout(activePane);
        cursorRow = layout.y + (*cursorY - *offsetY) + 1 + tabBarRows();
        if(utf8Mode && *cursorY >= 0 && *cursorY < (int)lines->size())
        {
            const std::string& line = (*lines)[*cursorY];
            int start = std::clamp(*offsetX, 0, (int)line.size());
            int end = std::clamp(*cursorX, 0, (int)line.size());
            if(end < start)
                std::swap(start, end);
            cursorCol = text_utils::utf8DisplayWidth(
                            std::string_view(line).substr(start, end - start)) +
                        1 + gutterWidth() + layout.x;
        }
        else
        {
            cursorCol = layout.x + (*cursorX - *offsetX) + 1 + gutterWidth();
        }
    }

    Terminal::write(Terminal::cursorPos(cursorRow, cursorCol));
    Terminal::flush();

    lastCursorScreenY = cursorRow;
    lastCursorScreenX = cursorCol;
}

void Editor::draw()
{
    refreshScreen();
}

void Editor::setStatusMessage(const std::string& msg)
{
    statusMessage = msg;
}

bool Editor::noteDoubleEscStatusClear()
{
    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastEsc =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastEscTime)
            .count();
    if(timeSinceLastEsc <= DOUBLE_ESC_TIMEOUT_MS)
    {
        setStatusMessage("");
        needsFullRedraw = true;
        lastEscTime = std::chrono::steady_clock::time_point();
        return true;
    }
    lastEscTime = now;
    return false;
}

int Editor::tabBarRows() const
{
    return (showTabs && !buffers.empty()) ? 1 : 0;
}

int Editor::contentRows() const
{
    if(splitActive)
    {
        PaneLayout layout = getPaneLayout(activePane);
        return std::max(1, layout.rows - tabBarRows());
    }
    return std::max(1, screenRows - tabBarRows());
}

Editor::PaneLayout Editor::getPaneLayout(int pane) const
{
    PaneLayout layout;
    layout.x = 0;
    layout.y = 0;
    layout.rows = screenRows;
    layout.cols = screenCols;

    if(!splitActive)
        return layout;

    if(splitVertical)
    {
        if(screenCols < 2)
            return layout;
        int leftCols = screenCols / 2;
        int rightCols = screenCols - leftCols;
        if(rightCols > 1)
            rightCols -= 1; // avoid auto-wrap at last column
        if(pane == 0)
        {
            layout.x = 0;
            layout.cols = leftCols;
        }
        else
        {
            layout.x = leftCols;
            layout.cols = rightCols;
        }
        layout.y = 0;
        layout.rows = screenRows;
    }
    else
    {
        if(screenRows < 2)
            return layout;
        int topRows = screenRows / 2;
        int bottomRows = screenRows - topRows;
        layout.x = 0;
        layout.cols = screenCols;
        if(pane == 0)
        {
            layout.y = 0;
            layout.rows = topRows;
        }
        else
        {
            layout.y = topRows;
            layout.rows = bottomRows;
        }
    }

    layout.rows = std::max(1, layout.rows);
    layout.cols = std::max(1, layout.cols);
    return layout;
}

void Editor::setPanePointers(int pane)
{
    cursorX = &splitPanes[pane].cursorX;
    cursorY = &splitPanes[pane].cursorY;
    wantedX = &splitPanes[pane].wantedX;
    offsetX = &splitPanes[pane].offsetX;
    offsetY = &splitPanes[pane].offsetY;
}

void Editor::enableSplit(bool vertical)
{
    if(!currentBuffer)
    {
        setStatusMessage("No buffer");
        return;
    }
    if(splitActive)
    {
        syncBufferStateFromActivePane();
    }
    splitActive = true;
    splitVertical = vertical;
    activePane = 0;
    splitTabBarOffset[0] = tabBarOffset;
    splitTabBarOffset[1] = tabBarOffset;
    initSplitPanesFromBuffer();
    setPanePointers(activePane);
    needsFullRedraw = true;
}

void Editor::closeSplit()
{
    if(!splitActive)
        return;
    syncBufferStateFromActivePane();
    int paneIndex = activePane;
    splitActive = false;
    currentBufferIndex = splitPanes[paneIndex].bufferIndex;
    tabBarOffset = splitTabBarOffset[paneIndex];
    activePane = 0;
    updateCurrentBufferPointers();
    needsFullRedraw = true;
}

void Editor::switchPane()
{
    if(!splitActive)
        return;
    syncBufferStateFromActivePane();
    splitTabBarOffset[activePane] = tabBarOffset;
    activePane = (activePane == 0) ? 1 : 0;
    tabBarOffset = splitTabBarOffset[activePane];
    currentBufferIndex = splitPanes[activePane].bufferIndex;
    updateCurrentBufferPointers();
    adjustViewport();
    needsFullRedraw = true;
}

void Editor::syncBufferStateFromActivePane()
{
    if(!currentBuffer)
        return;
    currentBuffer->cursorX = splitPanes[activePane].cursorX;
    currentBuffer->cursorY = splitPanes[activePane].cursorY;
    currentBuffer->wantedX = splitPanes[activePane].wantedX;
    currentBuffer->offsetX = splitPanes[activePane].offsetX;
    currentBuffer->offsetY = splitPanes[activePane].offsetY;
}

void Editor::initSplitPanesFromBuffer()
{
    if(!currentBuffer)
        return;
    PaneState state;
    state.bufferIndex = currentBufferIndex;
    state.cursorX = currentBuffer->cursorX;
    state.cursorY = currentBuffer->cursorY;
    state.wantedX = currentBuffer->wantedX;
    state.offsetX = currentBuffer->offsetX;
    state.offsetY = currentBuffer->offsetY;
    splitPanes[0] = state;
    splitPanes[1] = state;
}

void Editor::switchToBufferInActivePane(int index)
{
    if(index < 0 || index >= buffers.size())
        return;
    syncBufferStateFromActivePane();
    splitTabBarOffset[activePane] = tabBarOffset;
    tabBarOffset = splitTabBarOffset[activePane];
    splitPanes[activePane].bufferIndex = index;
    currentBufferIndex = index;
    updateCurrentBufferPointers();
    restoreBufferState();
    needsFullRedraw = true;
}

bool Editor::canSplit() const
{
    if(!splitActive)
        return false;
    if(splitVertical)
        return screenCols >= 2;
    return screenRows >= 2;
}

int Editor::lineNumberWidth() const
{
    if(!showRelativeLineNumbers)
        return 0;
    int maxLine = 1;
    if(!buffers.empty())
    {
        for(const auto& buf : buffers)
        {
            int count = (int)buf->lines.size();
            if(count > maxLine)
                maxLine = count;
        }
    }
    if(maxLine > maxLineCountSeen)
        maxLineCountSeen = maxLine;
    return (int)std::to_string(maxLineCountSeen).length();
}

int Editor::gutterWidth() const
{
    int width = showGitBlame ? kGitBlameWidth : kDiagnosticGutterWidth;
    int numbers = lineNumberWidth();
    if(numbers > 0)
        width += numbers + 1; // add space after line number
    return width;
}

void Editor::toggleGitBlame()
{
    if(gitHandler)
        gitHandler->toggleGitBlame();
}

void Editor::updateGitBlameForVisibleRange()
{
    if(gitHandler)
        gitHandler->updateGitBlameForVisibleRange();
}

std::string Editor::blameDisplayForLine(int row) const
{
    if(gitHandler)
        return gitHandler->blameDisplayForLine(row);
    return "";
}

std::string Editor::blameFullForLine(int row) const
{
    if(gitHandler)
        return gitHandler->blameFullForLine(row);
    return "";
}

void Editor::openGitShowCommitMode()
{
    if(gitHandler)
        gitHandler->openGitShowCommitMode();
}

std::vector<std::string> Editor::loadGitShowLines(const std::string& hash)
{
    if(gitHandler)
        return gitHandler->loadGitShowLines(hash);
    return {};
}

void Editor::openGitLogMode()
{
    if(gitHandler)
        gitHandler->openGitLogMode();
}

void Editor::openGitLogModeForFile()
{
    if(gitHandler)
        gitHandler->openGitLogModeForFile();
}

static std::optional<int> parseIndentWidthLine(const std::string& line)
{
    size_t start = 0;
    while(start < line.size() && (line[start] == ' ' || line[start] == '\t'))
        start++;
    if(start >= line.size() || line[start] == '#')
        return std::nullopt;

    constexpr std::string_view key = "IndentWidth";
    if(line.compare(start, key.size(), key) != 0)
        return std::nullopt;
    size_t pos = start + key.size();
    while(pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
        pos++;
    if(pos >= line.size() || line[pos] != ':')
        return std::nullopt;
    pos++;
    while(pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
        pos++;
    if(pos >= line.size())
        return std::nullopt;

    size_t end = pos;
    while(end < line.size() && std::isdigit((unsigned char)line[end]))
        end++;
    if(end == pos)
        return std::nullopt;

    try
    {
        int value = std::stoi(line.substr(pos, end - pos));
        if(value > 0)
            return value;
    }
    catch(...)
    {
    }
    return std::nullopt;
}

static std::optional<std::string> parseScalarValueLine(const std::string& line,
                                                       std::string_view key)
{
    size_t start = 0;
    while(start < line.size() && (line[start] == ' ' || line[start] == '\t'))
        start++;
    if(start >= line.size() || line[start] == '#')
        return std::nullopt;

    if(line.compare(start, key.size(), key) != 0)
        return std::nullopt;
    size_t pos = start + key.size();
    while(pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
        pos++;
    if(pos >= line.size() || line[pos] != ':')
        return std::nullopt;
    pos++;
    while(pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
        pos++;
    if(pos >= line.size())
        return std::nullopt;

    std::string value = line.substr(pos);
    while(!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
                             value.back() == '\r' || value.back() == '\n'))
        value.pop_back();
    return value;
}

static bool parseBraceNewLineValue(std::string value)
{
    for(char& c : value)
        c = (char)std::tolower((unsigned char)c);
    if(value == "allman" || value == "whitesmiths" || value == "gnu")
        return true;
    if(value == "attach" || value == "stroustrup" || value == "linux" ||
       value == "webkit")
        return false;
    if(value == "true" || value == "always")
        return true;
    if(value == "false" || value == "never")
        return false;
    return false;
}

void Editor::updateClangFormatIndentWidth()
{
    if(!currentBuffer)
        return;

    currentBuffer->clangIndentWidthValid = true;
    currentBuffer->clangIndentWidth = -1;
    currentBuffer->clangBraceStyleValid = true;
    currentBuffer->clangBraceNewLine = false;

    if(!isFileType<FileType::Cpp>() || !filename || filename->empty())
        return;

    std::filesystem::path path = *filename;
    if(path.is_relative())
        path = std::filesystem::absolute(path);
    if(path.has_parent_path())
        path = path.parent_path();

    std::error_code ec;
    while(true)
    {
        std::filesystem::path clangFormat = path / ".clang-format";
        std::filesystem::path altFormat = path / "_clang-format";
        std::filesystem::path found;

        if(std::filesystem::exists(clangFormat, ec))
            found = clangFormat;
        else if(std::filesystem::exists(altFormat, ec))
            found = altFormat;

        if(!found.empty())
        {
            std::ifstream in(found);
            if(in.is_open())
            {
                std::string line;
                bool inBraceWrapping = false;
                size_t braceWrappingIndent = 0;
                while(std::getline(in, line))
                {
                    std::optional<int> width = parseIndentWidthLine(line);
                    if(width)
                    {
                        currentBuffer->clangIndentWidth = *width;
                    }

                    auto breakValue =
                        parseScalarValueLine(line, "BreakBeforeBraces");
                    if(breakValue)
                    {
                        currentBuffer->clangBraceNewLine =
                            parseBraceNewLineValue(*breakValue);
                        continue;
                    }

                    auto braceWrapping =
                        parseScalarValueLine(line, "BraceWrapping");
                    if(braceWrapping)
                    {
                        inBraceWrapping = true;
                        braceWrappingIndent = line.find_first_not_of(" \t");
                        if(braceWrappingIndent == std::string::npos)
                            braceWrappingIndent = 0;
                        continue;
                    }

                    if(inBraceWrapping)
                    {
                        size_t indent = line.find_first_not_of(" \t");
                        if(indent == std::string::npos)
                            continue;
                        if(indent <= braceWrappingIndent)
                        {
                            inBraceWrapping = false;
                            continue;
                        }

                        auto afterControl =
                            parseScalarValueLine(line, "AfterControlStatement");
                        if(afterControl)
                        {
                            currentBuffer->clangBraceNewLine =
                                parseBraceNewLineValue(*afterControl);
                        }
                    }
                }
            }
            return;
        }

        if(path == path.root_path())
            break;
        path = path.parent_path();
    }
}

int Editor::indentWidthForBraces() const
{
    if(currentBuffer && currentBuffer->clangIndentWidthValid &&
       currentBuffer->clangIndentWidth > 0)
        return currentBuffer->clangIndentWidth;
    return tabSpaces;
}

bool Editor::braceNewLineForAutoBraces() const
{
    if(currentBuffer && currentBuffer->clangBraceStyleValid)
        return currentBuffer->clangBraceNewLine;
    return false;
}

void Editor::commentLines(int startY, int endY)
{
    if(!currentBuffer || !lines)
        return;
    if(!isFileType<FileType::Cpp>() && !isFileType<FileType::Python>())
    {
        setStatusMessage("comment: unsupported filetype");
        return;
    }

    std::string prefix = isFileType<FileType::Python>() ? "#" : "//";
    if(startY > endY)
        std::swap(startY, endY);

    bool allCommented = true;
    bool anyCommented = false;
    for(int y = startY; y <= endY && y < (int)lines->size(); ++y)
    {
        const std::string& line = (*lines)[y];
        size_t pos = line.find_first_not_of(" \t");
        if(pos == std::string::npos)
            continue;
        if(line.compare(pos, prefix.size(), prefix) == 0)
        {
            anyCommented = true;
        }
        else
        {
            allCommented = false;
        }
    }

    if(commentTogglePartial && anyCommented)
        allCommented = true;

    for(int y = startY; y <= endY && y < (int)lines->size(); ++y)
    {
        std::string& line = (*lines)[y];
        size_t pos = line.find_first_not_of(" \t");
        if(pos == std::string::npos)
            continue;

        if(allCommented)
        {
            if(line.compare(pos, prefix.size(), prefix) != 0)
                continue;
            size_t eraseLen = prefix.size();
            if(pos + eraseLen < line.size() && line[pos + eraseLen] == ' ')
                eraseLen++;
            line.erase(pos, eraseLen);
            continue;
        }

        if(line.compare(pos, prefix.size(), prefix) == 0)
            continue;
        line.insert(pos, prefix + " ");
    }

    *dirty = true;
    saveState();
    currentBuffer->lspSyncNeeded = true;
    needsFullRedraw = true;
}

void Editor::syncClangdDiagnosticsIfNeeded(bool force)
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    if(!currentBuffer || !isClangdLspEnabled() ||
       !isFileType<FileType::Cpp>() || !lspClient)
        return;

    bool shouldCheck = force || currentBuffer->lspSyncNeeded || *dirty;
    if(!shouldCheck)
        return;

    auto hashBuffer = [](const std::vector<std::string>& src) -> size_t
    {
        size_t h = 1469598103934665603ull;
        for(const auto& line : src)
        {
            for(unsigned char c : line)
            {
                h ^= c;
                h *= 1099511628211ull;
            }
            h ^= '\n';
            h *= 1099511628211ull;
        }
        return h;
    };

    size_t newHash = hashBuffer(*lines);
    if(force || !currentBuffer->lspHashValid ||
       newHash != currentBuffer->lspContentHash)
    {
        std::string text;
        text.reserve(lines->size() * 80);
        for(size_t i = 0; i < lines->size(); ++i)
        {
            text += (*lines)[i];
            if(i + 1 < lines->size())
                text.push_back('\n');
        }
        lspClient->didChange(currentBuffer->filename, text, "cpp");
        currentBuffer->lspContentHash = newHash;
        currentBuffer->lspHashValid = true;
    }

    if(diagnosticPopupActive)
    {
        std::optional<LspDiagnosticSummary> diag =
            getClangdDiagnosticForLine(diagnosticPopupLine);
        if(!diag || diag->severity <= 0 || diag->severity > 2)
        {
            closeDiagnosticPopup();
        }
        else
        {
            diagnosticPopupData = *diag;
        }
    }

    currentBuffer->lspSyncNeeded = false;
#else
    (void)force;
#endif
}

bool Editor::handleSetCommand(std::string_view cmd)
{
    if(!cmd.starts_with("set "))
        return false;

    std::string opt = std::string(cmd.substr(4));
    if(opt == "autobraces?")
    {
        setStatusMessage(std::string("autobraces=") +
                         (autoBraces ? "true" : "false"));
        return true;
    }
    if(opt == "autoquotes?")
    {
        setStatusMessage(std::string("autoquotes=") +
                         (autoQuotes ? "true" : "false"));
        return true;
    }
    if(opt == "autobracesinstrings?")
    {
        setStatusMessage(std::string("autobracesinstrings=") +
                         (autoBracesInStrings ? "true" : "false"));
        return true;
    }
    if(opt == "autotags?")
    {
        setStatusMessage(std::string("autotags=") +
                         (autoTags ? "true" : "false"));
        return true;
    }
    if(opt == "tabspaces?")
    {
        setStatusMessage("tabspaces=" + std::to_string(tabSpaces));
        return true;
    }
    if(opt == "autocomplete?")
    {
        setStatusMessage(std::string("autocomplete=") +
                         (autoCompletion ? "true" : "false"));
        return true;
    }
    if(opt == "completionautoparens?")
    {
        setStatusMessage(std::string("completionautoparens=") +
                         (completionAutoParens ? "true" : "false"));
        return true;
    }
    if(opt == "showtabs?")
    {
        setStatusMessage(std::string("showtabs=") +
                         (showTabs ? "true" : "false"));
        return true;
    }
    if(opt == "utf8?")
    {
        setStatusMessage(std::string("utf8=") + (utf8Mode ? "true" : "false"));
        return true;
    }
    if(opt == "gitblameinfo?")
    {
        setStatusMessage(std::string("gitblameinfo=") +
                         (showGitBlameInfo ? "true" : "false"));
        return true;
    }
    if(opt == "gitdefaultcolors?")
    {
        setStatusMessage(std::string("gitdefaultcolors=") +
                         (gitUseDefaultColors ? "true" : "false"));
        return true;
    }
    if(opt == "commenttogglepartial?")
    {
        setStatusMessage(std::string("commenttogglepartial=") +
                         (commentTogglePartial ? "true" : "false"));
        return true;
    }
    if(opt == "formatoninsertleave?")
    {
        setStatusMessage(std::string("formatoninsertleave=") +
                         (formatOnInsertLeave ? "true" : "false"));
        return true;
    }
    if(opt == "formatonsave?")
    {
        setStatusMessage(std::string("formatonsave=") +
                         (formatOnSave ? "true" : "false"));
        return true;
    }
    if(opt == "gdcenter?")
    {
        setStatusMessage(std::string("gdcenter=") +
                         (gdCenterScreen ? "true" : "false"));
        return true;
    }
    if(opt == "formatondoubleesctimeoutms?")
    {
        setStatusMessage("formatondoubleesctimeoutms=" +
                         std::to_string(formatOnDoubleEscTimeoutMs));
        return true;
    }
    if(opt == "python.formatter?")
    {
        setStatusMessage("python.formatter=" + pythonFormatter);
        return true;
    }
    if(opt == "pyfmt?")
    {
        setStatusMessage("python.formatter=" + pythonFormatter);
        return true;
    }
    if(opt == "syntax.cpp.highlight_system_includes?")
    {
        setStatusMessage(std::string("syntax.cpp.highlight_system_includes=") +
                         (syntaxCppHighlightSystemIncludes ? "true" : "false"));
        return true;
    }
    if(opt == "syntax.cpp.highlight_param_types?")
    {
        setStatusMessage(std::string("syntax.cpp.highlight_param_types=") +
                         (syntaxCppHighlightParamTypes ? "true" : "false"));
        return true;
    }

    auto set_flag = [&](bool value)
    {
        autoBraces = value;
        setStatusMessage(std::string("autobraces=") +
                         (autoBraces ? "true" : "false"));
    };

    auto set_auto_quotes = [&](bool value)
    {
        autoQuotes = value;
        setStatusMessage(std::string("autoquotes=") +
                         (autoQuotes ? "true" : "false"));
    };

    auto set_auto_braces_in_strings = [&](bool value)
    {
        autoBracesInStrings = value;
        setStatusMessage(std::string("autobracesinstrings=") +
                         (autoBracesInStrings ? "true" : "false"));
    };

    auto set_autotags = [&](bool value)
    {
        autoTags = value;
        setStatusMessage(std::string("autotags=") +
                         (autoTags ? "true" : "false"));
    };

    auto set_gdcenter = [&](bool value)
    {
        gdCenterScreen = value;
        setStatusMessage(std::string("gdcenter=") +
                         (gdCenterScreen ? "true" : "false"));
    };

    if(opt == "autobraces")
    {
        set_flag(true);
        return true;
    }
    if(opt == "noautobraces")
    {
        set_flag(false);
        return true;
    }
    if(opt == "autoquotes")
    {
        set_auto_quotes(true);
        return true;
    }
    if(opt == "noautoquotes")
    {
        set_auto_quotes(false);
        return true;
    }
    if(opt == "autobracesinstrings")
    {
        set_auto_braces_in_strings(true);
        return true;
    }
    if(opt == "noautobracesinstrings")
    {
        set_auto_braces_in_strings(false);
        return true;
    }
    if(opt == "syntax.cpp.highlight_system_includes")
    {
        syntaxCppHighlightSystemIncludes = true;
        setStatusMessage("syntax.cpp.highlight_system_includes=true");
        return true;
    }
    if(opt == "nosyntax.cpp.highlight_system_includes")
    {
        syntaxCppHighlightSystemIncludes = false;
        setStatusMessage("syntax.cpp.highlight_system_includes=false");
        return true;
    }
    if(opt == "syntax.cpp.highlight_param_types")
    {
        syntaxCppHighlightParamTypes = true;
        setStatusMessage("syntax.cpp.highlight_param_types=true");
        return true;
    }
    if(opt == "nosyntax.cpp.highlight_param_types")
    {
        syntaxCppHighlightParamTypes = false;
        setStatusMessage("syntax.cpp.highlight_param_types=false");
        return true;
    }
    if(opt.rfind("python.formatter=", 0) == 0 || opt.rfind("pyfmt=", 0) == 0)
    {
        std::string value = opt.substr(opt.find('=') + 1);
        std::string v = ascii_lower(value);
        if(v == "black" || v == "ruff")
        {
            pythonFormatter = v;
            setStatusMessage("python.formatter=" + pythonFormatter);
        }
        else
        {
            setStatusMessage("python.formatter: expected black|ruff");
        }
        return true;
    }
    if(opt == "autotags")
    {
        set_autotags(true);
        return true;
    }
    if(opt == "noautotags")
    {
        set_autotags(false);
        return true;
    }
    if(opt == "gitblameinfo")
    {
        showGitBlameInfo = true;
        setStatusMessage("gitblameinfo=true");
        return true;
    }
    if(opt == "nogitblameinfo" || opt == "disablegitblame")
    {
        showGitBlameInfo = false;
        setStatusMessage("gitblameinfo=false");
        return true;
    }
    if(opt == "enablegitdefaultcolors")
    {
        gitUseDefaultColors = true;
        setStatusMessage("gitdefaultcolors=true");
        return true;
    }
    if(opt == "disablegitdefaultcolors")
    {
        gitUseDefaultColors = false;
        setStatusMessage("gitdefaultcolors=false");
        return true;
    }
    if(opt == "commenttogglepartial")
    {
        commentTogglePartial = true;
        setStatusMessage("commenttogglepartial=true");
        return true;
    }
    if(opt == "nocommenttogglepartial")
    {
        commentTogglePartial = false;
        setStatusMessage("commenttogglepartial=false");
        return true;
    }
    if(opt == "formatoninsertleave")
    {
        formatOnInsertLeave = true;
        setStatusMessage("formatoninsertleave=true");
        return true;
    }
    if(opt == "gdcenter")
    {
        set_gdcenter(true);
        return true;
    }
    if(opt == "nogdcenter")
    {
        set_gdcenter(false);
        return true;
    }
    if(opt == "noformatoninsertleave")
    {
        formatOnInsertLeave = false;
        setStatusMessage("formatoninsertleave=false");
        return true;
    }
    if(opt == "formatonsave")
    {
        formatOnSave = true;
        setStatusMessage("formatonsave=true");
        return true;
    }
    if(opt == "noformatonsave")
    {
        formatOnSave = false;
        setStatusMessage("formatonsave=false");
        return true;
    }
    if(opt.rfind("formatondoubleesctimeoutms=", 0) == 0)
    {
        std::string value =
            opt.substr(std::string("formatondoubleesctimeoutms=").length());
        try
        {
            int ms = std::stoi(value);
            if(ms > 0 && ms <= 5000)
            {
                formatOnDoubleEscTimeoutMs = ms;
                setStatusMessage("formatondoubleesctimeoutms=" +
                                 std::to_string(formatOnDoubleEscTimeoutMs));
            }
            else
            {
                setStatusMessage("formatondoubleesctimeoutms: expected 1-5000");
            }
        }
        catch(...)
        {
            setStatusMessage("formatondoubleesctimeoutms: expected number");
        }
        return true;
    }
    if(opt.rfind("formatonsave=", 0) == 0)
    {
        std::string value = opt.substr(std::string("formatonsave=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            formatOnSave = true;
            setStatusMessage("formatonsave=true");
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            formatOnSave = false;
            setStatusMessage("formatonsave=false");
        }
        else
        {
            setStatusMessage("formatonsave: expected true/false");
        }
        return true;
    }
    if(opt.rfind("gdcenter=", 0) == 0)
    {
        std::string value = opt.substr(std::string("gdcenter=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            set_gdcenter(true);
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            set_gdcenter(false);
        }
        else
        {
            setStatusMessage("gdcenter: expected true/false");
        }
        return true;
    }
    if(opt.rfind("autobraces=", 0) == 0)
    {
        std::string value = opt.substr(std::string("autobraces=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            set_flag(true);
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            set_flag(false);
        }
        else
        {
            setStatusMessage("autobraces: expected true/false");
        }
        return true;
    }
    if(opt.rfind("autoquotes=", 0) == 0)
    {
        std::string value = opt.substr(std::string("autoquotes=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            set_auto_quotes(true);
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            set_auto_quotes(false);
        }
        else
        {
            setStatusMessage("autoquotes: expected true/false");
        }
        return true;
    }
    if(opt.rfind("autobracesinstrings=", 0) == 0)
    {
        std::string value =
            opt.substr(std::string("autobracesinstrings=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            set_auto_braces_in_strings(true);
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            set_auto_braces_in_strings(false);
        }
        else
        {
            setStatusMessage("autobracesinstrings: expected true/false");
        }
        return true;
    }
    if(opt.rfind("autotags=", 0) == 0)
    {
        std::string value = opt.substr(std::string("autotags=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            set_autotags(true);
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            set_autotags(false);
        }
        else
        {
            setStatusMessage("autotags: expected true/false");
        }
        return true;
    }

    auto set_auto_completion = [&](bool value)
    {
        autoCompletion = value;
        setStatusMessage(std::string("autocomplete=") +
                         (autoCompletion ? "true" : "false"));
    };

    auto set_completion_auto_parens = [&](bool value)
    {
        completionAutoParens = value;
        setStatusMessage(std::string("completionautoparens=") +
                         (completionAutoParens ? "true" : "false"));
    };

    if(opt == "autocomplete")
    {
        set_auto_completion(true);
        return true;
    }
    if(opt == "noautocomplete")
    {
        set_auto_completion(false);
        return true;
    }
    if(opt == "completionautoparens")
    {
        set_completion_auto_parens(true);
        return true;
    }
    if(opt == "nocompletionautoparens")
    {
        set_completion_auto_parens(false);
        return true;
    }
    if(opt == "showtabs")
    {
        showTabs = true;
        tabBarOffset = 0;
        setStatusMessage("showtabs=true");
        needsFullRedraw = true;
        return true;
    }
    if(opt == "noshowtabs")
    {
        showTabs = false;
        tabBarOffset = 0;
        setStatusMessage("showtabs=false");
        needsFullRedraw = true;
        return true;
    }
    if(opt == "utf8")
    {
        utf8Mode = true;
        setStatusMessage("utf8=true");
        needsFullRedraw = true;
        return true;
    }
    if(opt == "noutf8")
    {
        utf8Mode = false;
        setStatusMessage("utf8=false");
        needsFullRedraw = true;
        return true;
    }
    if(opt.rfind("autocomplete=", 0) == 0)
    {
        std::string value = opt.substr(std::string("autocomplete=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            set_auto_completion(true);
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            set_auto_completion(false);
        }
        else
        {
            setStatusMessage("autocomplete: expected true/false");
        }
        return true;
    }
    if(opt.rfind("completionautoparens=", 0) == 0)
    {
        std::string value =
            opt.substr(std::string("completionautoparens=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            set_completion_auto_parens(true);
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            set_completion_auto_parens(false);
        }
        else
        {
            setStatusMessage("completionautoparens: expected true/false");
        }
        return true;
    }
    if(opt.rfind("showtabs=", 0) == 0)
    {
        std::string value = opt.substr(std::string("showtabs=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            showTabs = true;
            tabBarOffset = 0;
            setStatusMessage("showtabs=true");
            needsFullRedraw = true;
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            showTabs = false;
            tabBarOffset = 0;
            setStatusMessage("showtabs=false");
            needsFullRedraw = true;
        }
        else
        {
            setStatusMessage("showtabs: expected true/false");
        }
        return true;
    }
    if(opt.rfind("utf8=", 0) == 0)
    {
        std::string value = opt.substr(std::string("utf8=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            utf8Mode = true;
            setStatusMessage("utf8=true");
            needsFullRedraw = true;
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            utf8Mode = false;
            setStatusMessage("utf8=false");
            needsFullRedraw = true;
        }
        else
        {
            setStatusMessage("utf8: expected true/false");
        }
        return true;
    }

    if(opt.rfind("tabspaces=", 0) == 0)
    {
        std::string value =
            std::string(opt.substr(std::string("tabspaces=").length()));
        try
        {
            int v = std::stoi(value);
            if(v >= 1 && v <= 16)
            {
                tabSpaces = v;
                setStatusMessage("tabspaces=" + std::to_string(tabSpaces));
            }
            else
            {
                setStatusMessage("tabspaces: expected 1-16");
            }
        }
        catch(...)
        {
            setStatusMessage("tabspaces: expected number");
        }
        return true;
    }

    setStatusMessage("Unknown option: " + opt);
    return true;
}

void Editor::handleResize()
{
#if defined(UVIM_TERMINAL_POSIX)
    if(!g_pending_resize)
        return;
    g_pending_resize = 0;

    int rows = 0;
    int cols = 0;
    Terminal::getWindowSize(rows, cols);
    screenRows = std::max(1, rows - 2);
    screenCols = std::max(1, cols);
    needsFullRedraw = true;
#endif
}

// Command execution
void Editor::executeCommand(std::string_view cmd)
{
    if(!cmd.empty())
    {
        if(commandHistory.empty() || commandHistory.back() != cmd)
            commandHistory.push_back(std::string(cmd));
        commandHistoryIndex = -1;
    }

    if(!cmd.empty() && (cmd.front() == '/' || cmd.front() == '?'))
    {
        if(!hasBuffer())
        {
            setStatusMessage("No buffer");
            return;
        }
        std::string query = std::string(cmd.substr(1));
        if(!query.empty())
        {
            addSearchToHistory(query);
            savedCursorX = *cursorX;
            savedCursorY = *cursorY;
            performSearch(query, cmd.front() == '/');
        }
        return;
    }

    if(handleSetCommand(cmd))
        return;

    if(cmd == "lspinfo")
    {
        showLspInfo();
        commandRequestedModeSet = true;
        commandRequestedMode = LSP_INFO;
        return;
    }
    if(cmd == "format" || cmd == "fmt")
    {
        if(isFileType<FileType::Python>())
        {
            pythonFormatBuffer();
            return;
        }
        if(isFileType<FileType::Mla>())
        {
            mlangFormatBuffer();
            return;
        }
        if(isFileType<FileType::Cpp>() ||
           (filename && !filename->empty() && isHeaderFile(*filename)))
        {
            clangFormatWithArgs("", "clang-format: formatted file");
            return;
        }
        setStatusMessage("format: unsupported file type");
        return;
    }
    if(cmd == "emoji" || cmd == "em")
    {
        openEmojiPopup();
        return;
    }

    if(cmd == "help" || cmd.rfind("help ", 0) == 0 || cmd.rfind("h ", 0) == 0)
    {
        std::string topic;
        if(cmd == "help" || cmd == "h")
        {
            topic = ""; // Default help
        }
        else if(cmd.rfind("help ", 0) == 0)
        {
            topic = std::string(cmd.substr(5));
        }
        else if(cmd.rfind("h ", 0) == 0)
        {
            topic = std::string(cmd.substr(2));
        }

        commandRequestedModeSet = true;
        commandRequestedMode = HELP;
        commandRequestedPath = topic; // Reuse path field for topic
        return;
    }

    if(!hasBuffer())
    {
        if(cmd == "q" || cmd == "q!" || cmd == "qa" || cmd == "qa!" ||
           cmd == "qall" || cmd == "qall!")
        {
            Terminal::clearScreen();
            exit(0);
        }
        if(cmd == "pwd")
        {
            char cwd[PATH_MAX];
            if(getcwd(cwd, sizeof(cwd)))
                setStatusMessage(std::string(cwd));
            else
                setStatusMessage("Error getting current directory");
            return;
        }
        if(cmd.rfind("cd ", 0) == 0 || cmd == "cd")
        {
            std::string path =
                (cmd.length() > 3) ? std::string(cmd.substr(3)) : "";
            if(path.empty())
            {
                const char* home = getenv("HOME");
                if(home)
                    path = home;
                else
                    path = "/";
            }
            if(!path.empty() && path[0] == '~')
            {
                const char* home = getenv("HOME");
                if(home)
                    path = std::string(home) + path.substr(1);
            }
            if(chdir(path.c_str()) == 0)
            {
                char cwd[PATH_MAX];
                if(getcwd(cwd, sizeof(cwd)))
                    setStatusMessage(std::string(cwd));
            }
            else
            {
                setStatusMessage("Cannot change to: " + path);
            }
            return;
        }
        if(cmd == "Ex" || cmd == "ex" || cmd == "E" || cmd == "e ." ||
           cmd == "Explore" || cmd == "explore")
        {
            commandRequestedModeSet = true;
            commandRequestedMode = FILE_BROWSER;
            commandRequestedPath = ".";
            return;
        }
        if(cmd == "Sex" || cmd == "Sexplore" || cmd == "Vex" ||
           cmd == "Vexplore")
        {
            setStatusMessage("Split explorer not yet implemented");
            commandRequestedModeSet = true;
            commandRequestedMode = FILE_BROWSER;
            commandRequestedPath = ".";
            return;
        }
        if(cmd == "ls" || cmd == "buffers" || cmd == "bn" || cmd == "bnext" ||
           cmd == "bp" || cmd == "bprev" || cmd == "bprevious" || cmd == "bd" ||
           cmd == "bdelete")
        {
            setStatusMessage("No buffers");
            return;
        }
        if(cmd == "enew")
        {
            createNewBuffer();
            setStatusMessage("New buffer created");
            return;
        }
        if(cmd.rfind("e ", 0) == 0 || cmd.rfind("edit ", 0) == 0)
        {
            std::string path = (cmd.rfind("e ", 0) == 0)
                                   ? std::string(cmd.substr(2))
                                   : std::string(cmd.substr(5));

            if(path == ".")
            {
                commandRequestedModeSet = true;
                commandRequestedMode = FILE_BROWSER;
                commandRequestedPath = ".";
                return;
            }
            else
            {
                struct stat fileStat;
                if(stat(path.c_str(), &fileStat) == 0 &&
                   S_ISDIR(fileStat.st_mode))
                {
                    commandRequestedModeSet = true;
                    commandRequestedMode = FILE_BROWSER;
                    commandRequestedPath = path;
                    return;
                }
                openFile(path);
                setMode(NORMAL);
                return;
            }
        }
        if(cmd.rfind("tabnew", 0) == 0 || cmd.rfind("tabe ", 0) == 0)
        {
            std::string fname = "";
            if(cmd.rfind("tabe ", 0) == 0 && cmd.length() > 5)
            {
                fname = std::string(cmd.substr(5));
            }
            else if(cmd.rfind("tabnew ", 0) == 0 && cmd.length() > 7)
            {
                fname = std::string(cmd.substr(7));
            }

            if(!fname.empty())
            {
                openFile(fname);
            }
            else
            {
                createNewBuffer();
                setStatusMessage("New buffer created");
            }
            return;
        }

        setStatusMessage("No buffer");
        return;
    }

    auto saveAllBuffers = [&](bool forceExit) -> bool
    {
        int savedCount = 0;
        int skippedNoName = 0;
        int currentBuf = currentBufferIndex;

        for(size_t i = 0; i < buffers.size(); i++)
        {
            if(buffers[i]->dirty)
            {
                if(buffers[i]->filename.empty())
                {
                    skippedNoName++;
                    continue;
                }
                switchToBuffer(i);
                saveFile();
                savedCount++;
            }
        }

        switchToBuffer(currentBuf);

        if(!forceExit)
        {
            if(skippedNoName > 0)
            {
                setStatusMessage("Saved " + std::to_string(savedCount) +
                                 " buffer(s), " +
                                 std::to_string(skippedNoName) + " unnamed");
            }
            else
            {
                setStatusMessage("Saved " + std::to_string(savedCount) +
                                 " buffer(s)");
            }
        }

        return skippedNoName == 0;
    };

    // Buffer commands
    if(cmd == "bn" || cmd == "bnext")
    {
        nextBuffer();
    }
    else if(cmd == "bp" || cmd == "bprev" || cmd == "bprevious")
    {
        previousBuffer();
    }
    else if(cmd == "bd" || cmd == "bdelete")
    {
        closeCurrentBuffer();
    }
    else if(cmd == "bd!")
    {
        *dirty = false;
        closeCurrentBuffer();
    }
    else if(cmd == "ls" || cmd == "buffers")
    {
        listBuffers();
    }
    else if(cmd.rfind("b ", 0) == 0 || cmd.rfind("buffer ", 0) == 0)
    {
        std::string_view arg =
            (cmd.rfind("b ", 0) == 0) ? cmd.substr(2) : cmd.substr(7);
        arg = trim_view(arg);

        int bufNum = 0;
        if(parse_int(arg, bufNum))
        {
            bufNum -= 1;
            if(bufNum >= 0 && bufNum < (int)buffers.size())
            {
                switchToBuffer(bufNum);
            }
            else
            {
                setStatusMessage("Buffer " + std::string(arg) +
                                 " does not exist");
            }
        }
        else
        {
            std::string needle(arg);
            for(size_t i = 0; i < buffers.size(); i++)
            {
                if(buffers[i]->filename.find(needle) != std::string::npos)
                {
                    switchToBuffer(i);
                    return;
                }
            }
            setStatusMessage("No matching buffer for " + needle);
        }
    }
    else if(cmd == "enew")
    {
        createNewBuffer();
        setStatusMessage("New buffer created");
    }
    else if(cmd == "wall" || cmd == "wa")
    {
        saveAllBuffers(false);
    }
    else if(cmd == "wa!")
    {
        saveAllBuffers(false);
    }
    else if(cmd == "qall" || cmd == "qa")
    {
        bool hasUnsaved = false;
        for(const auto& buf : buffers)
        {
            if(buf->dirty)
            {
                hasUnsaved = true;
                break;
            }
        }

        if(hasUnsaved)
        {
            setStatusMessage(
                "Some buffers have unsaved changes (add ! to override)");
        }
        else
        {
            Terminal::clearScreen();
            exit(0);
        }
    }
    else if(cmd == "qall!" || cmd == "qa!")
    {
        Terminal::clearScreen();
        exit(0);
    }
    else if(cmd == "qw" || cmd == "wqall" || cmd == "wqa" || cmd == "xa")
    {
        if(!saveAllBuffers(false))
        {
            setStatusMessage("Some buffers have no name (use :qw! to force)");
            return;
        }
        Terminal::clearScreen();
        exit(0);
    }
    // File browser commands
    else if(cmd == "Ex" || cmd == "ex" || cmd == "E" || cmd == "e ." ||
            cmd == "Explore" || cmd == "explore")
    {
        std::string dir = ".";
        if(!filename->empty())
        {
            size_t lastSlash = filename->find_last_of("/");
            if(lastSlash != std::string::npos)
            {
                dir = filename->substr(0, lastSlash);
                if(dir.empty())
                    dir = "/";
            }
        }
        commandRequestedModeSet = true;
        commandRequestedMode = FILE_BROWSER;
        commandRequestedPath = dir;
        return;
    }
    else if(cmd == "Sex" || cmd == "Sexplore" || cmd == "Vex" ||
            cmd == "Vexplore")
    {
        setStatusMessage("Split explorer not yet implemented");
        commandRequestedModeSet = true;
        commandRequestedMode = FILE_BROWSER;
        commandRequestedPath = ".";
        return;
    }
    // Standard commands
    else if(cmd == "w")
    {
        saveFile();
    }
    else if(cmd == "vs" || cmd == "vsplit")
    {
        enableSplit(true);
    }
    else if(cmd == "vh" || cmd == "hs" || cmd == "hsplit")
    {
        enableSplit(false);
    }
    else if(cmd == "q")
    {
        if(splitActive)
        {
            closeSplit();
            return;
        }
        bool anyDirty = false;
        for(const auto& buf : buffers)
        {
            if(buf->dirty)
            {
                anyDirty = true;
                break;
            }
        }
        if(anyDirty)
        {
            setStatusMessage("No write since last change (add ! to override)");
        }
        else
        {
            Terminal::clearScreen();
            exit(0);
        }
    }
    else if(cmd == "q!")
    {
        Terminal::clearScreen();
        exit(0);
    }
    else if(cmd == "qw!" || cmd == "wqall!" || cmd == "wqa!")
    {
        saveAllBuffers(true);
        Terminal::clearScreen();
        exit(0);
    }
    else if(cmd == "wq" || cmd == "x")
    {
        saveFile();
        if(buffers.size() > 1)
        {
            closeCurrentBuffer();
        }
        else
        {
            Terminal::clearScreen();
            exit(0);
        }
    }
    else if(cmd.rfind("w ", 0) == 0)
    {
        *filename = std::string(cmd.substr(2));
        saveFile();
    }
    else if(cmd.rfind("e ", 0) == 0 || cmd.rfind("edit ", 0) == 0)
    {
        std::string path = (cmd.rfind("e ", 0) == 0)
                               ? std::string(cmd.substr(2))
                               : std::string(cmd.substr(5));

        if(path == ".")
        {
            commandRequestedModeSet = true;
            commandRequestedMode = FILE_BROWSER;
            commandRequestedPath = ".";
            return;
        }
        else
        {
            struct stat fileStat;
            if(stat(path.c_str(), &fileStat) == 0 && S_ISDIR(fileStat.st_mode))
            {
                commandRequestedModeSet = true;
                commandRequestedMode = FILE_BROWSER;
                commandRequestedPath = path;
                return;
            }
            openFile(path);
            setMode(NORMAL);
        }
    }
    else if(cmd.rfind("tabnew", 0) == 0 || cmd.rfind("tabe ", 0) == 0)
    {
        std::string fname = "";
        if(cmd.rfind("tabe ", 0) == 0 && cmd.length() > 5)
        {
            fname = std::string(cmd.substr(5));
        }
        else if(cmd.rfind("tabnew ", 0) == 0 && cmd.length() > 7)
        {
            fname = std::string(cmd.substr(7));
        }

        if(!fname.empty())
        {
            openFile(fname);
        }
        else
        {
            createNewBuffer();
            setStatusMessage("New buffer created");
        }
    }
    else if(cmd == "tabn" || cmd == "tabnext")
    {
        nextBuffer();
    }
    else if(cmd == "tabp" || cmd == "tabprev")
    {
        previousBuffer();
    }
    else if(cmd == "pwd")
    {
        char cwd[PATH_MAX];
        if(getcwd(cwd, sizeof(cwd)))
        {
            setStatusMessage(std::string(cwd));
        }
        else
        {
            setStatusMessage("Error getting current directory");
        }
    }
    else if(cmd.rfind("cd ", 0) == 0 || cmd == "cd")
    {
        std::string path = (cmd.length() > 3) ? std::string(cmd.substr(3)) : "";
        if(path.empty())
        {
            // cd with no args goes to home directory
            const char* home = getenv("HOME");
            if(home)
                path = home;
            else
                path = "/";
        }

        // Expand ~ to home directory
        if(!path.empty() && path[0] == '~')
        {
            const char* home = getenv("HOME");
            if(home)
                path = std::string(home) + path.substr(1);
        }

        if(chdir(path.c_str()) == 0)
        {
            char cwd[PATH_MAX];
            if(getcwd(cwd, sizeof(cwd)))
                setStatusMessage(std::string(cwd));
        }
        else
        {
            setStatusMessage("Cannot change to: " + path);
        }
    }
    else
    {
        int line = 0;
        if(parse_int(cmd, line))
        {
            moveToLine(line - 1);
        }
        else
        {
            setStatusMessage("Not an editor command: " + std::string(cmd));
        }
    }
}

void Editor::forceQuit()
{
    Terminal::restoreTerminal();
    std::exit(0);
}

// ----- clangd completion popup helpers -----

static bool isIdentChar(char c)
{
    return std::isalnum((unsigned char)c) || c == '_';
}

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

std::string Editor::getAlternateFilePath()
{
    if(!currentBuffer || currentBuffer->filename.empty())
        return "";

    return findAlternateFile(currentBuffer->filename);
}

// Helper function for command-line path completion
static std::vector<std::string> getPathCompletions(std::string_view partial)
{
    std::vector<std::string> completions;

    std::string dirPath;
    std::string prefix;

    // Handle ~ expansion
    std::string expandedPartial(partial);
    if(!expandedPartial.empty() && expandedPartial[0] == '~')
    {
        const char* home = getenv("HOME");
        if(home)
            expandedPartial = std::string(home) + expandedPartial.substr(1);
    }

    size_t lastSlash = expandedPartial.find_last_of('/');
    if(lastSlash != std::string::npos)
    {
        dirPath = expandedPartial.substr(0, lastSlash);
        if(dirPath.empty())
            dirPath = "/";
        prefix = expandedPartial.substr(lastSlash + 1);
    }
    else
    {
        dirPath = ".";
        prefix = expandedPartial;
    }

    DIR* dir = opendir(dirPath.c_str());
    if(!dir)
        return completions;

    struct dirent* entry;
    while((entry = readdir(dir)) != nullptr)
    {
        std::string name = entry->d_name;

        // Skip . and ..
        if(name == "." || name == "..")
            continue;

        // Skip hidden files unless prefix starts with .
        if(name[0] == '.' && (prefix.empty() || prefix[0] != '.'))
            continue;

        // Check if name starts with prefix
        if(prefix.empty() || name.substr(0, prefix.length()) == prefix)
        {
            std::string fullPath;
            if(lastSlash != std::string::npos)
            {
                // Keep original path format (with ~ if used)
                if(!partial.empty() && partial[0] == '~')
                {
                    size_t origSlash = partial.find_last_of('/');
                    fullPath =
                        std::string(partial.substr(0, origSlash + 1)) + name;
                }
                else
                {
                    fullPath = dirPath + "/" + name;
                }
            }
            else
            {
                fullPath = name;
            }

            // Check if it's a directory and append /
            struct stat st;
            std::string checkPath = dirPath + "/" + name;
            if(stat(checkPath.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            {
                fullPath += "/";
            }

            completions.push_back(fullPath);
        }
    }

    closedir(dir);

    // Sort completions
    std::sort(completions.begin(), completions.end());

    return completions;
}

// Find longest common prefix among completions
static std::string longestCommonPrefix(const std::vector<std::string>& strings)
{
    if(strings.empty())
        return "";
    if(strings.size() == 1)
        return strings[0];

    std::string prefix = strings[0];
    for(size_t i = 1; i < strings.size(); ++i)
    {
        size_t j = 0;
        while(j < prefix.length() && j < strings[i].length() &&
              prefix[j] == strings[i][j])
        {
            ++j;
        }
        prefix = prefix.substr(0, j);
        if(prefix.empty())
            break;
    }
    return prefix;
}

bool Editor::dispatchModeKey(int c)
{
    if(!modeStateMachine)
    {
        return false;
    }

    modeStateMachine->dispatch(c);
    syncModeFromStateMachine();
    ensureBufferForMode(currentMode);
    if(replayingChange && !Terminal::hasBufferedKeys())
        replayingChange = false;
    return true;
}

void Editor::syncModeFromStateMachine()
{
    if(!modeStateMachine)
    {
        return;
    }

    Mode prevMode = currentMode;
    const ModeState& state = modeStateMachine->state();
    if(std::holds_alternative<WelcomeMode>(state))
    {
        currentMode = WELCOME;
    }
    else if(std::holds_alternative<NormalMode>(state))
    {
        currentMode = NORMAL;
    }
    else if(std::holds_alternative<InsertMode>(state))
    {
        currentMode = INSERT;
    }
    else if(std::holds_alternative<ReplaceMode>(state))
    {
        currentMode = REPLACE;
    }
    else if(std::holds_alternative<VisualMode>(state))
    {
        currentMode = VISUAL;
    }
    else if(std::holds_alternative<VisualLineMode>(state))
    {
        currentMode = VISUAL_LINE;
    }
    else if(std::holds_alternative<VisualBlockMode>(state))
    {
        currentMode = VISUAL_BLOCK;
    }
    else if(std::holds_alternative<CommandMode>(state))
    {
        currentMode = COMMAND;
    }
    else if(std::holds_alternative<SearchForwardMode>(state))
    {
        currentMode = SEARCH_FORWARD;
    }
    else if(std::holds_alternative<SearchBackwardMode>(state))
    {
        currentMode = SEARCH_BACKWARD;
    }
    else if(std::holds_alternative<FileBrowserMode>(state))
    {
        currentMode = FILE_BROWSER;
    }
    else if(std::holds_alternative<FuzzyFindMode>(state))
    {
        currentMode = FUZZY_FIND;
    }
    else if(std::holds_alternative<BufferBrowserMode>(state))
    {
        currentMode = BUFFER_BROWSER;
    }
    else if(std::holds_alternative<GrepSearchMode>(state))
    {
        currentMode = GREP_SEARCH;
    }
    else if(std::holds_alternative<OperatorPendingMode>(state))
    {
        currentMode = OP_PENDING;
    }
    else if(std::holds_alternative<ReferencesMode>(state))
    {
        currentMode = REFERENCES;
    }
    else if(std::holds_alternative<LspInfoMode>(state))
    {
        currentMode = LSP_INFO;
    }
    else if(std::holds_alternative<HelpMode>(state))
    {
        currentMode = HELP;
    }
    else if(std::holds_alternative<GitShowCommitMode>(state))
    {
        currentMode = GIT_SHOW;
    }
    else if(std::holds_alternative<GitLogMode>(state))
    {
        currentMode = GIT_LOG;
    }

    if(currentMode != prevMode)
        needsFullRedraw = true;
}

void Editor::handleKeypress(int c)
{
    if(c < 0)
        return;
    LOG_DEBUG(LOG, "handleKeypress c={} ('{}') mode={}", c, (char)c,
              static_cast<int>(currentMode));

    if(dispatchModeKey(c))
    {
        return;
    }

    switch(currentMode)
    {
    case NORMAL:
        handleNormalMode(c);
        break;
    case INSERT:
    case REPLACE:
        handleInsertMode(c);
        break;
    case VISUAL:
    case VISUAL_LINE:
        handleVisualMode(c);
        break;
    case VISUAL_BLOCK:
        handleVisualBlockMode(c);
        break;
    case COMMAND:
        handleCommandMode(c);
        break;
    case SEARCH_FORWARD:
    case SEARCH_BACKWARD:
        handleSearchMode(c);
        break;
    case FILE_BROWSER:
        break;
    case FUZZY_FIND:
        break;
    case BUFFER_BROWSER:
        break;
    case OP_PENDING:
        handleOperatorPendingMode(c);
        break;
    default:
        break;
    }
}

bool isLikelyDefinition(const std::string& line, const std::string& symbol)
{
    // skip comments
    size_t commentPos = line.find("//");
    std::string effectiveLine =
        (commentPos != std::string::npos) ? line.substr(0, commentPos) : line;

    // name(
    if(effectiveLine.find(symbol + "(") != std::string::npos)
        return true;

    // Class::name(
    if(effectiveLine.find("::" + symbol + "(") != std::string::npos)
        return true;

    return false;
}

static bool find_robot_keyword_in_file(const std::string& path,
                                       std::string_view keyword, int& outY,
                                       int& outX)
{
    std::ifstream file(path);
    if(!file.is_open())
        return false;

    std::string line;
    bool inKeywords = false;
    int lineNo = 0;
    while(std::getline(file, line))
    {
        std::string_view view{line};
        if(robot_section_header(view))
        {
            inKeywords = robot_keyword_section(view);
            lineNo++;
            continue;
        }
        if(inKeywords && robot_is_keyword_def(view))
        {
            std::string_view name = robot_first_cell(view);
            if(!name.empty() && text_utils::iequals_ascii(name, keyword))
            {
                outY = lineNo;
                outX = 0;
                return true;
            }
        }
        lineNo++;
    }
    return false;
}

static bool find_python_def_in_file(const std::string& path,
                                    std::string_view symbol, int& outY,
                                    int& outX)
{
    std::ifstream file(path);
    if(!file.is_open())
        return false;

    std::string line;
    int lineNo = 0;
    while(std::getline(file, line))
    {
        std::string_view view{line};
        if(python_def_line(view, symbol))
        {
            outY = lineNo;
            outX = 0;
            return true;
        }
        lineNo++;
    }
    return false;
}

static bool is_skip_dir(const std::filesystem::path& path)
{
    std::string name = path.filename().string();
    return name == ".git" || name == ".venv" || name == "build" ||
           name == "node_modules" || name == "dist" || name == "out";
}

void Editor::createNewBuffer()
{
    auto buffer = std::make_unique<Buffer>();
    buffers.push_back(std::move(buffer));
    currentBufferIndex = buffers.size() - 1;
    updateCurrentBufferPointers();
    if(splitActive)
    {
        splitPanes[activePane].bufferIndex = currentBufferIndex;
        setPanePointers(activePane);
    }
    needsFullRedraw = true;
}

void Editor::updateCurrentBufferPointers()
{
    if(currentBufferIndex >= 0 && currentBufferIndex < buffers.size())
    {
        currentBuffer = buffers[currentBufferIndex].get();
        lines = &currentBuffer->lines;
        filename = &currentBuffer->filename;
        dirty = &currentBuffer->dirty;
        if(splitActive)
        {
            setPanePointers(activePane);
        }
        else
        {
            cursorX = &currentBuffer->cursorX;
            cursorY = &currentBuffer->cursorY;
            wantedX = &currentBuffer->wantedX;
            offsetX = &currentBuffer->offsetX;
            offsetY = &currentBuffer->offsetY;
        }
    }
    else
    {
        currentBufferIndex = -1;
        clearCurrentBufferPointers();
    }
}

void Editor::clearCurrentBufferPointers()
{
    currentBuffer = nullptr;
    lines = nullptr;
    filename = nullptr;
    dirty = nullptr;
    cursorX = nullptr;
    cursorY = nullptr;
    wantedX = nullptr;
    offsetX = nullptr;
    offsetY = nullptr;
}

bool Editor::hasBuffer() const
{
    return currentBuffer != nullptr;
}

void Editor::ensureBufferForMode(Mode mode)
{
    switch(mode)
    {
    case WELCOME:
    case COMMAND:
    case FILE_BROWSER:
    case FUZZY_FIND:
    case BUFFER_BROWSER:
    case GREP_SEARCH:
    case REFERENCES:
    case LSP_INFO:
        return;
    default:
        break;
    }

    if(!hasBuffer())
    {
        createNewBuffer();
        saveState();
        if(currentBuffer)
            currentBuffer->savedUndoIndex = 0;
    }
}

void Editor::switchToBuffer(int index)
{
    if(index >= 0 && index < buffers.size())
    {
        if(splitActive)
        {
            switchToBufferInActivePane(index);
        }
        else
        {
            saveBufferState();
            currentBufferIndex = index;
            updateCurrentBufferPointers();
            restoreBufferState();
            needsFullRedraw = true;
        }

        // Check if the file has been modified externally
        checkFileChanges();

        std::string msg = "Buffer " + std::to_string(currentBufferIndex + 1) +
                          "/" + std::to_string(buffers.size());
        if(!filename->empty())
        {
            msg += ": " + *filename;
        }
        else
        {
            msg += ": [No Name]";
        }
        if(*dirty)
        {
            msg += " [+]";
        }
        // setStatusMessage(msg);
    }
}

void Editor::nextBuffer()
{
    if(buffers.size() > 1)
    {
        int nextIndex = (currentBufferIndex + 1) % buffers.size();
        switchToBuffer(nextIndex);
    }
    else
    {
        setStatusMessage("No other buffers");
    }
}

void Editor::previousBuffer()
{
    if(buffers.size() > 1)
    {
        int prevIndex = currentBufferIndex - 1;
        if(prevIndex < 0)
            prevIndex = buffers.size() - 1;
        switchToBuffer(prevIndex);
    }
    else
    {
        setStatusMessage("No other buffers");
    }
}

void Editor::closeCurrentBuffer()
{
    if(*dirty)
    {
        setStatusMessage("No write since last change (add ! to override)");
        return;
    }

    if(buffers.size() == 1)
    {
        buffers.erase(buffers.begin());
        currentBufferIndex = -1;
        clearCurrentBufferPointers();
        splitActive = false;
        setMode(WELCOME);
    }
    else
    {
        int removedIndex = currentBufferIndex;
        buffers.erase(buffers.begin() + currentBufferIndex);
        if(currentBufferIndex >= buffers.size())
        {
            currentBufferIndex = buffers.size() - 1;
        }
        updateCurrentBufferPointers();
        if(splitActive)
        {
            for(int i = 0; i < 2; i++)
            {
                int& paneIndex = splitPanes[i].bufferIndex;
                if(paneIndex == removedIndex)
                {
                    paneIndex = currentBufferIndex;
                }
                else if(paneIndex > removedIndex)
                {
                    paneIndex -= 1;
                }
            }
            currentBufferIndex = splitPanes[activePane].bufferIndex;
            updateCurrentBufferPointers();
        }
        restoreBufferState();
    }

    needsFullRedraw = true;
}

void Editor::listBuffers()
{
    std::stringstream ss;
    ss << "Buffers: ";

    for(size_t i = 0; i < buffers.size(); i++)
    {
        if(i == currentBufferIndex)
            ss << "[";

        ss << (i + 1) << ":";

        if(!buffers[i]->filename.empty())
        {
            size_t lastSlash = buffers[i]->filename.find_last_of("/\\");
            if(lastSlash != std::string::npos)
                ss << buffers[i]->filename.substr(lastSlash + 1);
            else
                ss << buffers[i]->filename;
        }
        else
        {
            ss << "[No Name]";
        }

        if(buffers[i]->dirty)
            ss << "+";

        if(i == currentBufferIndex)
            ss << "]";

        if(i < buffers.size() - 1)
            ss << " ";
    }

    setStatusMessage(ss.str());
}

int Editor::findBufferByFilename(const std::string& fname)
{
    for(int i = 0; i < buffers.size(); i++)
    {
        if(buffers[i]->filename == fname)
            return i;
    }
    return -1;
}

void Editor::saveBufferState()
{
    // State is automatically saved in buffer structure
}

void Editor::restoreBufferState()
{
    if(currentMode == VISUAL || currentMode == VISUAL_LINE)
    {
        setMode(NORMAL);
    }
}

bool Editor::searchDefinitionInBuffer(Buffer* buf, const std::string& symbol,
                                      int& outY, int& outX)
{
    for(int y = 0; y < buf->lines.size(); ++y)
    {
        const std::string& line = buf->lines[y];
        if(isLikelyDefinition(line, symbol))
        {
            size_t pos = line.find(symbol);
            if(pos != std::string::npos)
            {
                outY = y;
                outX = pos;
                return true;
            }
        }
    }
    return false;
}

static std::string trim_ascii_ws(std::string_view s)
{
    size_t start = 0;
    while(start < s.size() && std::isspace((unsigned char)s[start]))
        ++start;
    size_t end = s.size();
    while(end > start && std::isspace((unsigned char)s[end - 1]))
        --end;
    return std::string(s.substr(start, end - start));
}

static std::string collect_signature_line(const std::vector<std::string>& lines,
                                          int startY, int maxLines)
{
    if(startY < 0 || startY >= (int)lines.size())
        return "";
    std::string out = trim_ascii_ws(lines[startY]);
    if(out.find('(') == std::string::npos)
        return out;
    if(out.find(')') != std::string::npos || out.find('{') != std::string::npos)
        return out;

    for(int i = 1; i <= maxLines && startY + i < (int)lines.size(); ++i)
    {
        std::string chunk = trim_ascii_ws(lines[startY + i]);
        if(chunk.empty())
            continue;
        out += " " + chunk;
        if(chunk.find(')') != std::string::npos ||
           chunk.find('{') != std::string::npos ||
           chunk.find(';') != std::string::npos)
        {
            break;
        }
    }
    return out;
}

static std::string extract_initializer_type_candidate(std::string_view rhs)
{
    rhs = trim_view(rhs);
    if(rhs.empty())
        return "";
    if(!rhs.empty() && rhs.back() == ';')
        rhs.remove_suffix(1);
    rhs = trim_view(rhs);
    if(rhs.empty())
        return "";

    int depth = 0;
    std::string token;
    token.reserve(rhs.size());
    for(size_t i = 0; i < rhs.size(); ++i)
    {
        char c = rhs[i];
        if(c == '<')
            depth++;
        else if(c == '>')
            depth = std::max(0, depth - 1);
        if(depth == 0 && (c == '(' || c == '{' || c == ';'))
            break;
        token.push_back(c);
    }
    return trim_ascii_ws(token);
}

static bool is_control_statement(std::string_view line)
{
    line = trim_view(line);
    auto starts = [&](std::string_view kw)
    {
        if(!line.starts_with(kw))
            return false;
        if(line.size() == kw.size())
            return true;
        char next = line[kw.size()];
        return text_utils::is_space(next) || next == '(';
    };
    return starts("if") || starts("for") || starts("while") ||
           starts("switch") || starts("return") || starts("throw") ||
           starts("catch") || starts("else");
}

static bool find_declaration_in_lines(const std::vector<std::string>& lines,
                                      const std::string& symbol, int& outY,
                                      int& outX)
{
    if(symbol.empty())
        return false;
    for(int y = 0; y < (int)lines.size(); ++y)
    {
        const std::string& line = lines[y];
        if(line.find(symbol) == std::string::npos)
            continue;
        if(is_control_statement(line))
            continue;

        size_t pos = 0;
        while((pos = line.find(symbol, pos)) != std::string::npos)
        {
            bool leftOk = true;
            if(pos > 0)
            {
                char prev = line[pos - 1];
                if(isIdent(prev) || prev == '.' || prev == '>' || prev == '*')
                {
                    leftOk = false;
                }
                else if(prev == ':' && (pos < 2 || line[pos - 2] != ':'))
                {
                    leftOk = false;
                }
            }
            if(!leftOk)
            {
                pos += symbol.size();
                continue;
            }
            size_t after = pos + symbol.size();
            if(after < line.size() && isIdent(line[after]))
            {
                pos += symbol.size();
                continue;
            }
            while(after < line.size() &&
                  std::isspace((unsigned char)line[after]))
            {
                ++after;
            }
            if(after >= line.size() || line[after] != '(')
            {
                pos += symbol.size();
                continue;
            }
            outY = y;
            outX = (int)pos;
            return true;
        }
    }
    return false;
}

static bool load_file_lines(const std::string& path,
                            std::vector<std::string>& out)
{
    std::ifstream in(path);
    if(!in.is_open())
        return false;
    out.clear();
    std::string line;
    while(std::getline(in, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        out.push_back(line);
    }
    if(out.empty())
        out.push_back("");
    return true;
}

static std::string last_qualifier(std::string_view text)
{
    std::string s(text);
    while(s.size() >= 2 && s.substr(s.size() - 2) == "::")
        s.resize(s.size() - 2);
    size_t pos = s.rfind("::");
    if(pos != std::string::npos)
        s = s.substr(pos + 2);
    return s;
}

static std::string extract_type_before_name(const std::string& line,
                                            const std::string& name)
{
    if(name.empty())
        return "";
    size_t pos = line.find(name);
    while(pos != std::string::npos)
    {
        bool leftOk = (pos == 0) || !isIdent(line[pos - 1]);
        size_t end = pos + name.size();
        bool rightOk = (end >= line.size()) || !isIdent(line[end]);
        if(leftOk && rightOk)
            break;
        pos = line.find(name, pos + name.size());
    }
    if(pos == std::string::npos)
        return "";

    int i = (int)pos - 1;
    auto is_skip = [](char c)
    { return c == ' ' || c == '\t' || c == '*' || c == '&'; };
    while(i >= 0 && is_skip(line[i]))
        --i;

    auto skip_template = [&](int& idx)
    {
        if(idx < 0 || line[idx] != '>')
            return;
        int depth = 0;
        while(idx >= 0)
        {
            char c = line[idx];
            if(c == '>')
                depth++;
            else if(c == '<')
            {
                depth--;
                if(depth == 0)
                {
                    --idx;
                    return;
                }
            }
            --idx;
        }
    };

    std::string qualifiers[] = {"const",    "volatile",  "mutable",
                                "static",   "constexpr", "inline",
                                "typename", "class",     "struct"};

    while(i >= 0)
    {
        while(i >= 0 && is_skip(line[i]))
            --i;
        skip_template(i);
        while(i >= 0 && is_skip(line[i]))
            --i;
        if(i < 0)
            break;

        int end = i;
        while(i >= 0 && (isIdent(line[i]) || line[i] == ':'))
            --i;
        if(end < 0 || end < i + 1)
            break;
        std::string token = line.substr((size_t)i + 1, (size_t)(end - i));
        bool isQualifier = false;
        for(const auto& q : qualifiers)
        {
            if(token == q)
            {
                isQualifier = true;
                break;
            }
        }
        if(isQualifier)
            continue;
        return token;
    }
    return "";
}

void Editor::openSymbolPopupForCursor()
{
    closeSymbolPopup();
    if(!currentBuffer || !lines)
        return;

    std::string symbol = getSymbolUnderCursor();
    if(symbol.empty())
    {
        setStatusMessage("No symbol");
        needsFullRedraw = true;
        return;
    }

    int defY = -1;
    int defX = 0;
    std::string signature;

    bool memberCall = false;
    std::string memberObject;
    {
        const std::string& line = (*lines)[*cursorY];
        int x = *cursorX;
        if(x >= 0 && x < (int)line.size() && isIdent(line[x]))
        {
            int l = x;
            while(l > 0 && isIdent(line[l - 1]))
                l--;
            int p = l - 1;
            while(p >= 0 && std::isspace((unsigned char)line[p]))
                --p;
            if(p >= 0 && line[p] == '.')
            {
                memberCall = true;
                int end = p - 1;
                while(end >= 0 && std::isspace((unsigned char)line[end]))
                    --end;
                int start = end;
                while(start >= 0 && isIdent(line[start]))
                    --start;
                if(end >= 0)
                    memberObject =
                        line.substr((size_t)start + 1, (size_t)(end - start));
            }
            else if(p >= 1 && line[p] == '>' && line[p - 1] == '-')
            {
                memberCall = true;
                int end = p - 2;
                while(end >= 0 && std::isspace((unsigned char)line[end]))
                    --end;
                int start = end;
                while(start >= 0 && isIdent(line[start]))
                    --start;
                if(end >= 0)
                    memberObject =
                        line.substr((size_t)start + 1, (size_t)(end - start));
            }
        }
    }

    auto resolve_return_type_for_function =
        [&](const std::string& funcName, const std::string& candidate,
            const std::vector<std::string>& currentLines) -> std::string
    {
        if(funcName.empty())
            return "";
        int y = -1;
        int x = 0;
        if(find_declaration_in_lines(currentLines, funcName, y, x))
        {
            std::string type =
                extract_type_before_name(currentLines[y], funcName);
            if(!type.empty())
                return type;
        }

        std::string alternate = findAlternateFile(currentBuffer->filename);
        if(!alternate.empty())
        {
            std::vector<std::string> altLines;
            if(load_file_lines(alternate, altLines))
            {
                if(find_declaration_in_lines(altLines, funcName, y, x))
                {
                    std::string type =
                        extract_type_before_name(altLines[y], funcName);
                    if(!type.empty())
                        return type;
                }
            }
        }

        if(candidate.rfind("std::", 0) == 0)
        {
            std::string base = last_qualifier(candidate.substr(5));
            std::string header = stdlib_goto::headerForSymbol(base);
            if(header.empty())
                header = stdlib_goto::headerForSymbol(funcName);
            if(!header.empty())
            {
                std::string headerPath = resolveSystemInclude(header);
                if(!headerPath.empty())
                {
                    std::vector<std::string> headerLines;
                    if(load_file_lines(headerPath, headerLines))
                    {
                        if(find_declaration_in_lines(headerLines, funcName, y,
                                                     x))
                        {
                            std::string type = extract_type_before_name(
                                headerLines[y], funcName);
                            if(!type.empty())
                                return type;
                        }
                    }
                }
            }
        }

        return "";
    };

    auto infer_auto_type_from_decl =
        [&](const std::string& declLine, const std::string& varName,
            const std::vector<std::string>& currentLines) -> std::string
    {
        size_t eq = declLine.find('=');
        if(eq == std::string::npos)
            return "";
        std::string_view rhs = std::string_view(declLine).substr(eq + 1);
        std::string candidate = extract_initializer_type_candidate(rhs);
        if(candidate.empty())
            return "";
        std::string funcName = last_qualifier(candidate);
        std::string type =
            resolve_return_type_for_function(funcName, candidate, currentLines);
        if(!type.empty())
            return type;
        return candidate;
    };

    if(memberCall && !memberObject.empty())
    {
        int objY = -1;
        int objX = 0;
        if(searchLocalDefinition(*lines, memberObject, *cursorY, *cursorX, objY,
                                 objX) ||
           searchMemberDefinition(*lines, memberObject, objY, objX))
        {
            std::string declLine = (*lines)[objY];
            std::string typeToken =
                extract_type_before_name(declLine, memberObject);
            if(typeToken == "auto")
            {
                typeToken =
                    infer_auto_type_from_decl(declLine, memberObject, *lines);
            }
            if(!typeToken.empty())
            {
                std::string base = last_qualifier(typeToken);
                std::string header = stdlib_goto::headerForSymbol(base);
                if(!header.empty())
                {
                    std::string headerPath = resolveSystemInclude(header);
                    if(!headerPath.empty())
                    {
                        std::vector<std::string> headerLines;
                        if(load_file_lines(headerPath, headerLines))
                        {
                            if(find_declaration_in_lines(headerLines, symbol,
                                                         defY, defX))
                            {
                                signature = collect_signature_line(headerLines,
                                                                   defY, 3);
                            }
                        }
                    }
                }
            }
        }
    }

    if(signature.empty() && symbolPrefix.rfind("std::", 0) == 0)
    {
        std::string base = last_qualifier(symbolPrefix.substr(5));
        std::string header = stdlib_goto::headerForSymbol(base);
        if(header.empty())
            header = stdlib_goto::headerForSymbol(symbol);
        if(!header.empty())
        {
            std::string headerPath = resolveSystemInclude(header);
            if(!headerPath.empty())
            {
                std::vector<std::string> headerLines;
                if(load_file_lines(headerPath, headerLines))
                {
                    if(find_declaration_in_lines(headerLines, symbol, defY,
                                                 defX))
                    {
                        signature =
                            collect_signature_line(headerLines, defY, 3);
                    }
                }
            }
        }
    }

    if(signature.empty())
    {
        std::string alternate = findAlternateFile(currentBuffer->filename);
        if(!alternate.empty())
        {
            std::vector<std::string> altLines;
            if(load_file_lines(alternate, altLines))
            {
                if(find_declaration_in_lines(altLines, symbol, defY, defX))
                    signature = collect_signature_line(altLines, defY, 3);
            }
        }
    }

    if(signature.empty())
    {
        if(find_declaration_in_lines(*lines, symbol, defY, defX))
        {
            signature = collect_signature_line(*lines, defY, 3);
        }
        else if(searchMemberDefinition(*lines, symbol, defY, defX))
        {
            signature = collect_signature_line(*lines, defY, 1);
        }
        else if(searchLocalDefinition(*lines, symbol, *cursorY, *cursorX, defY,
                                      defX))
        {
            signature = collect_signature_line(*lines, defY, 1);
        }
    }

    if(signature.empty())
    {
        std::string qualified =
            symbolPrefix.empty() ? symbol : symbolPrefix + symbol;
        signature = qualified + "()";
    }

    symbolPopupText = std::move(signature);
    symbolPopupActive = true;
    symbolPopupCursorX = *cursorX;
    symbolPopupCursorY = *cursorY;
    needsFullRedraw = true;
}

void Editor::closeSymbolPopup()
{
    symbolPopupActive = false;
    symbolPopupCursorX = -1;
    symbolPopupCursorY = -1;
    symbolPopupText.clear();
}
void Editor::run()
{
    //    setStatusMessage("Welcome to uVim!");

    while(true)
    {
        handleResize();
        draw();
        int c = Terminal::readKeyTimeout(50);
        if(c < 0)
        {
            // No key pressed, check if file has changed externally
            checkFileChanges();
            continue;
        }
        handleKeypress(c);
    }
}
void Editor::insertTab()
{
    for(int i = 0; i < tabSpaces; i++)
    {
        insertChar(' ');
    }
}

void Editor::toggleCase()
{
    if(*cursorY >= (int)lines->size())
        return;
    std::string& line = (*lines)[*cursorY];
    if(*cursorX >= (int)line.length())
        return;

    char c = line[*cursorX];
    if(std::isupper(c))
        line[*cursorX] = std::tolower(c);
    else if(std::islower(c))
        line[*cursorX] = std::toupper(c);

    if(*cursorX < (int)line.length() - 1)
        (*cursorX)++;
    *dirty = true;
    saveState();
}

void Editor::joinLines()
{
    if(*cursorY >= (int)lines->size() - 1)
        return;

    std::string& currentLine = (*lines)[*cursorY];
    const std::string& nextLine = (*lines)[*cursorY + 1];

    while(!currentLine.empty() && std::isspace(currentLine.back()))
    {
        currentLine.pop_back();
    }

    int joinPos = currentLine.length();

    if(!currentLine.empty() && !nextLine.empty())
    {
        currentLine += ' ';
        joinPos++;
    }

    size_t start = 0;
    while(start < nextLine.length() && std::isspace(nextLine[start]))
    {
        start++;
    }

    currentLine += nextLine.substr(start);
    lines->erase(lines->begin() + *cursorY + 1);

    *cursorX = joinPos;
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

void Editor::insertLineAbove()
{
    const std::string& currentLine = (*lines)[*cursorY];
    auto leading_ws_len = [](const std::string& s) -> size_t
    {
        size_t i = 0;
        while(i < s.size() && (s[i] == ' ' || s[i] == '\t'))
            ++i;
        return i;
    };
    auto ltrim = [&](const std::string& s) -> std::string
    {
        size_t i = leading_ws_len(s);
        return s.substr(i);
    };
    auto starts_with_kw = [](const std::string& s) -> bool
    {
        auto starts = [&](const char* kw) -> bool
        {
            size_t n = std::strlen(kw);
            if(s.size() < n)
                return false;
            if(s.compare(0, n, kw) != 0)
                return false;
            if(s.size() == n)
                return true;
            char next = s[n];
            return std::isspace((unsigned char)next) || next == ';';
        };
        return starts("return") || starts("break") || starts("continue") ||
               starts("throw") || starts("goto");
    };
    auto starts_control = [](const std::string& s) -> bool
    {
        auto starts = [&](const char* kw) -> bool
        {
            size_t n = std::strlen(kw);
            if(s.size() < n)
                return false;
            if(s.compare(0, n, kw) != 0)
                return false;
            if(s.size() == n)
                return true;
            char next = s[n];
            return std::isspace((unsigned char)next) || next == '(';
        };
        return starts("if") || starts("for") || starts("while") ||
               starts("else") || starts("switch");
    };
    size_t indent = 0;
    while(indent < currentLine.length() &&
          (currentLine[indent] == ' ' || currentLine[indent] == '\t'))
    {
        indent++;
    }
    std::string indentStr = currentLine.substr(0, indent);
    if(autoTags &&
       (isFileType<FileType::Html>() || isFileType<FileType::Xml>()))
    {
        size_t pos = currentLine.find('<');
        if(pos != std::string::npos)
        {
            size_t gt = currentLine.find('>', pos);
            if(gt != std::string::npos && pos + 1 < currentLine.size())
            {
                char next = currentLine[pos + 1];
                if(next != '/' && next != '!' && next != '?')
                {
                    size_t nameStart = pos + 1;
                    while(nameStart < gt && (currentLine[nameStart] == ' ' ||
                                             currentLine[nameStart] == '\t'))
                        ++nameStart;
                    size_t nameEnd = nameStart;
                    auto isTagChar = [](char ch)
                    {
                        return text_utils::is_alnum(ch) || ch == ':' ||
                               ch == '_' || ch == '-';
                    };
                    while(nameEnd < gt && isTagChar(currentLine[nameEnd]))
                        ++nameEnd;
                    bool isVoid = false;
                    if(nameEnd > nameStart)
                    {
                        std::string_view tag =
                            std::string_view(currentLine)
                                .substr(nameStart, nameEnd - nameStart);
                        if(isFileType<FileType::Html>())
                        {
                            for(auto v : constants::html_void_tags)
                            {
                                if(text_utils::iequals_ascii(tag, v))
                                {
                                    isVoid = true;
                                    break;
                                }
                            }
                        }
                    }
                    if(isVoid)
                        return;
                    indentStr.append(tabSpaces, ' ');
                }
            }
        }
    }
    if(isFileType<FileType::Cpp>())
    {
        std::string trimmed = ltrim(currentLine);
        if(starts_with_kw(trimmed))
        {
            bool adjusted = false;
            for(int y = *cursorY - 1; y >= 0; --y)
            {
                const std::string& prevLine = (*lines)[y];
                std::string prevTrim = ltrim(prevLine);
                if(prevTrim.empty())
                    continue;
                size_t prevIndent = leading_ws_len(prevLine);
                if(prevIndent < indent)
                {
                    if(starts_control(prevTrim) &&
                       prevTrim.find('{') == std::string::npos)
                    {
                        indentStr = prevLine.substr(0, prevIndent);
                        adjusted = true;
                    }
                    break;
                }
            }
            if(!adjusted && !indentStr.empty())
            {
                if(indentStr.back() == '\t')
                {
                    indentStr.pop_back();
                }
                else if(indentStr.length() >= 4)
                {
                    indentStr.erase(indentStr.length() - 4);
                }
                else
                {
                    indentStr.clear();
                }
            }
        }
    }

    lines->insert(lines->begin() + *cursorY, indentStr);
    *cursorX = (int)indentStr.length();
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

void Editor::insertLineBelow()
{
    if(*cursorY >= (int)lines->size())
    {
        lines->push_back("");
    }
    else
    {
        const std::string& currentLine = (*lines)[*cursorY];
        auto leading_ws_len = [](const std::string& s) -> size_t
        {
            size_t i = 0;
            while(i < s.size() && (s[i] == ' ' || s[i] == '\t'))
                ++i;
            return i;
        };
        auto ltrim = [&](const std::string& s) -> std::string
        {
            size_t i = leading_ws_len(s);
            return s.substr(i);
        };
        auto starts_with_kw = [](const std::string& s) -> bool
        {
            auto starts = [&](const char* kw) -> bool
            {
                size_t n = std::strlen(kw);
                if(s.size() < n)
                    return false;
                if(s.compare(0, n, kw) != 0)
                    return false;
                if(s.size() == n)
                    return true;
                char next = s[n];
                return std::isspace((unsigned char)next) || next == ';';
            };
            return starts("return") || starts("break") || starts("continue") ||
                   starts("throw") || starts("goto");
        };
        auto starts_control = [](const std::string& s) -> bool
        {
            auto starts = [&](const char* kw) -> bool
            {
                size_t n = std::strlen(kw);
                if(s.size() < n)
                    return false;
                if(s.compare(0, n, kw) != 0)
                    return false;
                if(s.size() == n)
                    return true;
                char next = s[n];
                return std::isspace((unsigned char)next) || next == '(';
            };
            return starts("if") || starts("for") || starts("while") ||
                   starts("else") || starts("switch");
        };
        size_t indent = 0;
        while(indent < currentLine.length() &&
              (currentLine[indent] == ' ' || currentLine[indent] == '\t'))
        {
            indent++;
        }
        std::string indentStr = currentLine.substr(0, indent);

        bool addExtraIndent = false;
        if(isFileType<FileType::Cpp>())
        {
            size_t lastNonSpace = currentLine.find_last_not_of(" \t");
            if(lastNonSpace != std::string::npos &&
               currentLine[lastNonSpace] == '{')
            {
                addExtraIndent = true;
            }
        }
        if(autoTags &&
           (isFileType<FileType::Html>() || isFileType<FileType::Xml>()))
        {
            bool htmlShouldIndent = false;
            size_t lt = currentLine.rfind('<');
            size_t gt = currentLine.rfind('>');
            if(lt != std::string::npos && gt != std::string::npos && lt < gt &&
               lt + 1 < currentLine.size())
            {
                char next = currentLine[lt + 1];
                if(next != '/' && next != '!' && next != '?')
                {
                    size_t selfClose = currentLine.rfind('/');
                    if(selfClose == std::string::npos || selfClose < lt ||
                       selfClose > gt)
                    {
                        bool isVoid = false;
                        size_t nameStart = lt + 1;
                        while(nameStart < gt &&
                              (currentLine[nameStart] == ' ' ||
                               currentLine[nameStart] == '\t'))
                            ++nameStart;
                        size_t nameEnd = nameStart;
                        auto isTagChar = [](char ch)
                        {
                            return text_utils::is_alnum(ch) || ch == ':' ||
                                   ch == '_' || ch == '-';
                        };
                        while(nameEnd < gt && isTagChar(currentLine[nameEnd]))
                            ++nameEnd;
                        if(nameEnd > nameStart && isFileType<FileType::Html>())
                        {
                            std::string_view tag =
                                std::string_view(currentLine)
                                    .substr(nameStart, nameEnd - nameStart);
                            for(auto v : constants::html_void_tags)
                            {
                                if(text_utils::iequals_ascii(tag, v))
                                {
                                    isVoid = true;
                                    break;
                                }
                            }
                        }
                        if(!isVoid)
                            htmlShouldIndent = true;
                    }
                }
            }
            if(htmlShouldIndent)
                addExtraIndent = true;
        }

        if(isFileType<FileType::Cpp>() && !addExtraIndent)
        {
            std::string trimmed = ltrim(currentLine);
            if(starts_with_kw(trimmed))
            {
                bool adjusted = false;
                for(int y = *cursorY - 1; y >= 0; --y)
                {
                    const std::string& prevLine = (*lines)[y];
                    std::string prevTrim = ltrim(prevLine);
                    if(prevTrim.empty())
                        continue;
                    size_t prevIndent = leading_ws_len(prevLine);
                    if(prevIndent < indent)
                    {
                        if(starts_control(prevTrim) &&
                           prevTrim.find('{') == std::string::npos)
                        {
                            indentStr = prevLine.substr(0, prevIndent);
                            adjusted = true;
                        }
                        break;
                    }
                }
                if(!adjusted && !indentStr.empty())
                {
                    if(indentStr.back() == '\t')
                    {
                        indentStr.pop_back();
                    }
                    else if(indentStr.length() >= 4)
                    {
                        indentStr.erase(indentStr.length() - 4);
                    }
                    else
                    {
                        indentStr.clear();
                    }
                }
            }
        }

        std::string newLine = indentStr;
        if(addExtraIndent)
            newLine.append(tabSpaces, ' ');

        lines->insert(lines->begin() + *cursorY + 1, newLine);
    }
    (*cursorY)++;
    if(*cursorY >= 0 && *cursorY < (int)lines->size())
        *cursorX = (int)(*lines)[*cursorY].length();
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

void Editor::deleteCurrentLine()
{
    if(lines->empty())
        return;

    yankLine();
    lines->erase(lines->begin() + *cursorY);

    if(lines->empty())
    {
        lines->push_back("");
    }
    if(*cursorY >= (int)lines->size())
    {
        *cursorY = lines->size() - 1;
    }
    *cursorX = 0;
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

void Editor::deleteToLineStart()
{
    if(*cursorY >= (int)lines->size())
        return;

    std::string& line = (*lines)[*cursorY];
    if(*cursorX > 0 && *cursorX <= (int)line.length())
    {
        line.erase(0, *cursorX);
        *cursorX = 0;
        *dirty = true;
    }
}

void Editor::deleteCharAtCursor()
{
    deleteCharForward();
    saveState();
}

void Editor::deleteCharBeforeCursor()
{
    if(*cursorX > 0)
    {
        deleteChar();
        saveState();
    }
}

void Editor::deleteWordBackward()
{
    if(*cursorY >= (int)lines->size())
        return;
    std::string& line = (*lines)[*cursorY];

    if(*cursorX == 0)
        return;

    int start = *cursorX;

    while(*cursorX > 0 && std::isspace(line[*cursorX - 1]))
    {
        (*cursorX)--;
    }
    while(*cursorX > 0 && !std::isspace(line[*cursorX - 1]))
    {
        (*cursorX)--;
    }

    line.erase(*cursorX, start - *cursorX);
    *dirty = true;
}

void Editor::deleteWord()
{
    if(*cursorY >= (int)lines->size())
        return;
    std::string& line = (*lines)[*cursorY];

    if(line.empty())
        return;

    int start = *cursorX;
    int end = *cursorX;

    if(end >= (int)line.length())
        return;

    // Helper lambda to check if char is a word character (alphanumeric or
    // underscore)
    auto isWordChar = [](char c)
    { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };

    char startChar = line[end];

    if(std::isspace(static_cast<unsigned char>(startChar)))
    {
        // On whitespace: delete whitespace, then the next word/punctuation
        while(end < (int)line.length() &&
              std::isspace(static_cast<unsigned char>(line[end])))
            end++;

        // Now delete the word or punctuation sequence
        if(end < (int)line.length())
        {
            if(isWordChar(line[end]))
            {
                while(end < (int)line.length() && isWordChar(line[end]))
                    end++;
            }
            else
            {
                // Punctuation sequence
                while(end < (int)line.length() && !isWordChar(line[end]) &&
                      !std::isspace(static_cast<unsigned char>(line[end])))
                    end++;
            }
        }
    }
    else if(isWordChar(startChar))
    {
        // On a word character: delete word + trailing whitespace
        while(end < (int)line.length() && isWordChar(line[end]))
            end++;

        // Include trailing whitespace
        while(end < (int)line.length() &&
              std::isspace(static_cast<unsigned char>(line[end])))
            end++;
    }
    else
    {
        // On punctuation: delete punctuation sequence + trailing whitespace
        while(end < (int)line.length() && !isWordChar(line[end]) &&
              !std::isspace(static_cast<unsigned char>(line[end])))
            end++;

        // Include trailing whitespace
        while(end < (int)line.length() &&
              std::isspace(static_cast<unsigned char>(line[end])))
            end++;
    }

    if(end > start)
    {
        // Yank before deleting
        yankBuffer = line.substr(start, end - start);
        line.erase(start, end - start);

        // Adjust cursor if past end of line
        if(*cursorX >= (int)line.length() && !line.empty())
        {
            *cursorX = line.length() - 1;
        }
        *dirty = true;
    }
}

void Editor::yankWord()
{
    if(*cursorY >= (int)lines->size())
        return;
    const std::string& line = (*lines)[*cursorY];

    if(*cursorX >= (int)line.length())
        return;

    int start = *cursorX;
    int end = *cursorX;

    // Get current word characters
    while(end < (int)line.length() && !std::isspace(line[end]))
    {
        end++;
    }
    // Include trailing whitespace
    while(end < (int)line.length() && std::isspace(line[end]))
    {
        end++;
    }

    yankBuffer = line.substr(start, end - start);
    setStatusMessage("Yanked: " + std::to_string(end - start) + " chars");
}

void Editor::handleBackspace()
{
    deleteChar();
}

void Editor::replaceCharAtCursor(char c)
{
    if(*cursorY >= (int)lines->size())
        return;
    std::string& line = (*lines)[*cursorY];
    if(*cursorX >= (int)line.length())
        return;

    line[*cursorX] = c;
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

void Editor::beginChangeRecording(int count)
{
    if(replayingChange || recordingChange)
        return;
    recordingChange = true;
    deferChangeCommit = false;
    pendingChangeKeys.clear();
    pendingChangeCount = (count > 0) ? count : 1;
}

void Editor::recordChangeKey(int key)
{
    if(!recordingChange || replayingChange)
        return;
    pendingChangeKeys.push_back(key);
}

void Editor::deferChangeRecordingCommit()
{
    if(!recordingChange || replayingChange)
        return;
    deferChangeCommit = true;
}

void Editor::commitChangeRecording()
{
    if(!recordingChange || replayingChange)
        return;
    if(!pendingChangeKeys.empty())
    {
        lastChangeKeys = pendingChangeKeys;
        lastChangeCount = pendingChangeCount;
    }
    recordingChange = false;
    deferChangeCommit = false;
    pendingChangeKeys.clear();
    pendingChangeCount = 1;
}

void Editor::cancelChangeRecording()
{
    recordingChange = false;
    deferChangeCommit = false;
    pendingChangeKeys.clear();
    pendingChangeCount = 1;
}

void Editor::finishChangeRecordingIfDeferred()
{
    if(recordingChange && deferChangeCommit)
    {
        commitChangeRecording();
    }
}

bool Editor::isRecordingChange() const
{
    return recordingChange;
}

bool Editor::isReplayingChange() const
{
    return replayingChange;
}

int Editor::readKeyRecorded()
{
    int key = Terminal::readKey();
    recordChangeKey(key);
    return key;
}

void Editor::repeatLastChange(int times)
{
    if(lastChangeKeys.empty())
    {
        setStatusMessage("No previous change");
        return;
    }
    int repeats = std::max(1, times);
    replayingChange = true;

    for(int i = repeats - 1; i >= 0; --i)
    {
        std::vector<int> sequence;
        if(lastChangeCount > 1)
        {
            std::string countStr = std::to_string(lastChangeCount);
            for(char ch : countStr)
                sequence.push_back(static_cast<unsigned char>(ch));
        }
        sequence.insert(sequence.end(), lastChangeKeys.begin(),
                        lastChangeKeys.end());
        for(auto it = sequence.rbegin(); it != sequence.rend(); ++it)
            Terminal::unreadKey(*it);
    }
}

void Editor::insertUtf8Char(int c)
{
    if(c < 128)
    {
        insertChar((char)c);
    }
    else
    {
        char buf[5] = {0};
        if(c < 0x800)
        {
            buf[0] = 0xC0 | (c >> 6);
            buf[1] = 0x80 | (c & 0x3F);
        }
        else if(c < 0x10000)
        {
            buf[0] = 0xE0 | (c >> 12);
            buf[1] = 0x80 | ((c >> 6) & 0x3F);
            buf[2] = 0x80 | (c & 0x3F);
        }
        else
        {
            buf[0] = 0xF0 | (c >> 18);
            buf[1] = 0x80 | ((c >> 12) & 0x3F);
            buf[2] = 0x80 | ((c >> 6) & 0x3F);
            buf[3] = 0x80 | (c & 0x3F);
        }
        for(int i = 0; buf[i]; i++)
        {
            insertChar(buf[i]);
        }
    }
}

void Editor::indentCurrentLine()
{
    if(*cursorY >= (int)lines->size())
        return;
    (*lines)[*cursorY] = "    " + (*lines)[*cursorY];
    *cursorX += 4;
    *dirty = true;
}

void Editor::dedentCurrentLine()
{
    if(*cursorY >= (int)lines->size())
        return;
    std::string& line = (*lines)[*cursorY];

    int remove = 0;
    while(remove < 4 && remove < (int)line.length() &&
          (line[remove] == ' ' || line[remove] == '\t'))
    {
        remove++;
    }

    if(remove > 0)
    {
        line.erase(0, remove);
        *cursorX = std::max(0, *cursorX - remove);
        *dirty = true;
    }
}

void Editor::handleLinewiseOperator(char op, int count)
{
    switch(op)
    {
    case 'd':
        for(int i = 0; i < count && !lines->empty(); i++)
        {
            deleteCurrentLine();
        }
        break;
    case 'y':
    {
        LOG_DEBUG(LOG,
                  "handleLinewiseOperator: yy detected, count={}, cursorY={}",
                  count, *cursorY);
        yankBuffer.clear();
        int endLine = std::min(*cursorY + count, (int)lines->size());
        for(int y = *cursorY; y < endLine; y++)
        {
            yankBuffer += (*lines)[y] + "\n";
        }

        LOG_DEBUG(LOG,
                  "handleLinewiseOperator: yankBuffer.length()={}, "
                  "useSystemClipboard={}",
                  yankBuffer.length(), useSystemClipboard);

        std::string msg = std::to_string(count) + " lines yanked";
        if(useSystemClipboard && !yankBuffer.empty())
        {
            LOG_DEBUG(LOG,
                      "handleLinewiseOperator: calling setSystemClipboard");
            setSystemClipboard(yankBuffer);
            msg += " (copied to clipboard)";
        }
        setStatusMessage(msg);
    }
    break;
    case '>':
        for(int i = 0; i < count && *cursorY + i < (int)lines->size(); i++)
        {
            (*lines)[*cursorY + i] = "    " + (*lines)[*cursorY + i];
        }
        *dirty = true;
        saveState();
        needsFullRedraw = true;
        break;
    case '<':
        for(int i = 0; i < count && *cursorY + i < (int)lines->size(); i++)
        {
            std::string& line = (*lines)[*cursorY + i];
            int remove = 0;
            while(remove < 4 && remove < (int)line.length() &&
                  (line[remove] == ' ' || line[remove] == '\t'))
            {
                remove++;
            }
            if(remove > 0)
                line.erase(0, remove);
        }
        *dirty = true;
        saveState();
        needsFullRedraw = true;
        break;
    case '=':
        for(int i = 0; i < count && *cursorY + i < (int)lines->size(); i++)
        {
            autoIndentLine(*cursorY + i);
        }
        *dirty = true;
        saveState();
        needsFullRedraw = true;
        break;
    case 'c':
        for(int i = 0; i < count && !lines->empty(); i++)
        {
            deleteCurrentLine();
        }
        insertLineAbove();
        break;
    }
}

// ============================================================================
// Extended Visual Mode Commands
// ============================================================================

void Editor::setVisualRange()
{
    currentBuffer->visualEndX = *cursorX;
    currentBuffer->visualEndY = *cursorY;
}

void Editor::swapVisualEnds()
{
    std::swap(*cursorX, currentBuffer->visualStartX);
    std::swap(*cursorY, currentBuffer->visualStartY);
    currentBuffer->visualEndX = *cursorX;
    currentBuffer->visualEndY = *cursorY;
    adjustViewport();
}

void Editor::swapVisualBlockCorner()
{
    std::swap(*cursorX, currentBuffer->visualBlockStartX);
    std::swap(*cursorY, currentBuffer->visualBlockStartY);
    currentBuffer->visualBlockEndX = *cursorX;
    currentBuffer->visualBlockEndY = *cursorY;
    adjustViewport();
}

void Editor::prepareBlockInsert(bool atEnd)
{
    int startY, startX, endY, endX;
    getVisualBlockBounds(startY, startX, endY, endX);

    currentBuffer->visualBlockStartY = startY;
    currentBuffer->visualBlockEndY = endY;
    currentBuffer->visualBlockStartX = atEnd ? endX + 1 : startX;
    currentBuffer->visualBlockInsertText.clear();

    *cursorY = startY;
    *cursorX = atEnd ? endX + 1 : startX;

    if(*cursorY < (int)lines->size())
    {
        std::string& line = (*lines)[*cursorY];
        while((int)line.length() < *cursorX)
        {
            line += ' ';
        }
    }
}

void Editor::indentSelection()
{
    int startY, startX, endY, endX;
    getSelectionBounds(startY, startX, endY, endX);

    for(int y = startY; y <= endY && y < (int)lines->size(); y++)
    {
        (*lines)[y] = "    " + (*lines)[y];
    }
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

void Editor::dedentSelection()
{
    int startY, startX, endY, endX;
    getSelectionBounds(startY, startX, endY, endX);

    for(int y = startY; y <= endY && y < (int)lines->size(); y++)
    {
        std::string& line = (*lines)[y];
        int remove = 0;
        while(remove < 4 && remove < (int)line.length() &&
              (line[remove] == ' ' || line[remove] == '\t'))
        {
            remove++;
        }
        if(remove > 0)
            line.erase(0, remove);
    }
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

void Editor::autoIndentSelection()
{
    int startY, startX, endY, endX;
    getSelectionBounds(startY, startX, endY, endX);

    for(int y = startY; y <= endY && y < (int)lines->size(); y++)
    {
        autoIndentLine(y);
    }
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

void Editor::lowercaseSelection()
{
    int startY, startX, endY, endX;
    getSelectionBounds(startY, startX, endY, endX);

    for(int y = startY; y <= endY && y < (int)lines->size(); y++)
    {
        std::string& line = (*lines)[y];
        int start = (y == startY) ? startX : 0;
        int end = (y == endY) ? std::min(endX + 1, (int)line.length())
                              : line.length();

        for(int x = start; x < end; x++)
        {
            line[x] = std::tolower(line[x]);
        }
    }
    *dirty = true;
    saveState();
    setMode(NORMAL);
    needsFullRedraw = true;
}

void Editor::uppercaseSelection()
{
    int startY, startX, endY, endX;
    getSelectionBounds(startY, startX, endY, endX);

    for(int y = startY; y <= endY && y < (int)lines->size(); y++)
    {
        std::string& line = (*lines)[y];
        int start = (y == startY) ? startX : 0;
        int end = (y == endY) ? std::min(endX + 1, (int)line.length())
                              : line.length();

        for(int x = start; x < end; x++)
        {
            line[x] = std::toupper(line[x]);
        }
    }
    *dirty = true;
    saveState();
    setMode(NORMAL);
    needsFullRedraw = true;
}

void Editor::toggleCaseSelection()
{
    int startY, startX, endY, endX;
    getSelectionBounds(startY, startX, endY, endX);

    for(int y = startY; y <= endY && y < (int)lines->size(); y++)
    {
        std::string& line = (*lines)[y];
        int start = (y == startY) ? startX : 0;
        int end = (y == endY) ? std::min(endX + 1, (int)line.length())
                              : line.length();

        for(int x = start; x < end; x++)
        {
            if(std::isupper(line[x]))
                line[x] = std::tolower(line[x]);
            else if(std::islower(line[x]))
                line[x] = std::toupper(line[x]);
        }
    }
    *dirty = true;
    saveState();
    setMode(NORMAL);
    needsFullRedraw = true;
}

void Editor::yankLineSelection()
{
    int startY =
        std::min(currentBuffer->visualStartY, currentBuffer->visualEndY);
    int endY = std::max(currentBuffer->visualStartY, currentBuffer->visualEndY);

    yankBuffer.clear();
    for(int y = startY; y <= endY && y < (int)lines->size(); y++)
    {
        yankBuffer += (*lines)[y] + "\n";
    }

    if(useSystemClipboard && !yankBuffer.empty())
    {
        setSystemClipboard(yankBuffer);
    }
    setStatusMessage(std::to_string(endY - startY + 1) + " lines yanked");
}

void Editor::deleteLineSelection()
{
    int startY =
        std::min(currentBuffer->visualStartY, currentBuffer->visualEndY);
    int endY = std::max(currentBuffer->visualStartY, currentBuffer->visualEndY);

    yankLineSelection();

    for(int y = endY; y >= startY; y--)
    {
        if(y < (int)lines->size())
        {
            lines->erase(lines->begin() + y);
            if(currentBuffer && currentBuffer->blameValid &&
               y < (int)currentBuffer->blameEntries.size())
            {
                currentBuffer->blameEntries.erase(
                    currentBuffer->blameEntries.begin() + y);
            }
        }
    }

    if(lines->empty())
        lines->push_back("");

    *cursorY = std::min(startY, (int)lines->size() - 1);
    *cursorX = 0;
    *dirty = true;
    if(currentBuffer && currentBuffer->blameValid)
    {
        currentBuffer->blameStart = 0;
        currentBuffer->blameEnd = (int)currentBuffer->blameEntries.size() - 1;
    }
    saveState();
    setMode(NORMAL);
    needsFullRedraw = true;
}

void Editor::indentLineSelection()
{
    int startY =
        std::min(currentBuffer->visualStartY, currentBuffer->visualEndY);
    int endY = std::max(currentBuffer->visualStartY, currentBuffer->visualEndY);

    for(int y = startY; y <= endY && y < (int)lines->size(); y++)
    {
        (*lines)[y] = "    " + (*lines)[y];
    }
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

void Editor::dedentLineSelection()
{
    int startY =
        std::min(currentBuffer->visualStartY, currentBuffer->visualEndY);
    int endY = std::max(currentBuffer->visualStartY, currentBuffer->visualEndY);

    for(int y = startY; y <= endY && y < (int)lines->size(); y++)
    {
        std::string& line = (*lines)[y];
        int remove = 0;
        while(remove < 4 && remove < (int)line.length() &&
              (line[remove] == ' ' || line[remove] == '\t'))
        {
            remove++;
        }
        if(remove > 0)
            line.erase(0, remove);
    }
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

void Editor::autoIndentLineSelection()
{
    int startY =
        std::min(currentBuffer->visualStartY, currentBuffer->visualEndY);
    int endY = std::max(currentBuffer->visualStartY, currentBuffer->visualEndY);

    for(int y = startY; y <= endY && y < (int)lines->size(); y++)
    {
        autoIndentLine(y);
    }
    *dirty = true;
    saveState();
    needsFullRedraw = true;
}

// ============================================================================
// Marks
// ============================================================================

void Editor::setMark(char mark)
{
    if(mark >= 'a' && mark <= 'z')
    {
        MarkLocation loc;
        loc.filename = *filename;
        loc.line = *cursorY;
        loc.col = *cursorX;
        marks[mark] = loc;
        setStatusMessage(std::string("Mark '") + mark + "' set");
    }
}

void Editor::jumpToMark(char mark)
{
    if(mark >= 'a' && mark <= 'z')
    {
        auto it = marks.find(mark);
        if(it != marks.end())
        {
            pushJumpLocation();
            *cursorY = it->second.line;
            *cursorX = it->second.col;
            if(*cursorY >= (int)lines->size())
                *cursorY = lines->size() - 1;
            if(*cursorY >= 0 && *cursorX > (int)(*lines)[*cursorY].length())
                *cursorX = (*lines)[*cursorY].length();
            adjustViewport();
        }
        else
        {
            setStatusMessage(std::string("Mark '") + mark + "' not set");
        }
    }
}

// ============================================================================
// Misc Utilities
// ============================================================================

void Editor::goToFile()
{
    std::string word = getSymbolUnderCursor();
    if(!word.empty())
    {
        if(fileExists(word))
        {
            openFile(word);
        }
        else
        {
            setStatusMessage("File not found: " + word);
        }
    }
}

void Editor::showFileInfo()
{
    std::string info =
        "\"" + (filename->empty() ? "[No Name]" : *filename) + "\"";
    info += " " + std::to_string(lines->size()) + " lines";
    if(*dirty)
        info += " [Modified]";
    info += " -- " + std::to_string(*cursorY + 1) + "/" +
            std::to_string(lines->size());
    info +=
        " -- " +
        std::to_string((*cursorY + 1) * 100 / std::max(1, (int)lines->size())) +
        "%";
    setStatusMessage(info);
}

void Editor::forceFullRedraw()
{
    needsFullRedraw = true;
}

void Editor::executeOneNormalCommand(int key)
{
    switch(key)
    {
    case 'w':
        moveWordForward();
        break;
    case 'b':
        moveWordBackward();
        break;
    case 'e':
        moveToEndOfWord();
        break;
    case '0':
        moveToLineStart();
        break;
    case '$':
        moveToLineEnd();
        break;
    case 'h':
        moveLeft();
        break;
    case 'l':
        moveRight();
        break;
    case 'j':
        moveDown();
        adjustViewport();
        break;
    case 'k':
        moveUp();
        adjustViewport();
        break;
    default:
        break;
    }
}

// ============================================================================
// Command History
// ============================================================================

std::optional<std::string> Editor::commandHistoryUp()
{
    if(commandHistory.empty())
        return std::nullopt;
    if(commandHistoryIndex < 0)
    {
        commandHistoryIndex = commandHistory.size() - 1;
    }
    else if(commandHistoryIndex > 0)
    {
        commandHistoryIndex--;
    }
    commandInput = commandHistory[commandHistoryIndex];
    return commandInput;
}

std::optional<std::string> Editor::commandHistoryDown()
{
    if(commandHistory.empty() || commandHistoryIndex < 0)
        return std::nullopt;
    if(commandHistoryIndex < (int)commandHistory.size() - 1)
    {
        commandHistoryIndex++;
        commandInput = commandHistory[commandHistoryIndex];
        return commandInput;
    }

    commandHistoryIndex = -1;
    commandInput.clear();
    return commandInput;
}

void Editor::startCommandPopup()
{
    commandPopupActive = true;
    commandPopupQuery.clear();
    commandPopupCursor = 0;
    commandPopupOffset = 0;
    commandPopupAll = getCommandCompletions("");
    updateCommandPopup("");
}

void Editor::cancelCommandPopup()
{
    commandPopupActive = false;
    commandPopupQuery.clear();
    commandPopupFiltered.clear();
    commandPopupCursor = 0;
    commandPopupOffset = 0;
}

void Editor::updateCommandPopup(std::string_view query)
{
    if(!commandPopupActive)
        return;

    commandPopupQuery = std::string(query);
    commandPopupFiltered.clear();
    commandPopupCursor = 0;
    commandPopupOffset = 0;

    bool isSetQuery = commandPopupQuery.rfind("set", 0) == 0;
    bool isHelpQuery = commandPopupQuery == "help" ||
                       commandPopupQuery == "h" ||
                       commandPopupQuery.rfind("help ", 0) == 0 ||
                       commandPopupQuery.rfind("h ", 0) == 0;
    if(isSetQuery)
    {
        commandPopupAll = getSetCompletions("");
    }
    else if(isHelpQuery)
    {
        std::string cmd = (commandPopupQuery.rfind("h", 0) == 0 &&
                           commandPopupQuery.rfind("help", 0) != 0)
                              ? "h"
                              : "help";
        std::string topicPrefix;
        if(commandPopupQuery.size() > cmd.size() &&
           commandPopupQuery[cmd.size()] == ' ')
        {
            topicPrefix = commandPopupQuery.substr(cmd.size() + 1);
        }
        auto topics = getHelpCompletions(topicPrefix);
        commandPopupAll.clear();
        for(const auto& topic : topics)
            commandPopupAll.push_back(cmd + " " + topic);
    }
    else
    {
        commandPopupAll = getCommandCompletions("");
    }

    if(commandPopupQuery.empty())
    {
        for(int i = 0; i < (int)commandPopupAll.size(); ++i)
            commandPopupFiltered.push_back(i);
        needsFullRedraw = true;
        return;
    }

    if(isHelpQuery)
    {
        std::string prefix = commandPopupQuery;
        for(int i = 0; i < (int)commandPopupAll.size(); ++i)
        {
            if(commandPopupAll[i].rfind(prefix, 0) == 0)
                commandPopupFiltered.push_back(i);
        }
        needsFullRedraw = true;
        return;
    }

    std::vector<std::pair<int, int>> scored;
    std::vector<int> positions;
    scored.reserve(commandPopupAll.size());

    for(int i = 0; i < (int)commandPopupAll.size(); ++i)
    {
        int score =
            fuzzyScore(commandPopupQuery, commandPopupAll[i], positions);
        if(score >= 0)
            scored.emplace_back(i, score);
    }

    std::stable_sort(
        scored.begin(), scored.end(),
        [](const std::pair<int, int>& left, const std::pair<int, int>& right)
        {
            if(left.second != right.second)
                return left.second > right.second;
            return left.first < right.first;
        });

    for(const auto& entry : scored)
        commandPopupFiltered.push_back(entry.first);

    needsFullRedraw = true;
}

void Editor::moveCommandPopupCursor(int delta)
{
    if(!commandPopupActive || commandPopupFiltered.empty())
        return;

    int next = commandPopupCursor + delta;
    if(next < 0)
        next = 0;
    if(next >= (int)commandPopupFiltered.size())
        next = (int)commandPopupFiltered.size() - 1;
    commandPopupCursor = next;

    const int window = std::min(8, (int)commandPopupFiltered.size());
    if(commandPopupCursor < commandPopupOffset)
        commandPopupOffset = commandPopupCursor;
    else if(commandPopupCursor >= commandPopupOffset + window)
        commandPopupOffset = commandPopupCursor - window + 1;

    needsFullRedraw = true;
}

bool Editor::isCommandPopupActive() const
{
    return commandPopupActive;
}

std::optional<std::string> Editor::commandPopupSelection() const
{
    if(!commandPopupActive || commandPopupFiltered.empty())
        return std::nullopt;
    int idx = commandPopupFiltered[commandPopupCursor];
    if(idx < 0 || idx >= (int)commandPopupAll.size())
        return std::nullopt;
    return commandPopupAll[idx];
}

void Editor::startCommandHistorySearch(std::string_view seed)
{
    commandHistorySearchActive = true;
    commandHistorySearchOriginal = std::string(seed);
    commandHistorySearchQueryValue = std::string(seed);
    commandHistorySearchCursor = 0;
    commandHistorySearchOffset = 0;
    updateCommandHistorySearchQuery(commandHistorySearchQueryValue);
}

std::string Editor::cancelCommandHistorySearch()
{
    std::string restored = commandHistorySearchOriginal;
    commandHistorySearchActive = false;
    commandHistorySearchQueryValue.clear();
    commandHistorySearchOriginal.clear();
    commandHistorySearchMatches.clear();
    commandHistorySearchCursor = 0;
    commandHistorySearchOffset = 0;
    needsFullRedraw = true;
    return restored;
}

std::string Editor::acceptCommandHistorySearch()
{
    std::string selected;
    if(!commandHistorySearchMatches.empty() &&
       commandHistorySearchCursor >= 0 &&
       commandHistorySearchCursor < (int)commandHistorySearchMatches.size())
    {
        int idx = commandHistorySearchMatches[commandHistorySearchCursor];
        if(idx >= 0 && idx < (int)commandHistory.size())
            selected = commandHistory[idx];
    }
    if(selected.empty())
        selected = commandHistorySearchQueryValue;

    commandHistorySearchActive = false;
    commandHistorySearchQueryValue.clear();
    commandHistorySearchOriginal.clear();
    commandHistorySearchMatches.clear();
    commandHistorySearchCursor = 0;
    commandHistorySearchOffset = 0;
    needsFullRedraw = true;
    return selected;
}

void Editor::updateCommandHistorySearchQuery(std::string_view query)
{
    commandHistorySearchQueryValue = std::string(query);
    commandHistorySearchMatches.clear();
    commandHistorySearchCursor = 0;
    commandHistorySearchOffset = 0;

    if(commandHistory.empty())
    {
        needsFullRedraw = true;
        return;
    }

    if(commandHistorySearchQueryValue.empty())
    {
        for(int i = (int)commandHistory.size() - 1; i >= 0; --i)
            commandHistorySearchMatches.push_back(i);
        needsFullRedraw = true;
        return;
    }

    std::vector<std::pair<int, int>> scored;
    scored.reserve(commandHistory.size());
    std::vector<int> positions;

    for(int i = 0; i < (int)commandHistory.size(); ++i)
    {
        int score = fuzzyScore(commandHistorySearchQueryValue,
                               commandHistory[i], positions);
        if(score >= 0)
            scored.emplace_back(i, score);
    }

    if(!scored.empty())
    {
        std::stable_sort(scored.begin(), scored.end(),
                         [](const std::pair<int, int>& left,
                            const std::pair<int, int>& right)
                         {
                             if(left.second != right.second)
                                 return left.second > right.second;
                             return left.first > right.first;
                         });
        for(const auto& entry : scored)
            commandHistorySearchMatches.push_back(entry.first);
    }

    needsFullRedraw = true;
}

void Editor::moveCommandHistorySearchCursor(int delta)
{
    if(!commandHistorySearchActive || commandHistorySearchMatches.empty())
        return;
    int next = commandHistorySearchCursor + delta;
    if(next < 0)
        next = 0;
    if(next >= (int)commandHistorySearchMatches.size())
        next = (int)commandHistorySearchMatches.size() - 1;
    commandHistorySearchCursor = next;

    const int window = std::min(8, (int)commandHistorySearchMatches.size());
    if(commandHistorySearchCursor < commandHistorySearchOffset)
        commandHistorySearchOffset = commandHistorySearchCursor;
    else if(commandHistorySearchCursor >= commandHistorySearchOffset + window)
        commandHistorySearchOffset = commandHistorySearchCursor - window + 1;

    needsFullRedraw = true;
}

bool Editor::isCommandHistorySearchActive() const
{
    return commandHistorySearchActive;
}

const std::string& Editor::commandHistorySearchQuery() const
{
    return commandHistorySearchQueryValue;
}

void Editor::drawCommandPopup(std::string& output) const
{
    if(!commandPopupActive)
        return;
    if(commandHistorySearchActive)
        return;
    widgets::CommandPopupView view{
        .theme = theme,
        .screenRows = screenRows,
        .screenCols = screenCols,
        .entries = commandPopupAll,
        .filtered = commandPopupFiltered,
        .offset = commandPopupOffset,
        .cursor = commandPopupCursor,
    };
    widgets::drawCommandPopup(output, view);
}

void Editor::drawCommandHistoryPopup(std::string& output) const
{
    if(!commandHistorySearchActive)
        return;
    widgets::CommandHistoryPopupView view{
        .theme = theme,
        .screenRows = screenRows,
        .screenCols = screenCols,
        .history = commandHistory,
        .matches = commandHistorySearchMatches,
        .offset = commandHistorySearchOffset,
        .cursor = commandHistorySearchCursor,
    };
    widgets::drawCommandHistoryPopup(output, view);
}

std::vector<std::string> Editor::getCommandCompletions(std::string_view prefix)
{
    std::vector<std::string> commands = {
        "w",       "write",    "q",       "quit",   "q!",    "qa",
        "qall",    "qa!",      "qall!",   "wq",     "x",     "qw",
        "qw!",     "wa",       "wall",    "wa!",    "wqa",   "wqall",
        "wqa!",    "wqall!",   "xa",      "e",      "edit",  "new",
        "vnew",    "bn",       "bnext",   "bp",     "bprev", "bd",
        "bdelete", "ls",       "buffers", "sp",     "split", "vs",
        "vsplit",  "vh",       "hs",      "hsplit", "only",  "tabnew",
        "tabc",    "tabclose", "set",     "syntax", "noh",   "nohlsearch",
        "lspinfo", "emoji",    "em",      "help",   "h"};

    std::vector<std::string> matches;
    for(const auto& cmd : commands)
    {
        if(prefix.size() <= cmd.size() &&
           std::string_view(cmd).substr(0, prefix.size()) == prefix)
        {
            matches.push_back(cmd);
        }
    }
    return matches;
}

std::vector<std::string> Editor::getHelpCompletions(std::string_view prefix)
{
    static const std::vector<std::string> topics = {
        "commands", "modes",  "navigation", "editing", "files",
        "buffers",  "search", "clipboard",  "git",     "help"};

    std::vector<std::string> matches;
    for(const auto& topic : topics)
    {
        if(prefix.size() <= topic.size() &&
           std::string_view(topic).substr(0, prefix.size()) == prefix)
        {
            matches.push_back(topic);
        }
    }
    return matches;
}

std::vector<std::string> Editor::getSetCompletions(std::string_view prefix)
{
    static const std::vector<std::string> options = {
        "set autobraces",
        "set noautobraces",
        "set autobraces?",
        "set autobraces=",
        "set autoquotes",
        "set noautoquotes",
        "set autoquotes?",
        "set autoquotes=",
        "set autobracesinstrings",
        "set noautobracesinstrings",
        "set autobracesinstrings?",
        "set autobracesinstrings=",
        "set autocomplete",
        "set noautocomplete",
        "set autocomplete?",
        "set autocomplete=",
        "set completionautoparens",
        "set nocompletionautoparens",
        "set completionautoparens?",
        "set completionautoparens=",
        "set showtabs",
        "set noshowtabs",
        "set showtabs?",
        "set showtabs=",
        "set tabspaces?",
        "set tabspaces=",
        "set tabspaces=2",
        "set tabspaces=4",
        "set tabspaces=8",
        "set tabspaces=1",
        "set tabspaces=3",
        "set tabspaces=5",
        "set tabspaces=6",
        "set tabspaces=7",
        "set tabspaces=9",
        "set tabspaces=10",
        "set tabspaces=12",
        "set tabspaces=16",
        "set commenttogglepartial",
        "set nocommenttogglepartial",
        "set commenttogglepartial?",
        "set gdcenter",
        "set nogdcenter",
        "set gdcenter?",
        "set gdcenter=",
        "set formatoninsertleave",
        "set noformatoninsertleave",
        "set formatoninsertleave?",
        "set formatonsave",
        "set noformatonsave",
        "set formatonsave?",
        "set formatonsave=",
        "set formatondoubleesctimeoutms?",
        "set formatondoubleesctimeoutms=",
        "set gitdefaultcolors?",
        "set enablegitdefaultcolors",
        "set disablegitdefaultcolors",
        "set gitblameinfo?",
        "set gitblameinfo",
        "set nogitblameinfo",
        "set syntax.cpp.highlight_system_includes",
        "set nosyntax.cpp.highlight_system_includes",
        "set syntax.cpp.highlight_system_includes?",
        "set syntax.cpp.highlight_param_types",
        "set nosyntax.cpp.highlight_param_types",
        "set syntax.cpp.highlight_param_types?",
        "set python.formatter?",
        "set python.formatter=ruff",
        "set python.formatter=black",
        "set pyfmt=ruff",
        "set pyfmt=black",
        "set pyfmt?",
        "set utf8",
        "set noutf8",
        "set utf8?",
        "set utf8=",
    };

    std::vector<std::string> matches;
    for(const auto& opt : options)
    {
        if(prefix.size() <= opt.size() &&
           std::string_view(opt).substr(0, prefix.size()) == prefix)
        {
            matches.push_back(opt);
        }
    }
    return matches;
}

std::vector<std::string> Editor::getPathCompletions(std::string_view path)
{
    return ::getPathCompletions(path);
}

void Editor::deleteFilePrompt()
{
    setStatusMessage("File deletion not yet implemented");
}

void Editor::renameFilePrompt()
{
    setStatusMessage("File rename not yet implemented");
}

void Editor::createNewFilePrompt()
{
    setStatusMessage("New file creation not yet implemented");
}

void Editor::createNewDirectoryPrompt()
{
    setStatusMessage("New directory creation not yet implemented");
}

// ============================================================================
// Completion Helpers (for insert mode)
// ============================================================================

bool Editor::shouldTriggerCompletion()
{
    // Check if we should auto-trigger completion
    // Typically after typing an identifier character
    if(*cursorY >= (int)lines->size())
        return false;
    const std::string& line = (*lines)[*cursorY];
    if(*cursorX == 0)
        return false;

    char prevChar = line[*cursorX - 1];
    return text_utils::isIdent(prevChar) || prevChar == '-' || prevChar == '.';
}

void Editor::triggerCompletion()
{
    requestCompletion();
}

void Editor::nextCompletion()
{
    completionNext();
}

void Editor::previousCompletion()
{
    completionPrev();
}

#ifdef UVIM_ENABLE_CLANGD_LSP
static int utf16ToUtf8ByteOffset(const std::string& line, int utf16Offset)
{
    if(utf16Offset <= 0)
        return 0;

    int u16 = 0;
    int i = 0;
    while(i < (int)line.size() && u16 < utf16Offset)
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

bool Editor::mlangFormatBuffer()
{
    if(!currentBuffer || !lines)
        return false;
    if(!isFileType<FileType::Mla>())
        return false;
    if(!isMlangLspEnabled() || !mlangLspClient)
    {
        setStatusMessage("mlang LSP: OFF");
        return false;
    }

    std::string text;
    text.reserve(lines->size() * 80);
    for(size_t i = 0; i < lines->size(); ++i)
    {
        text += (*lines)[i];
        if(i + 1 < lines->size())
            text.push_back('\n');
    }
    mlangLspClient->didChange(currentBuffer->filename, text, "mlang");
    mlangLspClient->didChange(currentBuffer->filename, text, "mlang");

    std::vector<LspClient::TextEdit> edits =
        mlangLspClient->formatting(currentBuffer->filename, 4, true);
    if(edits.empty())
    {
        setStatusMessage("format: no changes");
        return true;
    }

    std::sort(edits.begin(), edits.end(),
              [](const LspClient::TextEdit& a, const LspClient::TextEdit& b)
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
            if(next == std::string::npos)
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
    needsFullRedraw = true;
    setStatusMessage("mlang: formatted buffer");
    return true;
}
#else
bool Editor::mlangFormatBuffer()
{
    setStatusMessage("mlang LSP: not compiled");
    return false;
}
#endif

// ============================================================================
// Compatibility Aliases
// ============================================================================

void Editor::deleteToEndOfLine()
{
    deleteToLineEnd();
}

void Editor::switchToAlternateFile()
{
    jumpToAlternateFile();
}
