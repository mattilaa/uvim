#include "editor.h"
#include "log.h"
#include "mode_state_machine.h"
#include "stdlib_goto.h"
#include "terminal.h"
#include "text_utils.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif
#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <csignal>
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
#include <vector>

namespace fs = std::filesystem;

static mla::log::FileLogger LOG("uvim");

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
        }
    }

    // No buffers on start unless files are explicitly opened.
    (void)skipInitialBuffer;

#if defined(UVIM_TERMINAL_POSIX)
    std::signal(SIGWINCH, handle_sigwinch);
#endif

    modeStateMachine =
        std::make_unique<ModeStateMachine>(createModeContext(this));
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

    // Determine project root (cwd at startup is good enough for uvim).
    char cwd[PATH_MAX];
    std::string rootDir = ".";
    if(getcwd(cwd, sizeof(cwd)))
        rootDir = std::string(cwd);

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
    setStatusMessage("clangd LSP: ON" +
                     (ccdir.empty() ? "" : (" (ccdir=" + ccdir + ")")));
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

    char cwd[PATH_MAX];
    std::string rootDir = ".";
    if(getcwd(cwd, sizeof(cwd)))
        rootDir = std::string(cwd);

    std::vector<std::string> args = this->robotLspArgs;
    if(args.empty())
    {
        args.push_back("--stdio");
    }

    robotLspClient = std::make_unique<LspClient>();
    if(!robotLspClient->startServer(this->robotLspPath, rootDir, args))
    {
        robotLspClient.reset();
        setStatusMessage("robot LSP: failed to start");
        return;
    }

    robotLspEnabled = true;
    setStatusMessage("robot LSP: ON");
#else
    (void)enable;
    (void)robotLspPath;
    (void)robotLspArgs;
    setStatusMessage("robot LSP: not compiled in");
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

    char cwd[PATH_MAX];
    std::string rootDir = ".";
    if(getcwd(cwd, sizeof(cwd)))
        rootDir = std::string(cwd);

    std::vector<std::string> args = this->pythonLspArgs;
    if(args.empty())
    {
        args.push_back("--stdio");
    }

    pythonLspClient = std::make_unique<LspClient>();
    if(!pythonLspClient->startServer(this->pythonLspPath, rootDir, args))
    {
        pythonLspClient.reset();

        LOG_ERROR(LOG, "Python LSP failed to start. Pyhtonh path: {}", this->pythonLspPath);
        // setStatusMessage("python LSP: failed to start");
        return;
    }

    pythonLspEnabled = true;
   // setStatusMessage("python LSP: ON");
#else
    (void)enable;
    (void)pythonLspPath;
    (void)pythonLspArgs;
    //setStatusMessage("python LSP: not compiled in");
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

void Editor::handleOperatorPendingMode(int c)
{
    if(dispatchModeKey(c))
    {
        return;
    }

    // If user pressed ESC, cancel
    if(c == Terminal::ESC)
    {
        setMode(NORMAL);
        setStatusMessage("");
        return;
    }

    // If user pressed a digit while building a count (rare), ignore here for
    // simplicity Support 'i' or 'a' to enter object-specifier substate
    if(!pendingAwaitingObject && (c == 'i' || c == 'a'))
    {
        pendingAwaitingObject = true;
        pendingObjectType = (char)c;
        setStatusMessage(std::string("Operator: ") + pendingOperator + " " +
                         pendingObjectType);
        return;
    }

    int startY, startX, endY, endX;
    bool rangeFound = false;

    if(pendingAwaitingObject)
    {
        // Expect a text-object specifier now (e.g. '(', '{', '"', 'w', etc.)
        char obj = (char)c;
        bool around = (pendingObjectType == 'a');
        rangeFound =
            getTextObjectRange(obj, around, startY, startX, endY, endX);
    }
    else
    {
        // Motion-based operator: treat c as a motion (w, b, e, $, 0, %, etc.)
        // We'll simulate the motion by saving cursor, doing it, reading
        // destination, then restoring.
        int saveX = *cursorX, saveY = *cursorY, saveWanted = *wantedX,
            saveOffsetY = *offsetY, saveOffsetX = *offsetX;

        bool isExclusiveMotion = false; // Track if motion should be exclusive

        // Apply motion
        switch(c)
        {
        case 'w':
        {
            // For dw/cw: delete from cursor to start of next word (exclusive)
            // This is vim's behavior: delete current word + trailing whitespace
            // but stay on the same line
            const std::string& line = (*lines)[*cursorY];
            int end = *cursorX;

            // Helper lambda for word character check
            auto isWordChar = [](char ch)
            {
                return std::isalnum(static_cast<unsigned char>(ch)) ||
                       ch == '_';
            };

            if(end < (int)line.length())
            {
                char startChar = line[end];

                if(std::isspace(static_cast<unsigned char>(startChar)))
                {
                    // On whitespace: skip whitespace then skip next word/punct
                    while(end < (int)line.length() &&
                          std::isspace(static_cast<unsigned char>(line[end])))
                        end++;

                    if(end < (int)line.length())
                    {
                        if(isWordChar(line[end]))
                        {
                            while(end < (int)line.length() &&
                                  isWordChar(line[end]))
                                end++;
                        }
                        else
                        {
                            while(end < (int)line.length() &&
                                  !isWordChar(line[end]) &&
                                  !std::isspace(
                                      static_cast<unsigned char>(line[end])))
                                end++;
                        }
                    }
                }
                else if(isWordChar(startChar))
                {
                    // On word: skip word + trailing whitespace
                    while(end < (int)line.length() && isWordChar(line[end]))
                        end++;
                    while(end < (int)line.length() &&
                          std::isspace(static_cast<unsigned char>(line[end])))
                        end++;
                }
                else
                {
                    // On punctuation: skip punctuation + trailing whitespace
                    while(end < (int)line.length() && !isWordChar(line[end]) &&
                          !std::isspace(static_cast<unsigned char>(line[end])))
                        end++;
                    while(end < (int)line.length() &&
                          std::isspace(static_cast<unsigned char>(line[end])))
                        end++;
                }
            }

            // Set destination
            *cursorX = end;
            // 'w' is exclusive, so we'll subtract 1 later
            isExclusiveMotion = true;
        }
        break;
        case 'b':
            moveWordBackward();
            isExclusiveMotion = true; // 'b' is exclusive in vim
            break;
        case 'e':
            moveToEndOfWord();
            isExclusiveMotion = false; // 'e' is inclusive in vim
            break;
        case '0':
            moveToLineStart();
            break;
        case '$':
            moveToLineEnd();
            break;
        case '%':
            moveToMatchingBracket();
            break;
        case 'j':
            moveDown(pendingCount);
            break;
        case 'k':
            moveUp(pendingCount);
            break;
        case 'G':
            // Move to last line or specific line if count given
            if(pendingCount > 0)
                moveToLine(pendingCount - 1);
            else
                moveToLastLine();
            break;
        case 'g':
            // For gg motion - need to read next char
            {
                int nextChar = Terminal::readKey();
                if(nextChar == 'g')
                {
                    moveToFirstLine();
                }
                else
                {
                    // Unsupported gg variant
                    setStatusMessage("Unknown motion for operator");
                    setMode(NORMAL);
                    *cursorX = saveX;
                    *cursorY = saveY;
                    *wantedX = saveWanted;
                    *offsetY = saveOffsetY;
                    *offsetX = saveOffsetX;
                    return;
                }
            }
            break;
        case '{':
            // Move to beginning of paragraph (previous blank line)
            {
                int targetY = *cursorY;
                // Skip current paragraph
                while(targetY > 0 && !(*lines)[targetY].empty())
                    targetY--;
                // Skip blank lines
                while(targetY > 0 && (*lines)[targetY].empty())
                    targetY--;
                // Find beginning of previous paragraph
                while(targetY > 0 && !(*lines)[targetY - 1].empty())
                    targetY--;
                *cursorY = targetY;
                *cursorX = 0;
            }
            break;
        case '}':
            // Move to end of paragraph (next blank line)
            {
                int targetY = *cursorY;
                int maxLine = lines->size() - 1;
                // Skip current paragraph
                while(targetY < maxLine && !(*lines)[targetY].empty())
                    targetY++;
                // Skip blank lines
                while(targetY < maxLine && (*lines)[targetY].empty())
                    targetY++;
                *cursorY = targetY;
                *cursorX = 0;
            }
            break;
        default:
            // unsupported motion -> cancel operator
            setStatusMessage("Unknown motion for operator");
            setMode(NORMAL);
            // restore
            *cursorX = saveX;
            *cursorY = saveY;
            *wantedX = saveWanted;
            *offsetY = saveOffsetY;
            *offsetX = saveOffsetX;
            return;
        }

        // get destination
        int destX = *cursorX, destY = *cursorY;
        // restore original cursor
        *cursorX = saveX;
        *cursorY = saveY;
        *wantedX = saveWanted;
        *offsetY = saveOffsetY;
        *offsetX = saveOffsetX;

        // compute range between original and dest
        if(saveY < destY || (saveY == destY && saveX <= destX))
        {
            // Forward motion
            startY = saveY;
            startX = saveX;
            endY = destY;
            endX = destX;

            // For exclusive forward motions, don't include the character at
            // destination
            if(isExclusiveMotion)
            {
                // Make the range exclusive by moving end back one position
                if(endX > 0)
                {
                    endX--;
                }
                else if(endY > 0)
                {
                    // If at start of line, go to end of previous line
                    endY--;
                    endX = (*lines)[endY].length() - 1;
                }
            }
        }
        else
        {
            // Backward motion
            startY = destY;
            startX = destX;
            endY = saveY;
            endX = saveX;

            // For exclusive backward motions, adjust similarly
            if(isExclusiveMotion)
            {
                // The range is already correct for backward exclusive motions
                // because we want to delete from dest to just before current
            }
        }

        rangeFound = true;
    }

    if(!rangeFound)
    {
        setStatusMessage("No object found");
        setMode(NORMAL);
        return;
    }

    // Apply operator
    char op = pendingOperator;

    // DEBUG: Write to file what we're about to delete
    {
        std::ofstream dbg("/tmp/uvim_dw_debug.txt", std::ios::app);
        dbg << "=== dw operation ===" << std::endl;
        dbg << "op=" << op << std::endl;
        dbg << "startY=" << startY << " startX=" << startX << std::endl;
        dbg << "endY=" << endY << " endX=" << endX << std::endl;
        if(startY < (int)lines->size())
        {
            dbg << "line[" << startY << "]=" << (*lines)[startY] << std::endl;
            dbg << "deleting chars " << startX << " to " << endX << std::endl;
            if(startY == endY && startX < (int)(*lines)[startY].length())
            {
                dbg << "text to delete: ["
                    << (*lines)[startY].substr(startX, endX - startX + 1) << "]"
                    << std::endl;
            }
        }
        dbg << std::endl;
    }

    applyOperatorToRange(op, startY, startX, endY, endX);

    // Clear state
    pendingOperator = 0;
    pendingAwaitingObject = false;
    pendingObjectType = 0;
    pendingCount = 0;

    // Return to NORMAL unless operator was 'c' (which already set INSERT mode)
    if(op != 'c')
    {
        setMode(NORMAL);
    }
    needsFullRedraw = true;
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
        setMode(INSERT);
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

    // Reset undo state cleanly
    currentBuffer->undoStack.clear();
    currentBuffer->undoIndex = -1;
    saveState();
    currentBuffer->savedUndoIndex = currentBuffer->undoIndex;

    needsFullRedraw = true;

#ifdef UVIM_ENABLE_CLANGD_LSP
    // Notify LSP about the newly opened file so gd works from system headers
    if(isClangdLspEnabled() && isCppFile() && lspClient)
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
    }
    if(isRobotLspEnabled() && isRobotFile() && robotLspClient)
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
    if(isPythonLspEnabled() && isPythonFile() && pythonLspClient)
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

void Editor::saveFile()
{
    if(filename->empty())
    {
        setStatusMessage("No file name");
        return;
    }

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
    if(isRobotLspEnabled() && isRobotFile())
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

            centerScreen();
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
            centerScreen();
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
                centerScreen();
                setStatusMessage("gd (robot) → " + p.string() + ":" +
                                 std::to_string(defY + 1));
                return;
            }
        }

        setStatusMessage("gd (robot): not found");
        return;
    }

    if(isPythonLspEnabled() && isPythonFile())
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

            centerScreen();
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
            centerScreen();
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
            std::string ext = p.extension().string();
            if(ext != ".py" && ext != ".pyi" && ext != ".pyw")
                continue;
            if(find_python_def_in_file(p.string(), symbol, defY, defX))
            {
                pushJumpLocation();
                openFile(p.string());
                *cursorY = defY;
                *cursorX = defX;
                centerScreen();
                setStatusMessage("gd (python) → " + p.string() + ":" +
                                 std::to_string(defY + 1));
                return;
            }
        }

        setStatusMessage("gd (python): not found");
        return;
    }

    // Prefer clangd definition when enabled; fallback to heuristic gd
    // otherwise.
    if(isClangdLspEnabled() && isCppFile())
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

            centerScreen();

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
            centerScreen();
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
            centerScreen();
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
            centerScreen();
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
        centerScreen();
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

    static int lastOffsetY = -1;
    static int lastOffsetX = -1;
    static Mode lastMode = NORMAL;
    static int lastVisualStartY = -1;
    static int lastVisualEndY = -1;

    int prevOffsetY = lastOffsetY;
    adjustViewport();

    bool scrolled = (*offsetY != lastOffsetY || *offsetX != lastOffsetX);
    bool modeChanged = (currentMode != lastMode);
    int scrollDelta = *offsetY - lastOffsetY;

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
       isEditingMode)
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
        cursorRow = (*cursorY - *offsetY) + 1;
        cursorCol = (*cursorX - *offsetX) + 1;
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

    auto set_flag = [&](bool value)
    {
        autoBraces = value;
        setStatusMessage(std::string("autobraces=") +
                         (autoBraces ? "true" : "false"));
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

    auto set_auto_completion = [&](bool value)
    {
        autoCompletion = value;
        setStatusMessage(std::string("autocomplete=") +
                         (autoCompletion ? "true" : "false"));
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
    if(handleSetCommand(cmd))
        return;

    if(cmd == "lspinfo")
    {
        showLspInfo();
        commandRequestedModeSet = true;
        commandRequestedMode = LSP_INFO;
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
    else if(cmd == "q")
    {
        if(*dirty)
        {
            setStatusMessage("No write since last change (add ! to override)");
        }
        else if(buffers.size() > 1)
        {
            closeCurrentBuffer();
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
    return true;
}

void Editor::syncModeFromStateMachine()
{
    if(!modeStateMachine)
    {
        return;
    }

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
        cursorX = &currentBuffer->cursorX;
        cursorY = &currentBuffer->cursorY;
        wantedX = &currentBuffer->wantedX;
        offsetX = &currentBuffer->offsetX;
        offsetY = &currentBuffer->offsetY;
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
        saveBufferState();
        currentBufferIndex = index;
        updateCurrentBufferPointers();
        restoreBufferState();
        needsFullRedraw = true;

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
        setMode(WELCOME);
    }
    else
    {
        buffers.erase(buffers.begin() + currentBufferIndex);
        if(currentBufferIndex >= buffers.size())
        {
            currentBufferIndex = buffers.size() - 1;
        }
        updateCurrentBufferPointers();
        restoreBufferState();
    }

    needsFullRedraw = true;
    setStatusMessage("Buffer closed");
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
void Editor::run()
{
    //    setStatusMessage("Welcome to uVim!");

    while(true)
    {
        handleResize();
        draw();
        int c = Terminal::readKeyTimeout(50);
        if(c < 0)
            continue;
        handleKeypress(c);
    }
}

void Editor::handleNormalMode(int c)
{
    LOG_DEBUG(LOG, "handleNormalMode c={} ('{}') commandBuffer='{}'", c,
              (char)c, commandBuffer);

    if(dispatchModeKey(c))
    {
        return;
    }

    static bool pendingDelete = false;
    static bool pendingYank = false;
    static bool pendingIndent = false;
    static bool pendingShiftRight = false;
    static bool pendingShiftLeft = false;

    // ----- single-character replace (vim/neovim-style 'r{char}') -----
    if(c == 'r')
    {
        // Cancel any pending operators
        pendingDelete = pendingYank = pendingIndent = false;

        int rc = Terminal::readKey();

        // Only accept printable characters
        if(rc < 32 || rc == 127)
            return;

        if(!lines || lines->empty())
            return;

        if(*cursorY < 0 || *cursorY >= (int)lines->size())
            return;

        std::string& line = (*lines)[*cursorY];
        if(*cursorX < 0 || *cursorX >= (int)line.size())
            return;

        line[*cursorX] = (char)rc;

        saveState(); // your undo model saves *after* changes
        *dirty = true;
        needsFullRedraw =
            true; // IMPORTANT: otherwise NORMAL mode may not redraw text
        return;
    }

    if(c >= '1' && c <= '9' && repeatCount == 0 && commandBuffer.empty())
    {
        repeatCount = c - '0';
        return;
    }
    else if(c >= '0' && c <= '9' && repeatCount > 0)
    {
        repeatCount = repeatCount * 10 + (c - '0');
        return;
    }
    int count = std::max(1, repeatCount);

    // ----- Leader (space) prefixed commands (MUST be early) -----
    if(commandBuffer == " ")
    {
        if(c == 'h')
        {
            // Leader + h: jump to alternate file (header/source)
            jumpToAlternateFile();
            commandBuffer.clear();
            repeatCount = 0;
            return;
        }
        else if(c == 'b')
        {
            // Leader + b: start buffer command sequence
            commandBuffer = " b";
            setStatusMessage("Leader-b");
            repeatCount = 0;
            return;
        }
        else if(c == 'y')
        {
            // Leader + y: yank to system clipboard
            yankToSystemClipboard();
            commandBuffer.clear();
            repeatCount = 0;
            return;
        }
        else if(c == 'p')
        {
            // Leader + p: paste from system clipboard
            pasteFromSystemClipboard();
            commandBuffer.clear();
            repeatCount = 0;
            return;
        }
        else if(c == 'f')
        {
            // Leader + f: format file with clang-format
            commandBuffer.clear();
            repeatCount = 0;

            LOG_DEBUG(LOG, "Leader-f pressed. filename='{}' isCppFile={}",
                      *filename, isCppFile());

            if(!isCppFile() && !isHeaderFile(*filename))
            {
                setStatusMessage("clang-format: not a C/C++ file (" +
                                 *filename + ")");
                return;
            }

            // Save current cursor position
            int savedY = *cursorY;
            int savedX = *cursorX;

            // Write buffer to temp file
            std::string tempPath =
                "/tmp/uvim_format_" + std::to_string(getpid()) + ".tmp";
            std::ofstream tempFile(tempPath);
            if(!tempFile.is_open())
            {
                setStatusMessage("clang-format: failed to create temp file");
                return;
            }

            // Write content with trailing newline
            for(size_t i = 0; i < lines->size(); ++i)
            {
                tempFile << (*lines)[i] << '\n';
            }
            tempFile.close();

            // Get the directory of the current file for clang-format to find
            // .clang-format
            std::string fileDir = ".";
            std::string absFilename = *filename;

            // Make filename absolute if it isn't
            if(!absFilename.empty() && absFilename[0] != '/')
            {
                char cwd[PATH_MAX];
                if(getcwd(cwd, sizeof(cwd)))
                {
                    absFilename = std::string(cwd) + "/" + *filename;
                }
            }

            // Run clang-format with stdin, using actual filename for style
            // lookup clang-format searches for .clang-format starting from the
            // file's directory
            auto is_exec = [](const std::string& p) -> bool
            { return ::access(p.c_str(), X_OK) == 0; };

            std::string clangFormatExe;
            {
#ifdef __APPLE__
                const std::vector<std::string> candidates = {
                    "/opt/homebrew/bin/clang-format",
                    "/opt/homebrew/opt/llvm/bin/clang-format",
                    "/usr/local/bin/clang-format",
                    "/usr/local/opt/llvm/bin/clang-format",
                    "/usr/bin/clang-format",
                };
#else
                const std::vector<std::string> candidates = {
                    "/usr/bin/clang-format",
                    "/usr/local/bin/clang-format",
                };
#endif
                for(const auto& c : candidates)
                {
                    if(is_exec(c))
                    {
                        clangFormatExe = c;
                        break;
                    }
                }
            }

            if(clangFormatExe.empty())
                clangFormatExe = "clang-format"; // fall back to PATH lookup

            // Run clang-format using stdin redirection (avoid piping via
            // `cat`).
            std::string cmd = "\"" + clangFormatExe +
                              "\" -style=file"
                              " -assume-filename=\"" +
                              absFilename +
                              "\""
                              " < \"" +
                              tempPath +
                              "\""
                              " 2>/tmp/uvim_clang_err.log";
            LOG_DEBUG(LOG, "Temp file written: {} lines={}", tempPath,
                      lines->size());
            LOG_DEBUG(LOG, "Running: {}", cmd);

            FILE* pipe = popen(cmd.c_str(), "r");
            if(!pipe)
            {
                unlink(tempPath.c_str());
                setStatusMessage("clang-format: failed to run");
                return;
            }

            // Read formatted output
            std::string formatted;
            char buffer[4096];
            while(fgets(buffer, sizeof(buffer), pipe))
            {
                formatted += buffer;
            }
            int status = pclose(pipe);
            unlink(tempPath.c_str());

            LOG_DEBUG(LOG, "pclose status={} formatted.size()={}", status,
                      formatted.size());

            // Check for errors
            if(formatted.empty())
            {
                // Read error file
                std::ifstream errFile("/tmp/uvim_clang_err.log");
                std::string errMsg;
                if(errFile.is_open())
                {
                    std::getline(errFile, errMsg);
                    errFile.close();
                }
                if(errMsg.empty())
                    errMsg = "no output (exit=" +
                             std::to_string(WEXITSTATUS(status)) + ")";
                setStatusMessage("clang-format: " + errMsg.substr(0, 50));
                return;
            }

            // Parse formatted output into lines
            std::vector<std::string> newLines;
            std::istringstream iss(formatted);
            std::string line;
            while(std::getline(iss, line))
            {
                // Remove \r if present
                if(!line.empty() && line.back() == '\r')
                    line.pop_back();
                newLines.push_back(line);
            }

            // Remove trailing empty line if present (clang-format adds one)
            if(!newLines.empty() && newLines.back().empty())
            {
                newLines.pop_back();
            }

            // Ensure at least one line
            if(newLines.empty())
            {
                newLines.push_back("");
            }

            // Check if anything changed
            if(newLines == *lines)
            {
                setStatusMessage("clang-format: no changes needed");
                return;
            }

            // Replace buffer content
            *lines = newLines;
            *dirty = true;

            // Restore cursor position (clamped to valid range)
            if(lines->empty())
            {
                *cursorY = 0;
                *cursorX = 0;
            }
            else
            {
                *cursorY = savedY;
                if(*cursorY >= (int)lines->size())
                    *cursorY = (int)lines->size() - 1;
                if(*cursorY < 0)
                    *cursorY = 0;

                *cursorX = savedX;
                int lineLen = (int)(*lines)[*cursorY].length();
                if(*cursorX > lineLen)
                    *cursorX = lineLen > 0 ? lineLen - 1 : 0;
                if(*cursorX < 0)
                    *cursorX = 0;
            }

            // Save state for undo
            saveState();

            adjustViewport();
            needsFullRedraw = true;
            setStatusMessage("clang-format: formatted " +
                             std::to_string(lines->size()) + " lines");
            return;
        }
        else if(c == 'x')
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
            openFileBrowser(dir);
            return;
        }
        else if(c == ' ')
        {
            // Double space cancels
            commandBuffer.clear();
            setStatusMessage("");
            repeatCount = 0;
            return;
        }
        else
        {
            // Unknown leader command - cancel
            commandBuffer.clear();
            setStatusMessage("");
        }
    }

    // ----- Leader-b (buffer) commands -----
    if(commandBuffer == " b")
    {
        if(c == 'd')
        {
            // Leader + bd: close current buffer
            commandBuffer.clear();
            closeCurrentBuffer();
            repeatCount = 0;
            return;
        }
        else
        {
            // Unknown buffer command - cancel
            commandBuffer.clear();
            setStatusMessage("");
        }
    }

    // ----- g-prefixed commands (MUST be first) -----
    if(commandBuffer == "g")
    {
        if(c == 'd')
        {
            goToDefinition();
            repeatCount = 0;
            return;
        }
        else if(c == 'g')
        {
            moveToFirstLine();
            commandBuffer.clear();
            repeatCount = 0;
            return;
        }
        else
        {
            // Unknown g-command → cancel
            commandBuffer.clear();
        }
    }
    else if(c == 'd')
    {
        if(pendingDelete)
        {
            // dd detected
            for(int i = 0; i < count; i++)
            {
                deleteLine();
            }
            saveState();
            setStatusMessage(std::to_string(count) + " line(s) deleted");
            pendingDelete = false;
            repeatCount = 0;
            return;
        }
        else
        {
            // first 'd'
            pendingDelete = true;
            pendingYank = false;   // Cancel any pending yank
            pendingIndent = false; // Cancel any pending indent
            return;
        }
    }
    else if(c == 'y')
    {
        if(pendingYank)
        {
            // yy detected - yank multiple lines
            yankBuffer.clear();
            int startLine = *cursorY;
            int endLine =
                std::min(startLine + count - 1, (int)lines->size() - 1);

            for(int i = startLine; i <= endLine; i++)
            {
                yankBuffer += (*lines)[i] + "\n";
            }

            int linesYanked = endLine - startLine + 1;
            setStatusMessage(std::to_string(linesYanked) + " line" +
                             (linesYanked > 1 ? "s" : "") + " yanked");
            pendingYank = false;
            repeatCount = 0;
            return;
        }
        else
        {
            // first 'y'
            pendingYank = true;
            pendingDelete = false; // Cancel any pending delete
            pendingIndent = false; // Cancel any pending indent
            return;
        }
    }
    else if(c == '=')
    {
        if(pendingIndent)
        {
            // == detected - indent current line(s)
            int startLine = *cursorY;
            int endLine =
                std::min(startLine + count - 1, (int)lines->size() - 1);

            autoIndentRange(startLine, endLine);

            int linesIndented = endLine - startLine + 1;
            setStatusMessage(std::to_string(linesIndented) + " line" +
                             (linesIndented > 1 ? "s" : "") + " indented");
            pendingIndent = false;
            repeatCount = 0;
            saveState();
            return;
        }
        else
        {
            // first '='
            pendingIndent = true;
            pendingDelete = false; // Cancel any pending delete
            pendingYank = false;   // Cancel any pending yank
            return;
        }
    }
    else if(c == '>')
    {
        if(pendingShiftRight)
        {
            // >> detected - shift right (increase indent)
            int startLine = *cursorY;
            int endLine =
                std::min(startLine + count - 1, (int)lines->size() - 1);

            for(int i = startLine; i <= endLine; i++)
            {
                int currentIndent = getLineIndent(i);
                indentLine(i, currentIndent + 4); // Add 4 spaces
            }

            int linesIndented = endLine - startLine + 1;
            setStatusMessage(std::to_string(linesIndented) + " line" +
                             (linesIndented > 1 ? "s" : "") + " >>");
            pendingShiftRight = false;
            repeatCount = 0;
            saveState();
            needsFullRedraw = true;
            return;
        }
        else
        {
            // first '>'
            pendingShiftRight = true;
            pendingShiftLeft = false;
            pendingDelete = false;
            pendingYank = false;
            pendingIndent = false;
            return;
        }
    }
    else if(c == '<')
    {
        if(pendingShiftLeft)
        {
            // << detected - shift left (decrease indent)
            int startLine = *cursorY;
            int endLine =
                std::min(startLine + count - 1, (int)lines->size() - 1);

            for(int i = startLine; i <= endLine; i++)
            {
                int currentIndent = getLineIndent(i);
                indentLine(i,
                           std::max(0, currentIndent - 4)); // Remove 4 spaces
            }

            int linesIndented = endLine - startLine + 1;
            setStatusMessage(std::to_string(linesIndented) + " line" +
                             (linesIndented > 1 ? "s" : "") + " <<");
            pendingShiftLeft = false;
            repeatCount = 0;
            saveState();
            needsFullRedraw = true;
            return;
        }
        else
        {
            // first '<'
            pendingShiftLeft = true;
            pendingShiftRight = false;
            pendingDelete = false;
            pendingYank = false;
            pendingIndent = false;
            return;
        }
    }
    else if(pendingYank && c != 'y')
    {
        // 'y' followed by motion command - enter operator-pending mode
        pendingYank = false;
        enterOperatorPending('y');
        pendingCount = count;
        // Process the motion command immediately
        handleOperatorPendingMode(c);
        repeatCount = 0;
        return;
    }
    else if(pendingDelete && c != 'd')
    {
        // 'd' followed by motion command - enter operator-pending mode
        pendingDelete = false;
        enterOperatorPending('d');
        pendingCount = count;
        // Process the motion command immediately
        handleOperatorPendingMode(c);
        repeatCount = 0;
        return;
    }
    else if(pendingIndent && c != '=')
    {
        // '=' followed by motion command - enter operator-pending mode
        pendingIndent = false;
        enterOperatorPending('=');
        pendingCount = count;
        // Process the motion command immediately
        handleOperatorPendingMode(c);
        repeatCount = 0;
        return;
    }
    else if(!pendingDelete && !pendingYank && !pendingIndent &&
            !pendingShiftRight && !pendingShiftLeft)
    {
        // Only reset if we're not in the middle of processing pending
        // operations
    }
    switch(c)
    {
    case Terminal::ESC:
    {
        // Handle double ESC to clear search highlights
        auto now = std::chrono::steady_clock::now();
        auto timeSinceLastEsc =
            std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                                  lastEscTime)
                .count();

        if(timeSinceLastEsc <= DOUBLE_ESC_TIMEOUT_MS &&
           (!searchMatches.empty() || !searchQuery.empty()))
        {
            // Double ESC detected - clear search highlights
            clearSearch();
            needsFullRedraw = true; // Force full redraw to clear highlights
            lastEscTime = std::chrono::steady_clock::time_point(); // Reset
        }
        else
        {
            // First ESC or timeout exceeded
            lastEscTime = now;
            // Clear any pending operations
            pendingDelete = false;
            pendingYank = false;
            pendingIndent = false;
            pendingShiftRight = false;
            pendingShiftLeft = false;
            repeatCount = 0;
            commandBuffer.clear();
        }
    }
    break;
    case 'i':
        saveState();
        setMode(INSERT);
        break;
    case 'I':
        saveState();
        moveToLineStart();
        setMode(INSERT);
        break;
    case 'a':
        saveState();
        if(*cursorX < (*lines)[*cursorY].length())
            (*cursorX)++;
        setMode(INSERT);
        break;
    case 'A':
        saveState();
        moveToLineEnd();
        if(*cursorX < (*lines)[*cursorY].length())
            (*cursorX)++;
        setMode(INSERT);
        break;
    case 'o':
    {
        // Get indentation from current line
        const std::string& currentLine = (*lines)[*cursorY];
        size_t indent = 0;
        while(indent < currentLine.length() &&
              (currentLine[indent] == ' ' || currentLine[indent] == '\t'))
        {
            indent++;
        }
        std::string indentStr = currentLine.substr(0, indent);

        // Check if current line ends with { (add extra indent)
        bool addExtraIndent = false;
        if(isCppFile())
        {
            size_t lastNonSpace = currentLine.find_last_not_of(" \t");
            if(lastNonSpace != std::string::npos &&
               currentLine[lastNonSpace] == '{')
            {
                addExtraIndent = true;
            }
        }

        // Insert new line below with proper indentation
        std::string newLine = indentStr;
        if(addExtraIndent)
        {
            newLine += "    ";
        }
        lines->insert(lines->begin() + *cursorY + 1, newLine);
        (*cursorY)++;
        *cursorX = newLine.length();
        *dirty = true;
        needsFullRedraw = true;
        setMode(INSERT);
        saveState();
        break;
    }
    case 'O':
    {
        // Get indentation from current line
        const std::string& currentLine = (*lines)[*cursorY];
        size_t indent = 0;
        while(indent < currentLine.length() &&
              (currentLine[indent] == ' ' || currentLine[indent] == '\t'))
        {
            indent++;
        }
        std::string indentStr = currentLine.substr(0, indent);

        // Insert new line above with same indentation
        lines->insert(lines->begin() + *cursorY, indentStr);
        *cursorX = indentStr.length();
        *dirty = true;
        needsFullRedraw = true;
        setMode(INSERT);
        saveState();
        break;
    }
    case 'v':
        startVisualMode();
        break;
    case 'V':
        startVisualLineMode();
        break;
    case 22: // Ctrl-V (ASCII 22)
        startVisualBlockMode();
        break;
    case ':':
        setMode(COMMAND);
        break;
    case '/':
        startSearchForward();
        break;
    case '?':
        startSearchBackward();
        break;
    case 'n':
        searchNext();
        break;
    case 'N':
        searchPrevious();
        break;
    case '#':
    {
        // Vim-style: search backward for the word under the cursor.
        // Anchor at the start of the current word so we don't match the same
        // occurrence when the cursor is inside the word.
        std::string sym = getSymbolUnderCursor();
        if(sym.empty())
        {
            setStatusMessage("#: no word under cursor");
            break;
        }

        // Move cursor to the start of the current identifier.
        if(*cursorY >= 0 && *cursorY < (int)lines->size())
        {
            const std::string& line = (*lines)[*cursorY];
            int x = *cursorX;
            if(x >= (int)line.size())
                x = (int)line.size() - 1;

            while(x > 0 && isIdent(line[x - 1]))
                --x;
            *cursorX = x;
        }

        searchQuery = sym;
        searchForward = false;
        performSearch();
        needsFullRedraw = true;
        *wantedX = *cursorX;
        break;
    }
    case 30: // Ctrl+^ (Ctrl+6)
        if(buffers.size() > 1)
        {
            previousBuffer();
        }
        break;
    case Terminal::CTRL_P:
        setMode(FUZZY_FIND);
        break;
    case Terminal::CTRL_W: // Ctrl+W for buffer browser
        setMode(BUFFER_BROWSER);
        break;
    case Terminal::CTRL_S: // Ctrl+S for grep search (find in files)
        setMode(GREP_SEARCH);
        break;
    case Terminal::CTRL_O:
        jumpBack();
        break;
    case Terminal::CTRL_I:
        jumpForward();
        break;
    case 'h':
        moveLeft(count);
        break;
    case Terminal::ARROW_LEFT:
        moveLeft(count);
        break;
    case 'l':
    case Terminal::ARROW_RIGHT:
        moveRight(count);
        break;
    case 'j':
    case Terminal::ARROW_DOWN:
        moveDown(count);
        break;
    case 'k':
    case Terminal::ARROW_UP:
        moveUp(count);
        break;
    case Terminal::CTRL_D:
        scrollHalfPageDown(false);
        break;
    case Terminal::CTRL_U:
        scrollHalfPageUp(false);
        break;
    case 'w':
        while(count-- > 0)
            moveWordForward();
        break;
    case 'b':
        while(count-- > 0)
            moveWordBackward();
        break;
    case 'e':
        while(count-- > 0)
            moveToEndOfWord();
        break;
    case '0':
        if(repeatCount == 0)
            moveToLineStart();
        break;
    case '$':
        moveToLineEnd();
        break;
    case 'g':
        commandBuffer = "g";
        return;
    case 'G':
        if(repeatCount > 0)
        {
            moveToLine(repeatCount - 1);
        }
        else
        {
            moveToLastLine();
        }
        break;
    case ' ': // Leader key (space)
        if(commandBuffer == " ")
        {
            commandBuffer.clear(); // Double space cancels
            setStatusMessage("");
            repeatCount = 0;
            return;
        }
        commandBuffer = " ";
        setStatusMessage("Leader");
        repeatCount = 0;
        return;
    case 'x':
        while(count-- > 0)
        {
            deleteCharForward();
        }
        saveState();
        break;
    case 's':
        // Substitute: delete char(s) under cursor and enter insert mode
        while(count-- > 0)
        {
            deleteCharForward();
        }
        saveState();
        setMode(INSERT);
        break;
    case 'D':
        deleteToLineEnd();
        saveState();
        needsFullRedraw = true;
        break;
    case 'Y':
        yankToLineEnd();
        break;

    case 'J':
    {
        // Join lines: current line with next line(s)
        // count specifies how many lines to join (default 2 = current + next)
        int linesToJoin = (repeatCount > 0) ? repeatCount : 2;
        int joinCount = 0;

        for(int i = 0; i < linesToJoin - 1; ++i)
        {
            if(*cursorY >= (int)lines->size() - 1)
                break; // No more lines to join

            std::string& currentLine = (*lines)[*cursorY];
            std::string& nextLine = (*lines)[*cursorY + 1];

            // Remove trailing whitespace from current line
            size_t endPos = currentLine.find_last_not_of(" \t");
            if(endPos != std::string::npos)
                currentLine = currentLine.substr(0, endPos + 1);

            // Remove leading whitespace from next line
            size_t startPos = nextLine.find_first_not_of(" \t");
            std::string trimmedNext = (startPos != std::string::npos)
                                          ? nextLine.substr(startPos)
                                          : "";

            // Join with a single space (unless current line is empty)
            if(!currentLine.empty() && !trimmedNext.empty())
            {
                *cursorX =
                    currentLine.length(); // Position cursor at join point
                currentLine += " " + trimmedNext;
            }
            else if(currentLine.empty())
            {
                currentLine = trimmedNext;
                *cursorX = 0;
            }
            else
            {
                *cursorX = currentLine.length();
            }

            // Delete the next line
            lines->erase(lines->begin() + *cursorY + 1);
            joinCount++;
        }

        if(joinCount > 0)
        {
            *dirty = true;
            saveState();
            needsFullRedraw = true;
            if(joinCount > 1)
                setStatusMessage(std::to_string(joinCount + 1) +
                                 " lines joined");
        }
        break;
    }

    case 'c':
        // change operator: enter operator pending (support e.g. cw, ci(, etc.)
        enterOperatorPending('c');
        break;
    case 'p':
        pasteAfter();
        break;
    case 'P':
        pasteBefore();
        break;
    case 'u':
        undo();
        break;
    case Terminal::CTRL_R:
        redo();
        break;
    case '%':
        moveToMatchingBracket();
        adjustViewport();
        break;
    default:
        if(c != 'g' && c != 'd' && c != 'y')
        {
            commandBuffer.clear();
        }
        break;
    }

    // Handle g-prefixed commands at end (for 'gg' which needs switch case for
    // first 'g')
    if(commandBuffer == "g")
    {
        if(c == 'd')
        {
            commandBuffer.clear();
            goToDefinition();
            repeatCount = 0;
            return;
        }

        // Unknown g-command → cancel
        if(c != 'g')
        {
            commandBuffer.clear();
        }
    }

    repeatCount = 0;
}

// ============================================================================
// Extended Editing Commands
// ============================================================================

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
    if(isCppFile())
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
        if(isCppFile())
        {
            size_t lastNonSpace = currentLine.find_last_not_of(" \t");
            if(lastNonSpace != std::string::npos &&
               currentLine[lastNonSpace] == '{')
            {
                addExtraIndent = true;
            }
        }

        if(isCppFile() && !addExtraIndent)
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
}

void Editor::repeatLastChange()
{
    setStatusMessage("Repeat not yet implemented");
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
        yankBuffer.clear();
        int endLine = std::min(*cursorY + count, (int)lines->size());
        for(int y = *cursorY; y < endLine; y++)
        {
            yankBuffer += (*lines)[y] + "\n";
        }
        setStatusMessage(std::to_string(count) + " lines yanked");
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
        }
    }

    if(lines->empty())
        lines->push_back("");

    *cursorY = std::min(startY, (int)lines->size() - 1);
    *cursorX = 0;
    *dirty = true;
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

void Editor::commandHistoryUp()
{
    if(commandHistory.empty())
        return;
    if(commandHistoryIndex < 0)
    {
        commandHistoryIndex = commandHistory.size() - 1;
    }
    else if(commandHistoryIndex > 0)
    {
        commandHistoryIndex--;
    }
    commandInput = commandHistory[commandHistoryIndex];
}

void Editor::commandHistoryDown()
{
    if(commandHistory.empty() || commandHistoryIndex < 0)
        return;
    if(commandHistoryIndex < (int)commandHistory.size() - 1)
    {
        commandHistoryIndex++;
        commandInput = commandHistory[commandHistoryIndex];
    }
    else
    {
        commandHistoryIndex = -1;
        commandInput.clear();
    }
}

std::vector<std::string> Editor::getCommandCompletions(std::string_view prefix)
{
    std::vector<std::string> commands = {
        "w",       "write",  "q",          "quit",   "q!",       "qa",
        "qall",    "qa!",    "qall!",      "wq",     "x",        "qw",
        "qw!",     "wa",     "wall",       "wa!",    "wqa",      "wqall",
        "wqa!",    "wqall!", "xa",         "e",      "edit",     "new",
        "vnew",    "bn",     "bnext",      "bp",     "bprev",    "bd",
        "bdelete", "ls",     "buffers",    "sp",     "split",    "vs",
        "vsplit",  "only",   "tabnew",     "tabc",   "tabclose", "set",
        "syntax",  "noh",    "nohlsearch", "lspinfo"};

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

// ============================================================================
// Mode Handler Wrappers - Delegate to state machine implementations
// ============================================================================

void Editor::handleInsertMode(int c)
{
    if(dispatchModeKey(c))
    {
        return;
    }

    // Delegate to state machine implementation in insert_mode.cpp
    InsertMode state;
    ModeContext ctx = createModeContext(this);
    KeyEvent event{c};

    auto nextState = state.handle(ctx, event);

    // Handle state transition if returned
    if(nextState.has_value())
    {
        // Determine which mode to transition to based on the returned state
        if(std::holds_alternative<NormalMode>(*nextState))
        {
            setMode(NORMAL);
        }
        else if(std::holds_alternative<InsertMode>(*nextState))
        {
            setMode(INSERT);
        }
        // Add other state transitions as needed
    }
}

void Editor::handleVisualMode(int c)
{
    if(dispatchModeKey(c))
    {
        return;
    }

    // Delegate to state machine implementation in visual_mode.cpp
    VisualMode state;
    ModeContext ctx = createModeContext(this);
    KeyEvent event{c};

    auto nextState = state.handle(ctx, event);

    // Handle state transition if returned
    if(nextState.has_value())
    {
        if(std::holds_alternative<NormalMode>(*nextState))
        {
            setMode(NORMAL);
        }
        else if(std::holds_alternative<VisualLineMode>(*nextState))
        {
            setMode(VISUAL_LINE);
        }
        else if(std::holds_alternative<VisualBlockMode>(*nextState))
        {
            setMode(VISUAL_BLOCK);
        }
        else if(std::holds_alternative<InsertMode>(*nextState))
        {
            setMode(INSERT);
        }
    }
}

void Editor::handleVisualBlockMode(int c)
{
    if(dispatchModeKey(c))
    {
        return;
    }

    // Delegate to state machine implementation in visual_mode.cpp
    VisualBlockMode state;
    ModeContext ctx = createModeContext(this);
    KeyEvent event{c};

    auto nextState = state.handle(ctx, event);

    // Handle state transition if returned
    if(nextState.has_value())
    {
        if(std::holds_alternative<NormalMode>(*nextState))
        {
            setMode(NORMAL);
        }
        else if(std::holds_alternative<InsertMode>(*nextState))
        {
            setMode(INSERT);
        }
    }
}

void Editor::handleSearchMode(int c)
{
    if(dispatchModeKey(c))
    {
        return;
    }

    // Determine which search mode we're in and delegate accordingly
    if(currentMode == SEARCH_FORWARD)
    {
        SearchForwardMode state;
        ModeContext ctx = createModeContext(this);
        KeyEvent event{c};

        auto nextState = state.handle(ctx, event);

        if(nextState.has_value() &&
           std::holds_alternative<NormalMode>(*nextState))
        {
            setMode(NORMAL);
        }
    }
    else if(currentMode == SEARCH_BACKWARD)
    {
        SearchBackwardMode state;
        ModeContext ctx = createModeContext(this);
        KeyEvent event{c};

        auto nextState = state.handle(ctx, event);

        if(nextState.has_value() &&
           std::holds_alternative<NormalMode>(*nextState))
        {
            setMode(NORMAL);
        }
    }
}
