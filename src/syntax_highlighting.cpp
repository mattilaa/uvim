#include "constants.h"
#include "cpp_constants.h"
#include "editor.h"
#include "terminal.h"
#include "text_utils.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unistd.h>

template <class Arr>
inline bool contains_sorted(const Arr& arr, std::string_view s)
{
    return std::ranges::binary_search(arr, s);
}

inline bool is_two_char_op(char a, char b) noexcept
{
    switch(a)
    {
    case '+':
        return (b == '+' || b == '=');
    case '-':
        return (b == '-' || b == '=' || b == '>');
    case '&':
        return (b == '&');
    case '|':
        return (b == '|');
    case '=':
        return (b == '=');
    case '!':
        return (b == '=');
    case '<':
        return (b == '<' || b == '=');
    case '>':
        return (b == '>' || b == '=');
    case ':':
        return (b == ':');
    case '.':
        // treats ".." as two-char; "..." will become ".." + "."
        return (b == '.');
    case '*':
        return (b == '=');
    case '/':
        return (b == '=');
    case '%':
        return (b == '=');
    default:
        return false;
    }
}

namespace
{
std::string ascii_lower(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for(char c : value)
        out.push_back(text_utils::ascii_tolower(c));
    return out;
}

std::optional<TokenType> parse_token_type(std::string_view value)
{
    const std::string type = ascii_lower(value);
    if(type == "normal")
        return TOKEN_NORMAL;
    if(type == "keyword")
        return TOKEN_KEYWORD;
    if(type == "type")
        return TOKEN_TYPE;
    if(type == "string")
        return TOKEN_STRING;
    if(type == "char")
        return TOKEN_CHAR;
    if(type == "comment")
        return TOKEN_COMMENT;
    if(type == "preprocessor")
        return TOKEN_PREPROCESSOR;
    if(type == "number")
        return TOKEN_NUMBER;
    if(type == "operator")
        return TOKEN_OPERATOR;
    if(type == "function" || type == "builtin")
        return TOKEN_FUNCTION;
    if(type == "constant" || type == "literal" || type == "bool")
        return TOKEN_KEYWORD;
    return std::nullopt;
}
} // namespace

bool Editor::isCppFile() const
{
    if(!filename || filename->empty())
        return false;

    // Work with views to avoid copies
    std::string_view pathSv{*filename};

    // Use filesystem for extension logic
    std::filesystem::path p{*filename};

    // If there is no extension, keep your "stdlib header path" heuristics
    if(!p.has_extension())
    {
        return text_utils::contains(pathSv, "/c++/") ||
               text_utils::contains(pathSv, "/bits/") ||
               text_utils::contains(pathSv, "/ext/") ||
               text_utils::contains(pathSv, "/__");
    }

    // Compare extensions (including the dot) without allocating a new string
    // Note: path.extension() returns a path; .string() allocates. Use native
    // string view if possible: We'll just take a small allocation-free route by
    // comparing on the filename view.
    auto dot = pathSv.find_last_of('.');
    if(dot == std::string_view::npos)
        return false;
    std::string_view ext = pathSv.substr(dot); // includes '.'

    return std::any_of(constants::CPP_FILE_EXTENSIONS.begin(),
                       constants::CPP_FILE_EXTENSIONS.end(),
                       [&](std::string_view e)
                       { return text_utils::iequals_ascii(ext, e); });
}

bool Editor::isMlaFile() const
{
    if(filename->empty())
        return false;

    std::string_view pathSv{*filename};
    auto dot = pathSv.find_last_of('.');
    if(dot == std::string_view::npos)
        return false;

    std::string_view ext = pathSv.substr(dot);

    return std::any_of(constants::MLA_FILE_EXTENSIONS.begin(),
                       constants::MLA_FILE_EXTENSIONS.end(),
                       [&](std::string_view e)
                       { return text_utils::iequals_ascii(ext, e); });
}

bool Editor::isRobotFile() const
{
    if(!filename || filename->empty())
        return false;

    std::string_view pathSv{*filename};
    auto dot = pathSv.find_last_of('.');
    if(dot == std::string_view::npos)
        return false;
    std::string_view ext = pathSv.substr(dot);

    return std::any_of(constants::ROBOT_FILE_EXTENSIONS.begin(),
                       constants::ROBOT_FILE_EXTENSIONS.end(),
                       [&](std::string_view e)
                       { return text_utils::iequals_ascii(ext, e); });
}

bool Editor::isPythonFile() const
{
    if(!filename || filename->empty())
        return false;

    std::string_view pathSv{*filename};
    auto dot = pathSv.find_last_of('.');
    if(dot == std::string_view::npos)
        return false;
    std::string_view ext = pathSv.substr(dot);

    return std::any_of(constants::PYTHON_FILE_EXTENSIONS.begin(),
                       constants::PYTHON_FILE_EXTENSIONS.end(),
                       [&](std::string_view e)
                       { return text_utils::iequals_ascii(ext, e); });
}

void Editor::ensureMlangTokensLoaded() const
{
    if(mlangTokensLoaded)
        return;

    mlangTokensLoaded = true;
    mlangTokensAvailable = false;
    mlangTokensCaseInsensitive = false;
    mlangTokenTypes.clear();

    auto load_from_path = [&](const std::filesystem::path& path) -> bool
    {
        std::error_code ec;
        if(!std::filesystem::exists(path, ec))
            return false;

        std::ifstream in(path);
        if(!in)
            return false;

        nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
        if(root.is_discarded())
            return false;

        mlangTokensCaseInsensitive = root.value("case_insensitive", false);

        auto add_tokens = [&](std::string_view typeName,
                              const nlohmann::json& items)
        {
            auto tokenType = parse_token_type(typeName);
            if(!tokenType || !items.is_array())
                return;

            for(const auto& item : items)
            {
                if(!item.is_string())
                    continue;
                std::string key = item.get<std::string>();
                if(mlangTokensCaseInsensitive)
                    key = ascii_lower(key);
                mlangTokenTypes[key] = *tokenType;
            }
        };

        if(root.contains("tokens"))
        {
            const auto& tokens = root["tokens"];
            if(tokens.is_array())
            {
                for(const auto& entry : tokens)
                {
                    if(!entry.is_object())
                        continue;
                    std::string type =
                        entry.value("type", std::string{});
                    add_tokens(type,
                               entry.value("items", nlohmann::json::array()));
                }
            }
            else if(tokens.is_object())
            {
                for(auto it = tokens.begin(); it != tokens.end(); ++it)
                {
                    add_tokens(it.key(), it.value());
                }
            }
        }

        mlangTokensAvailable = !mlangTokenTypes.empty();
        return mlangTokensAvailable;
    };

    std::vector<std::filesystem::path> candidates;
    if(!mlangLspPath.empty())
    {
        std::filesystem::path lspPath = mlangLspPath;
        if(lspPath.is_relative())
            lspPath = std::filesystem::absolute(lspPath);
        std::filesystem::path dir = lspPath.parent_path();
        if(!dir.empty())
            candidates.push_back(dir / "mlang_commands.json");
    }

    candidates.push_back(std::filesystem::current_path() /
                         "mlang_commands.json");

    for(const auto& candidate : candidates)
    {
        if(load_from_path(candidate))
            return;
    }
}

std::optional<TokenType>
Editor::lookupMlangTokenType(std::string_view word) const
{
    ensureMlangTokensLoaded();
    if(!mlangTokensAvailable)
        return std::nullopt;

    std::string key = mlangTokensCaseInsensitive ? ascii_lower(word)
                                                 : std::string(word);
    auto it = mlangTokenTypes.find(key);
    if(it == mlangTokenTypes.end())
        return std::nullopt;
    return it->second;
}

bool Editor::isJsonFile() const
{
    if(!filename || filename->empty())
        return false;
    std::string_view pathSv{*filename};
    auto dot = pathSv.find_last_of('.');
    if(dot == std::string_view::npos)
        return false;
    std::string_view ext = pathSv.substr(dot);
    return std::any_of(constants::JSON_FILE_EXTENSIONS.begin(),
                       constants::JSON_FILE_EXTENSIONS.end(),
                       [&](std::string_view e)
                       { return text_utils::iequals_ascii(ext, e); });
}

bool Editor::isYamlFile() const
{
    if(!filename || filename->empty())
        return false;
    std::string_view pathSv{*filename};
    auto dot = pathSv.find_last_of('.');
    if(dot == std::string_view::npos)
        return false;
    std::string_view ext = pathSv.substr(dot);
    return std::any_of(constants::YAML_FILE_EXTENSIONS.begin(),
                       constants::YAML_FILE_EXTENSIONS.end(),
                       [&](std::string_view e)
                       { return text_utils::iequals_ascii(ext, e); });
}

bool Editor::isCMakeFile() const
{
    if(!filename || filename->empty())
        return false;

    std::string_view pathSv{*filename};
    size_t slashPos = pathSv.find_last_of("/\\");
    std::string_view base = (slashPos == std::string_view::npos)
                                ? pathSv
                                : pathSv.substr(slashPos + 1);

    auto ends_with = [](std::string_view value, std::string_view suffix) -> bool
    {
        if(value.size() < suffix.size())
            return false;
        return text_utils::iequals_ascii(
            value.substr(value.size() - suffix.size()), suffix);
    };

    bool isCmakeBasename =
        std::any_of(constants::CMAKE_FILE_BASENAMES.begin(),
                    constants::CMAKE_FILE_BASENAMES.end(),
                    [&](std::string_view name)
                    { return text_utils::iequals_ascii(base, name); });
    if(isCmakeBasename)
        return true;

    return std::any_of(constants::CMAKE_FILE_SUFFIXES.begin(),
                       constants::CMAKE_FILE_SUFFIXES.end(),
                       [&](std::string_view suffix)
                       { return ends_with(base, suffix); });
}

bool Editor::isShellFile() const
{
    if(!filename || filename->empty())
        return false;

    std::string_view pathSv{*filename};
    size_t slashPos = pathSv.find_last_of("/\\");
    std::string_view base = (slashPos == std::string_view::npos)
                                ? pathSv
                                : pathSv.substr(slashPos + 1);

    auto ends_with = [](std::string_view value, std::string_view suffix) -> bool
    {
        if(value.size() < suffix.size())
            return false;
        return text_utils::iequals_ascii(
            value.substr(value.size() - suffix.size()), suffix);
    };

    bool hasShellExtension =
        std::any_of(constants::SHELL_FILE_EXTENSIONS.begin(),
                    constants::SHELL_FILE_EXTENSIONS.end(),
                    [&](std::string_view ext)
                    { return ends_with(base, ext); });
    if(hasShellExtension)
    {
        return true;
    }

    bool hasShellBasename =
        std::any_of(constants::SHELL_FILE_BASENAMES.begin(),
                    constants::SHELL_FILE_BASENAMES.end(),
                    [&](std::string_view name)
                    { return text_utils::iequals_ascii(base, name); });
    if(hasShellBasename)
    {
        return true;
    }

    if(lines && !lines->empty())
    {
        std::string_view first{(*lines)[0]};
        if(first.starts_with("#!"))
        {
            bool hasShell = std::any_of(
                constants::SHELL_SHEBANG_HINTS.begin(),
                constants::SHELL_SHEBANG_HINTS.end(),
                [&](std::string_view hint)
                { return text_utils::contains(first, hint); });
            if(!hasShell)
            {
                if(first.find("/sh") != std::string_view::npos ||
                   first.find(" sh") != std::string_view::npos)
                {
                    hasShell = true;
                }
            }
            if(hasShell)
            {
                return true;
            }
        }
    }

    return false;
}

size_t Editor::byteOffsetForPosition(int y, int x) const
{
    if(!lines || lines->empty())
        return 0;

    y = std::clamp(y, 0, (int)lines->size() - 1);
    const std::string& ln = (*lines)[y];
    x = std::clamp(x, 0, (int)ln.size());

    size_t off = 0;
    for(int i = 0; i < y; ++i)
        off += (*lines)[i].size() + 1; // + '\n'
    off += (size_t)x;
    return off;
}

bool Editor::clangFormatWithArgs(const std::string& extraArgs,
                                 const std::string& successMessage)
{
    if(!lines || !filename)
        return false;

    if(!isCppFile() || isMlaFile())
    {
        setStatusMessage("clang-format: not a C/C++ file (" + *filename + ")");
        return false;
    }

    const int savedY = cursorY ? *cursorY : 0;
    const int savedX = cursorX ? *cursorX : 0;

    std::string tempPath =
        "/tmp/uvim_format_" + std::to_string(getpid()) + ".tmp";
    std::ofstream tempFile(tempPath);
    if(!tempFile.is_open())
    {
        setStatusMessage("clang-format: failed to create temp file");
        return false;
    }

    for(size_t i = 0; i < lines->size(); ++i)
        tempFile << (*lines)[i] << '\n';
    tempFile.close();

    std::string absFilename = *filename;
    if(!absFilename.empty() && absFilename[0] != '/')
    {
        char cwd[PATH_MAX];
        if(getcwd(cwd, sizeof(cwd)))
            absFilename = std::string(cwd) + "/" + *filename;
    }

    auto buildCmd = [&](const std::string& exe) -> std::string
    {
        std::string cmd = "cat \"" + tempPath + "\" | " + exe +
                          " -style=file -assume-filename=\"" + absFilename +
                          "\"";
        if(!extraArgs.empty())
            cmd += " " + extraArgs;
        cmd += " 2>/tmp/uvim_clang_err.log";
        return cmd;
    };

    std::string cmd = buildCmd("/opt/homebrew/bin/clang-format");
    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe)
    {
        cmd = buildCmd("clang-format");
        pipe = popen(cmd.c_str(), "r");
    }

    if(!pipe)
    {
        unlink(tempPath.c_str());
        setStatusMessage("clang-format: failed to run");
        return false;
    }

    std::string formatted;
    char buffer[4096];
    while(fgets(buffer, sizeof(buffer), pipe))
        formatted += buffer;

    int status = pclose(pipe);
    (void)status;
    unlink(tempPath.c_str());

    if(formatted.empty())
    {
        std::ifstream errFile("/tmp/uvim_clang_err.log");
        std::string errMsg;
        if(errFile.is_open())
        {
            std::getline(errFile, errMsg);
            errFile.close();
        }
        if(errMsg.empty())
            errMsg = "no output";
        setStatusMessage("clang-format: " + errMsg.substr(0, 80));
        return false;
    }

    std::vector<std::string> newLines;
    std::istringstream iss(formatted);
    std::string line;
    while(std::getline(iss, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        newLines.push_back(line);
    }

    if(!newLines.empty() && newLines.back().empty())
        newLines.pop_back();
    if(newLines.empty())
        newLines.push_back("");

    if(newLines == *lines)
    {
        setStatusMessage("clang-format: no changes needed");
        return true;
    }

    saveState();
    *lines = std::move(newLines);
    if(dirty)
        *dirty = true;

    if(cursorY && cursorX && lines && !lines->empty())
    {
        *cursorY = std::clamp(savedY, 0, (int)lines->size() - 1);
        *cursorX = std::clamp(savedX, 0, (int)(*lines)[*cursorY].size());
    }

    adjustViewport();
    needsFullRedraw = true;
    setStatusMessage(successMessage);
    return true;
}

bool Editor::pythonFormatBuffer()
{
    if(!lines || !filename)
        return false;
    if(!isPythonFile())
    {
        setStatusMessage("black: not a Python file");
        return false;
    }

    const int savedY = cursorY ? *cursorY : 0;
    const int savedX = cursorX ? *cursorX : 0;

    std::string tempPath =
        "/tmp/uvim_black_" + std::to_string(getpid()) + ".py";
    std::ofstream tempFile(tempPath);
    if(!tempFile.is_open())
    {
        setStatusMessage("black: failed to create temp file");
        return false;
    }

    for(size_t i = 0; i < lines->size(); ++i)
        tempFile << (*lines)[i] << '\n';
    tempFile.close();

    std::string cmd =
        "black --quiet \"" + tempPath + "\" 2>/tmp/uvim_black_err.log";
    int status = std::system(cmd.c_str());
    (void)status;

    std::ifstream in(tempPath);
    if(!in.is_open())
    {
        unlink(tempPath.c_str());
        setStatusMessage("black: failed to read temp output");
        return false;
    }

    std::vector<std::string> newLines;
    std::string line;
    while(std::getline(in, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        newLines.push_back(line);
    }
    in.close();
    unlink(tempPath.c_str());

    if(!newLines.empty() && newLines.back().empty())
        newLines.pop_back();
    if(newLines.empty())
        newLines.push_back("");

    if(newLines == *lines)
    {
        setStatusMessage("black: no changes");
        return true;
    }

    saveState();
    *lines = std::move(newLines);
    if(dirty)
        *dirty = true;

    if(cursorY && cursorX && lines && !lines->empty())
    {
        *cursorY = std::clamp(savedY, 0, (int)lines->size() - 1);
        *cursorX = std::clamp(savedX, 0, (int)(*lines)[*cursorY].size());
    }

    adjustViewport();
    needsFullRedraw = true;
    setStatusMessage("black: formatted buffer");
    return true;
}

void Editor::pythonLintBuffer()
{
    if(!lines || !filename)
        return;
    if(!isPythonFile())
    {
        setStatusMessage("ruff: not a Python file");
        return;
    }

    std::string tempPath = "/tmp/uvim_ruff_" + std::to_string(getpid()) + ".py";
    std::ofstream tempFile(tempPath);
    if(!tempFile.is_open())
    {
        setStatusMessage("ruff: failed to create temp file");
        return;
    }
    for(size_t i = 0; i < lines->size(); ++i)
        tempFile << (*lines)[i] << '\n';
    tempFile.close();

    std::string cmd =
        "ruff check --quiet \"" + tempPath + "\" 2>/tmp/uvim_ruff_err.log";
    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe)
    {
        unlink(tempPath.c_str());
        setStatusMessage("ruff: failed to run");
        return;
    }

    std::string output;
    char buffer[4096];
    while(fgets(buffer, sizeof(buffer), pipe))
        output += buffer;
    pclose(pipe);
    unlink(tempPath.c_str());

    if(output.empty())
    {
        setStatusMessage("ruff: clean");
        return;
    }

    size_t nl = output.find('\n');
    if(nl != std::string::npos)
        output = output.substr(0, nl);
    setStatusMessage("ruff: " + output);
}

static std::string read_first_line(const std::string& path)
{
    std::ifstream in(path);
    if(!in.is_open())
        return {};
    std::string line;
    std::getline(in, line);
    return line;
}

static std::string normalize_robot_line(const std::string& line, int spaceCount)
{
    if(line.empty())
        return line;
    auto first_non_ws = line.find_first_not_of(" \t");
    if(first_non_ws == std::string::npos)
        return line;
    std::string_view trimmed(line.c_str() + first_non_ws,
                             line.size() - first_non_ws);
    if(trimmed.rfind("***", 0) == 0 || trimmed.rfind("#", 0) == 0)
        return line;

    std::vector<std::string> cells;
    size_t i = first_non_ws;
    if(first_non_ws > 0)
        cells.emplace_back("");

    auto push_cell = [&](std::string& cell)
    {
        cells.push_back(cell);
        cell.clear();
    };

    std::string cell;
    while(i < line.size())
    {
        char c = line[i];
        if(c == '\t')
        {
            push_cell(cell);
            ++i;
            while(i < line.size() && line[i] == ' ')
                ++i;
            continue;
        }
        if(c == ' ')
        {
            size_t j = i;
            while(j < line.size() && line[j] == ' ')
                ++j;
            if(j - i >= 2)
            {
                push_cell(cell);
                i = j;
                continue;
            }
        }
        cell.push_back(c);
        ++i;
    }
    push_cell(cell);

    if(cells.size() <= 1)
        return line;

    std::string out;
    for(size_t idx = 0; idx < cells.size(); ++idx)
    {
        if(idx > 0)
            out.append(spaceCount, ' ');
        out += cells[idx];
    }
    return out;
}

static bool has_robot_cell_separator(std::string_view line)
{
    for(size_t i = 0; i < line.size(); ++i)
    {
        if(line[i] == '\t')
            return true;
        if(line[i] == ' ' && i + 1 < line.size() && line[i + 1] == ' ')
            return true;
    }
    return false;
}

static std::string_view trim_robot_header(std::string_view line)
{
    while(!line.empty() &&
          (line.front() == ' ' || line.front() == '\t' || line.front() == '\r'))
        line.remove_prefix(1);
    while(!line.empty() &&
          (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
        line.remove_suffix(1);
    return line;
}

static bool ascii_starts_with(std::string_view value,
                              std::string_view prefix) noexcept
{
    if(value.size() < prefix.size())
        return false;
    for(size_t i = 0; i < prefix.size(); ++i)
    {
        if(text_utils::ascii_tolower(value[i]) != prefix[i])
            return false;
    }
    return true;
}

static bool split_robot_first_cell(std::string_view line, size_t start,
                                   std::string_view& first,
                                   std::string_view& rest)
{
    size_t i = start;
    while(i < line.size())
    {
        if(line[i] == '\t')
        {
            first = line.substr(start, i - start);
            size_t j = i + 1;
            while(j < line.size() && line[j] == ' ')
                ++j;
            rest = line.substr(j);
            return true;
        }
        if(line[i] == ' ')
        {
            size_t j = i;
            while(j < line.size() && line[j] == ' ')
                ++j;
            if(j - i >= 2)
            {
                first = line.substr(start, i - start);
                rest = line.substr(j);
                return true;
            }
            i = j;
            continue;
        }
        ++i;
    }
    return false;
}

static bool
match_robot_setting_prefix(std::string_view line,
                           const std::unordered_set<std::string>& settings,
                           size_t& matchLen)
{
    matchLen = 0;
    if(settings.empty())
        return false;

    for(const auto& setting : settings)
    {
        std::string_view prefix(setting);
        if(!ascii_starts_with(line, prefix))
            continue;
        if(line.size() > prefix.size())
        {
            char next = line[prefix.size()];
            if(next != ' ' && next != '\t')
                continue;
        }
        if(prefix.size() > matchLen)
            matchLen = prefix.size();
    }
    return matchLen > 0;
}

static std::vector<std::string>
normalize_robot_spacing(const std::vector<std::string>& input, int spaceCount,
                        const std::unordered_set<std::string>& settings)
{
    enum class Section
    {
        None,
        Settings,
        TestCases,
        Tasks,
        Keywords,
        Other,
    };

    Section section = Section::None;
    size_t settingsFirstWidth = 0;
    for(const auto& setting : settings)
        settingsFirstWidth = std::max(settingsFirstWidth, setting.size());

    for(const auto& raw : input)
    {
        std::string line = normalize_robot_line(raw, spaceCount);
        std::string_view trimmed = trim_robot_header(line);
        if(trimmed.empty() || trimmed.front() == '#')
            continue;

        if(trimmed.rfind("***", 0) == 0 && trimmed.ends_with("***"))
        {
            std::string_view inner = trimmed.substr(3, trimmed.size() - 6);
            inner = trim_robot_header(inner);
            if(text_utils::iequals_ascii(inner, "Settings"))
                section = Section::Settings;
            else if(text_utils::iequals_ascii(inner, "Test Cases"))
                section = Section::TestCases;
            else if(text_utils::iequals_ascii(inner, "Tasks"))
                section = Section::Tasks;
            else if(text_utils::iequals_ascii(inner, "Keywords"))
                section = Section::Keywords;
            else
                section = Section::Other;
            continue;
        }

        if(section == Section::Settings)
        {
            size_t firstNonWs = line.find_first_not_of(" \t");
            if(firstNonWs == std::string_view::npos)
                continue;
            std::string_view lineView(line);
            std::string_view trimmedLine = lineView.substr(firstNonWs);
            std::string_view firstCell;
            std::string_view restCell;
            if(split_robot_first_cell(trimmedLine, 0, firstCell, restCell))
            {
                settingsFirstWidth =
                    std::max(settingsFirstWidth, firstCell.size());
            }
            else
            {
                size_t matchLen = 0;
                if(match_robot_setting_prefix(trimmedLine, settings, matchLen))
                {
                    settingsFirstWidth = std::max(settingsFirstWidth, matchLen);
                }
            }
        }
    }

    section = Section::None;
    bool prevNonEmptyTitle = false;
    std::vector<std::string> out;
    out.reserve(input.size());

    for(const auto& raw : input)
    {
        std::string line = normalize_robot_line(raw, spaceCount);
        std::string_view trimmed = trim_robot_header(line);
        if(trimmed.empty() || trimmed.front() == '#')
        {
            out.push_back(std::move(line));
            continue;
        }

        if(trimmed.rfind("***", 0) == 0 && trimmed.ends_with("***"))
        {
            std::string_view inner = trimmed.substr(3, trimmed.size() - 6);
            inner = trim_robot_header(inner);
            if(text_utils::iequals_ascii(inner, "Settings"))
                section = Section::Settings;
            else if(text_utils::iequals_ascii(inner, "Test Cases"))
                section = Section::TestCases;
            else if(text_utils::iequals_ascii(inner, "Tasks"))
                section = Section::Tasks;
            else if(text_utils::iequals_ascii(inner, "Keywords"))
                section = Section::Keywords;
            else
                section = Section::Other;

            out.push_back(std::move(line));
            prevNonEmptyTitle = false;
            continue;
        }

        if(settingsFirstWidth > 0)
        {
            size_t firstNonWs = line.find_first_not_of(" 	");
            if(firstNonWs != std::string_view::npos)
            {
                std::string prefix(line.substr(0, firstNonWs));
                std::string_view lineView(line);
                std::string_view trimmedLine = lineView.substr(firstNonWs);
                size_t matchLen = 0;
                bool match =
                    match_robot_setting_prefix(trimmedLine, settings, matchLen);
                if(match && section == Section::Settings)
                {
                    size_t restStart = matchLen;
                    while(restStart < trimmedLine.size() &&
                          (trimmedLine[restStart] == ' ' ||
                           trimmedLine[restStart] == '	'))
                    {
                        ++restStart;
                    }
                    std::string rebuilt = prefix;
                    std::string_view firstMatch =
                        trimmedLine.substr(0, matchLen);
                    rebuilt += firstMatch;
                    size_t pad = (settingsFirstWidth > matchLen)
                                     ? (settingsFirstWidth - matchLen)
                                     : 0;
                    rebuilt.append(pad + (size_t)spaceCount, ' ');
                    rebuilt += trimmedLine.substr(restStart);
                    line = std::move(rebuilt);
                }
                else if(section == Section::Settings)
                {
                    std::string_view firstCell;
                    std::string_view restCell;
                    if(split_robot_first_cell(trimmedLine, 0, firstCell,
                                              restCell))
                    {
                        std::string rebuilt = prefix;
                        rebuilt += firstCell;
                        size_t pad =
                            (settingsFirstWidth > firstCell.size())
                                ? (settingsFirstWidth - firstCell.size())
                                : 0;
                        rebuilt.append(pad + (size_t)spaceCount, ' ');
                        rebuilt += restCell;
                        line = std::move(rebuilt);
                    }
                }
            }
        }

        bool needsIndent =
            (section == Section::TestCases || section == Section::Tasks ||
             section == Section::Keywords);
        bool hasIndent = !line.empty() && (line[0] == ' ' || line[0] == '\t');
        if(needsIndent && !hasIndent)
        {
            bool shouldIndent = false;
            if(has_robot_cell_separator(line))
                shouldIndent = true;
            else if(prevNonEmptyTitle)
                shouldIndent = true;

            if(shouldIndent)
            {
                line.insert(0, std::string(spaceCount, ' '));
                hasIndent = true;
            }
        }

        prevNonEmptyTitle = !hasIndent;

        out.push_back(std::move(line));
    }

    return out;
}

bool Editor::robotFormatBuffer()
{
    if(!lines || !filename)
        return false;
    if(!isRobotFile())
    {
        setStatusMessage("robocop: not a Robot file");
        return false;
    }

    const int savedY = cursorY ? *cursorY : 0;
    const int savedX = cursorX ? *cursorX : 0;

    std::string tempPath =
        "/tmp/uvim_robot_" + std::to_string(getpid()) + ".robot";
    std::ofstream tempFile(tempPath);
    if(!tempFile.is_open())
    {
        setStatusMessage("robocop: failed to create temp file");
        return false;
    }
    for(size_t i = 0; i < lines->size(); ++i)
        tempFile << (*lines)[i] << '\n';
    tempFile.close();

    const std::string robocopErr = "/tmp/uvim_robocop_err.log";
    const std::string robocopOut = "/tmp/uvim_robocop_out.log";
    const std::string robocopFmt = "/tmp/uvim_robocop_fmt.log";

    auto run_robocop_output = [&](const std::string& extraArgs,
                                  const char* outputFlag) -> bool
    {
        std::string cmd = "robocop format " + extraArgs + " " + outputFlag +
                          " \"" + robocopFmt + "\" \"" + tempPath + "\" >" +
                          robocopOut + " 2>" + robocopErr;
        int status = std::system(cmd.c_str());
        return status == 0;
    };

    auto run_robocop_output_after = [&](const std::string& extraArgs,
                                        const char* outputFlag) -> bool
    {
        std::string cmd = "robocop format " + extraArgs + " \"" + tempPath +
                          "\" " + outputFlag + " \"" + robocopFmt + "\" >" +
                          robocopOut + " 2>" + robocopErr;
        int status = std::system(cmd.c_str());
        return status == 0;
    };

    auto run_robocop_stdout = [&](const std::string& extraArgs) -> bool
    {
        std::string cmd = "robocop format " + extraArgs + " \"" + tempPath +
                          "\" >\"" + robocopFmt + "\" 2>" + robocopErr;
        int status = std::system(cmd.c_str());
        return status == 0;
    };

    bool robocopOk = run_robocop_output("", "--output") ||
                     run_robocop_output("", "--output-file") ||
                     run_robocop_output("", "-o") ||
                     run_robocop_output_after("", "--output") ||
                     run_robocop_output_after("", "--output-file") ||
                     run_robocop_output_after("", "-o") ||
                     run_robocop_stdout("");

    if(!robocopOk)
    {
        std::string err = read_first_line(robocopErr);
        if(err.empty())
            err = "robocop failed";
        setStatusMessage(err);
        unlink(tempPath.c_str());
        unlink(robocopFmt.c_str());
        return false;
    }

    const std::string& readPath =
        std::filesystem::exists(robocopFmt) ? robocopFmt : tempPath;
    std::ifstream in(readPath);
    if(!in.is_open())
    {
        unlink(tempPath.c_str());
        unlink(robocopFmt.c_str());
        setStatusMessage("robocop: failed to read temp output");
        return false;
    }

    std::vector<std::string> newLines;
    std::string line;
    while(std::getline(in, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        newLines.push_back(line);
    }
    in.close();
    unlink(tempPath.c_str());
    unlink(robocopFmt.c_str());

    if(!newLines.empty() && newLines.back().empty())
        newLines.pop_back();
    if(newLines.empty())
        newLines.push_back("");

    auto looks_like_robocop_log = [&]() -> bool
    {
        if(newLines.empty())
            return false;
        if(newLines[0].rfind("Usage: robocop", 0) == 0)
            return true;
        if(newLines[0].rfind("Reformatted ", 0) == 0)
            return true;
        for(const auto& l : newLines)
        {
            if(l.find("file reformatted") != std::string::npos ||
               l.find("files left unchanged") != std::string::npos)
                return true;
        }
        return false;
    };

    if(looks_like_robocop_log())
    {
        auto normalized = normalize_robot_spacing(*lines, 4, robotSettingSet);
        if(normalized != *lines)
        {
            *lines = std::move(normalized);
            saveState();
            if(dirty)
                *dirty = true;
            adjustViewport();
            needsFullRedraw = true;
            setStatusMessage("robocop: normalized spacing");
            return true;
        }
        setStatusMessage("robocop: no changes");
        needsFullRedraw = true;
        return true;
    }

    auto normalized = normalize_robot_spacing(newLines, 4, robotSettingSet);
    if(normalized != newLines)
        newLines = std::move(normalized);

    if(newLines == *lines)
    {
        setStatusMessage("robocop: no changes");
        needsFullRedraw = true;
        return true;
    }

    *lines = std::move(newLines);
    saveState();
    if(dirty)
        *dirty = true;

    if(cursorY && cursorX && lines && !lines->empty())
    {
        *cursorY = std::clamp(savedY, 0, (int)lines->size() - 1);
        *cursorX = std::clamp(savedX, 0, (int)(*lines)[*cursorY].size());
    }

    adjustViewport();
    needsFullRedraw = true;
    setStatusMessage("robocop: formatted buffer");
    return true;
}

bool Editor::jsonFormatBuffer()
{
    if(!lines || !filename)
        return false;
    if(!isJsonFile())
    {
        setStatusMessage("json.tool: not a JSON file");
        return false;
    }

    const int savedY = cursorY ? *cursorY : 0;
    const int savedX = cursorX ? *cursorX : 0;

    std::string tempPath =
        "/tmp/uvim_json_" + std::to_string(getpid()) + ".json";
    std::string outPath =
        "/tmp/uvim_json_" + std::to_string(getpid()) + "_out.json";
    std::ofstream tempFile(tempPath);
    if(!tempFile.is_open())
    {
        setStatusMessage("json.tool: failed to create temp file");
        return false;
    }
    for(size_t i = 0; i < lines->size(); ++i)
        tempFile << (*lines)[i] << '\n';
    tempFile.close();

    std::string cmd = "python -m json.tool \"" + tempPath + "\" > \"" +
                      outPath + "\" 2>/tmp/uvim_json_err.log";
    int status = std::system(cmd.c_str());
    if(status != 0)
    {
        std::string err = read_first_line("/tmp/uvim_json_err.log");
        if(err.empty())
            err = "json.tool failed";
        setStatusMessage(err);
        unlink(tempPath.c_str());
        unlink(outPath.c_str());
        return false;
    }

    std::ifstream in(outPath);
    if(!in.is_open())
    {
        unlink(tempPath.c_str());
        unlink(outPath.c_str());
        setStatusMessage("json.tool: failed to read temp output");
        return false;
    }

    std::vector<std::string> newLines;
    std::string line;
    while(std::getline(in, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        newLines.push_back(line);
    }
    in.close();
    unlink(tempPath.c_str());
    unlink(outPath.c_str());

    if(!newLines.empty() && newLines.back().empty())
        newLines.pop_back();
    if(newLines.empty())
        newLines.push_back("");

    if(newLines == *lines)
    {
        setStatusMessage("json.tool: no changes");
        return true;
    }

    saveState();
    *lines = std::move(newLines);
    if(dirty)
        *dirty = true;

    if(cursorY && cursorX && lines && !lines->empty())
    {
        *cursorY = std::clamp(savedY, 0, (int)lines->size() - 1);
        *cursorX = std::clamp(savedX, 0, (int)(*lines)[*cursorY].size());
    }

    adjustViewport();
    needsFullRedraw = true;
    setStatusMessage("json.tool: formatted buffer");
    return true;
}

bool Editor::yamlFormatBuffer()
{
    if(!lines || !filename)
        return false;
    if(!isYamlFile())
    {
        setStatusMessage("yaml: not a YAML file");
        return false;
    }

    const int savedY = cursorY ? *cursorY : 0;
    const int savedX = cursorX ? *cursorX : 0;

    std::string tempPath =
        "/tmp/uvim_yaml_" + std::to_string(getpid()) + ".yml";
    std::string outPath =
        "/tmp/uvim_yaml_" + std::to_string(getpid()) + "_out.yml";
    std::ofstream tempFile(tempPath);
    if(!tempFile.is_open())
    {
        setStatusMessage("yaml: failed to create temp file");
        return false;
    }
    for(size_t i = 0; i < lines->size(); ++i)
        tempFile << (*lines)[i] << '\n';
    tempFile.close();

    std::string cmd = "python -m yaml \"" + tempPath + "\" > \"" + outPath +
                      "\" 2>/tmp/uvim_yaml_err.log";
    int status = std::system(cmd.c_str());
    if(status != 0)
    {
        std::string err = read_first_line("/tmp/uvim_yaml_err.log");
        if(err.empty())
            err = "yaml formatter failed";
        setStatusMessage(err);
        unlink(tempPath.c_str());
        unlink(outPath.c_str());
        return false;
    }

    std::ifstream in(outPath);
    if(!in.is_open())
    {
        unlink(tempPath.c_str());
        unlink(outPath.c_str());
        setStatusMessage("yaml: failed to read temp output");
        return false;
    }

    std::vector<std::string> newLines;
    std::string line;
    while(std::getline(in, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        newLines.push_back(line);
    }
    in.close();
    unlink(tempPath.c_str());
    unlink(outPath.c_str());

    if(!newLines.empty() && newLines.back().empty())
        newLines.pop_back();
    if(newLines.empty())
        newLines.push_back("");

    if(newLines == *lines)
    {
        setStatusMessage("yaml: no changes");
        return true;
    }

    saveState();
    *lines = std::move(newLines);
    if(dirty)
        *dirty = true;

    if(cursorY && cursorX && lines && !lines->empty())
    {
        *cursorY = std::clamp(savedY, 0, (int)lines->size() - 1);
        *cursorX = std::clamp(savedX, 0, (int)(*lines)[*cursorY].size());
    }

    adjustViewport();
    needsFullRedraw = true;
    setStatusMessage("yaml: formatted buffer");
    return true;
}

void Editor::clangFormatVisualSelection()
{
    if(currentMode != VISUAL && currentMode != VISUAL_LINE)
        return;

    if(!lines || lines->empty())
    {
        setStatusMessage("clang-format: empty buffer");
        return;
    }

    if(currentMode == VISUAL_LINE)
    {
        const int startY =
            std::min(currentBuffer->visualStartY, currentBuffer->visualEndY);
        const int endY =
            std::max(currentBuffer->visualStartY, currentBuffer->visualEndY);

        const int startLine = startY + 1;
        const int endLine = endY + 1;

        const std::string args = "-lines=" + std::to_string(startLine) + ":" +
                                 std::to_string(endLine);

        clangFormatWithArgs(args, "clang-format: formatted selection (lines)");
        return;
    }

    int startY, startX, endY, endX;
    getSelectionBounds(startY, startX, endY, endX);

    endY = std::clamp(endY, 0, (int)lines->size() - 1);
    const int endLineLen = (int)(*lines)[endY].size();
    const int endXExclusive = std::clamp(endX + 1, 0, endLineLen);

    const size_t startOff = byteOffsetForPosition(startY, startX);
    const size_t endOff = byteOffsetForPosition(endY, endXExclusive);

    if(endOff <= startOff)
    {
        setStatusMessage("clang-format: empty selection");
        return;
    }

    const size_t len = endOff - startOff;
    const std::string args = "-offset=" + std::to_string(startOff) +
                             " -length=" + std::to_string(len);

    clangFormatWithArgs(args, "clang-format: formatted selection");
}

void Editor::clangFormatVisualBlockSelection()
{
    if(currentMode != VISUAL_BLOCK)
        return;

    if(!lines || lines->empty())
    {
        setStatusMessage("clang-format: empty buffer");
        return;
    }

    int startY, startX, endY, endX;
    getVisualBlockBounds(startY, startX, endY, endX);

    std::string args;
    for(int y = startY; y <= endY && y < (int)lines->size(); ++y)
    {
        const int lineLen = (int)(*lines)[y].size();
        const int segStart = std::clamp(startX, 0, lineLen);
        const int segEndExclusive = std::clamp(endX + 1, 0, lineLen);

        if(segEndExclusive <= segStart)
            continue;

        const size_t off = byteOffsetForPosition(y, segStart);
        const size_t len = (size_t)(segEndExclusive - segStart);

        args += " -offset=" + std::to_string(off) +
                " -length=" + std::to_string(len);
    }

    if(args.empty())
    {
        setStatusMessage("clang-format: empty visual block");
        return;
    }

    if(!args.empty() && args[0] == ' ')
        args.erase(0, 1);

    clangFormatWithArgs(args, "clang-format: formatted visual block");
}
std::string Editor::getColorCode(TokenType type) const
{
    return theme.syntax(type);
}

std::vector<Token> Editor::tokenizeLine(const std::string& line,
                                        bool& inBlockComment) const
{
    if(isRobotFile())
    {
        std::vector<Token> tokens;
        std::string_view sv{line};
        const int len = static_cast<int>(sv.size());
        int i = 0;

        int first = 0;
        while(first < len && text_utils::is_space(sv[first]))
            ++first;

        if(first >= len)
            return tokens;

        std::string_view trimmed = sv.substr(first);
        if(trimmed.starts_with("#"))
        {
            tokens.push_back({TOKEN_COMMENT, first, len - first});
            return tokens;
        }

        if(trimmed.starts_with("***") && trimmed.ends_with("***"))
        {
            tokens.push_back({TOKEN_KEYWORD, first, len - first});
            return tokens;
        }

        bool isContinuation = false;
        if(trimmed.starts_with("..."))
        {
            tokens.push_back({TOKEN_OPERATOR, first, 3});
            i = first + 3;
            isContinuation = true;
        }

        if(sv[first] == '[')
        {
            size_t close = sv.find(']', (size_t)first + 1);
            if(close != std::string_view::npos)
            {
                tokens.push_back(
                    {TOKEN_KEYWORD, first, (int)(close - first + 1)});
            }
        }

        auto is_keyword = [&](std::string_view word) -> bool
        { return isRobotKeyword(word); };

        auto parse_first_cell = [&](int start) -> std::pair<int, int>
        {
            int idx = start;
            int spaceRun = 0;
            for(; idx < len; ++idx)
            {
                if(sv[idx] == '\t')
                    break;
                if(sv[idx] == ' ')
                {
                    ++spaceRun;
                    if(spaceRun >= 2)
                        break;
                }
                else
                {
                    spaceRun = 0;
                }
            }
            int end = idx;
            if(end > start && sv[end - 1] == ' ')
                --end;
            if(end < start)
                end = start;
            return {start, end};
        };

        if(!isContinuation)
        {
            auto [cellStart, cellEnd] = parse_first_cell(first);
            if(cellEnd > cellStart)
            {
                std::string_view cell = sv.substr(
                    cellStart, static_cast<size_t>(cellEnd - cellStart));
                bool isSettingCell = cell.starts_with('[');
                bool highlightedCell = false;
                if(isRobotSetting(cell))
                {
                    tokens.push_back(
                        {TOKEN_KEYWORD, cellStart, cellEnd - cellStart});
                    highlightedCell = true;
                }
                if(isRobotCustomKeyword(cell))
                {
                    tokens.push_back(
                        {TOKEN_FUNCTION, cellStart, cellEnd - cellStart});
                    highlightedCell = true;
                }
                if(!highlightedCell && syntaxRobotHighlightTitles &&
                   first == 0 && !isSettingCell)
                {
                    tokens.push_back(
                        {TOKEN_FUNCTION, cellStart, cellEnd - cellStart});
                    highlightedCell = true;
                }
                if(!highlightedCell && syntaxRobotHighlightCalls &&
                   !isSettingCell)
                {
                    tokens.push_back(
                        {TOKEN_FUNCTION, cellStart, cellEnd - cellStart});
                }
            }
        }

        while(i < len)
        {
            if(text_utils::is_space(sv[i]))
            {
                ++i;
                continue;
            }

            if(sv[i] == '#' && (i == first || text_utils::is_space(sv[i - 1])))
            {
                tokens.push_back({TOKEN_COMMENT, i, len - i});
                break;
            }

            if((sv[i] == '$' || sv[i] == '@' || sv[i] == '&') && i + 1 < len &&
               sv[i + 1] == '{')
            {
                int start = i;
                i += 2;
                while(i < len && sv[i] != '}')
                    ++i;
                if(i < len)
                    ++i;
                tokens.push_back({TOKEN_TYPE, start, i - start});
                continue;
            }

            if(sv[i] == '"' || sv[i] == '\'')
            {
                char quote = sv[i];
                int start = i++;
                while(i < len && sv[i] != quote)
                {
                    if(sv[i] == '\\' && i + 1 < len)
                        i += 2;
                    else
                        ++i;
                }
                if(i < len)
                    ++i;
                tokens.push_back({TOKEN_STRING, start, i - start});
                continue;
            }

            if(text_utils::is_digit(sv[i]))
            {
                int start = i;
                while(i < len && text_utils::is_digit(sv[i]))
                    ++i;
                tokens.push_back({TOKEN_NUMBER, start, i - start});
                continue;
            }

            if(text_utils::is_alpha(sv[i]) || sv[i] == '_')
            {
                int start = i;
                while(i < len && (text_utils::is_alnum(sv[i]) || sv[i] == '_' ||
                                  sv[i] == '.'))
                {
                    ++i;
                }
                std::string_view word = sv.substr(start, i - start);
                if(is_keyword(word))
                {
                    tokens.push_back({TOKEN_KEYWORD, start, i - start});
                }
                continue;
            }

            if(cpp_constants::is_operator_char(sv[i]))
            {
                tokens.push_back({TOKEN_OPERATOR, i, 1});
                ++i;
                continue;
            }

            ++i;
        }

        return tokens;
    }

    if(isPythonFile())
    {
        std::vector<Token> tokens;
        std::string_view sv{line};
        const int len = static_cast<int>(sv.size());
        int i = 0;
        bool lastWasDef = false;
        bool lastWasClass = false;

        auto is_keyword = [](std::string_view word) -> bool
        {
            static constexpr std::string_view kKeywords[] = {
                "and",    "as",       "assert",   "async", "await",  "break",
                "class",  "continue", "def",      "del",   "elif",   "else",
                "except", "False",    "finally",  "for",   "from",   "global",
                "if",     "import",   "in",       "is",    "lambda", "match",
                "case",   "None",     "nonlocal", "not",   "or",     "pass",
                "raise",  "return",   "True",     "try",   "while",  "with",
                "yield",
            };
            for(const auto& kw : kKeywords)
            {
                if(text_utils::iequals_ascii(word, kw))
                    return true;
            }
            return false;
        };

        while(i < len)
        {
            if(text_utils::is_space(sv[i]))
            {
                ++i;
                continue;
            }

            if(sv[i] == '@')
            {
                int start = i++;
                while(i < len && !text_utils::is_space(sv[i]))
                    ++i;
                tokens.push_back({TOKEN_KEYWORD, start, i - start});
                lastWasDef = false;
                lastWasClass = false;
                continue;
            }

            if(sv[i] == '#')
            {
                tokens.push_back({TOKEN_COMMENT, i, len - i});
                break;
            }

            if(sv[i] == '"' || sv[i] == '\'')
            {
                char quote = sv[i];
                int start = i++;
                if(i + 1 < len && sv[i] == quote && sv[i + 1] == quote)
                {
                    i += 2;
                    tokens.push_back({TOKEN_STRING, start, len - start});
                    break;
                }
                while(i < len && sv[i] != quote)
                {
                    if(sv[i] == '\\' && i + 1 < len)
                        i += 2;
                    else
                        ++i;
                }
                if(i < len)
                    ++i;
                tokens.push_back({TOKEN_STRING, start, i - start});
                continue;
            }

            if(text_utils::is_digit(sv[i]) ||
               (sv[i] == '.' && i + 1 < len && text_utils::is_digit(sv[i + 1])))
            {
                int start = i;
                while(i < len && (text_utils::is_digit(sv[i]) || sv[i] == '.' ||
                                  sv[i] == 'e' || sv[i] == 'E' ||
                                  sv[i] == '_' || sv[i] == 'j' ||
                                  sv[i] == 'J' || sv[i] == '+' || sv[i] == '-'))
                {
                    ++i;
                }
                tokens.push_back({TOKEN_NUMBER, start, i - start});
                continue;
            }

            if(text_utils::is_alpha(sv[i]) || sv[i] == '_')
            {
                int start = i;
                while(i < len && (text_utils::is_alnum(sv[i]) || sv[i] == '_'))
                    ++i;
                std::string_view word = sv.substr(start, i - start);
                if(is_keyword(word))
                {
                    tokens.push_back({TOKEN_KEYWORD, start, i - start});
                    lastWasDef = text_utils::iequals_ascii(word, "def");
                    lastWasClass = text_utils::iequals_ascii(word, "class");
                }
                else if(lastWasDef)
                {
                    tokens.push_back({TOKEN_FUNCTION, start, i - start});
                    lastWasDef = false;
                }
                else if(lastWasClass)
                {
                    tokens.push_back({TOKEN_TYPE, start, i - start});
                    lastWasClass = false;
                }
                else
                {
                    int prev = start - 1;
                    while(prev >= 0 && text_utils::is_space(sv[prev]))
                        --prev;
                    bool memberAccess = (prev >= 0 && sv[prev] == '.');

                    int next = i;
                    while(next < len && text_utils::is_space(sv[next]))
                        ++next;
                    bool isCall = (next < len && sv[next] == '(');

                    if(isCall)
                        tokens.push_back({TOKEN_FUNCTION, start, i - start});
                    else if(memberAccess)
                        tokens.push_back({TOKEN_TYPE, start, i - start});
                }
                continue;
            }

            if(cpp_constants::is_operator_char(sv[i]))
            {
                tokens.push_back({TOKEN_OPERATOR, i, 1});
                ++i;
                continue;
            }

            ++i;
        }

        return tokens;
    }

    if(isMlaFile())
    {
        std::vector<Token> tokens;
        std::string_view sv{line};
        const int len = static_cast<int>(sv.size());
        int i = 0;

        auto is_keyword = [](std::string_view word) -> bool
        {
            static constexpr std::string_view kKeywords[] = {
                "pub",      "extern", "fn",       "impl",     "return",
                "if",       "else",   "match",    "enum",     "for",
                "in",       "break",  "continue", "mod",      "use",
                "let",      "var",    "true",     "false",
            };
            for(const auto& kw : kKeywords)
            {
                if(text_utils::iequals_ascii(word, kw))
                    return true;
            }
            return false;
        };

        auto is_builtin_type = [](std::string_view word) -> bool
        {
            static constexpr std::string_view kTypes[] = {
                "void",  "bool",  "int",   "float", "double", "string",
                "str8",  "str16", "list",  "map",   "tuple",  "struct",
                "i8",    "i16",   "i32",   "i64",   "u8",     "u16",
                "u32",   "u64",
            };
            for(const auto& t : kTypes)
            {
                if(text_utils::iequals_ascii(word, t))
                    return true;
            }
            return false;
        };

        while(i < len)
        {
            while(i < len && text_utils::is_space(sv[i]))
                ++i;
            if(i >= len)
                break;

            // In block comment: consume until "*/" or EOL
            if(inBlockComment)
            {
                const int start = i;
                while(i < len &&
                      !(i < len - 1 && sv[i] == '*' && sv[i + 1] == '/'))
                    ++i;

                if(i < len - 1 && sv[i] == '*' && sv[i + 1] == '/')
                {
                    i += 2;
                    inBlockComment = false;
                }

                tokens.push_back({TOKEN_COMMENT, start, i - start});
                continue;
            }

            // Preprocessor line (only if first non-space is '#')
            if(i == 0 && sv[i] == '#')
            {
                tokens.push_back({TOKEN_PREPROCESSOR, i, len - i});
                break;
            }

            // Line comment
            if(i < len - 1 && sv[i] == '/' && sv[i + 1] == '/')
            {
                tokens.push_back({TOKEN_COMMENT, i, len - i});
                break;
            }

            // Block comment start
            if(i < len - 1 && sv[i] == '/' && sv[i + 1] == '*')
            {
                const int start = i;
                i += 2;

                while(i < len - 1 && !(sv[i] == '*' && sv[i + 1] == '/'))
                    ++i;

                if(i < len - 1 && sv[i] == '*' && sv[i + 1] == '/')
                {
                    i += 2;
                }
                else
                {
                    inBlockComment = true;
                    i = len;
                }

                tokens.push_back({TOKEN_COMMENT, start, i - start});
                continue;
            }

            // String literal
            if(sv[i] == '"')
            {
                const int start = i++;
                while(i < len && sv[i] != '"')
                {
                    if(sv[i] == '\\' && i + 1 < len)
                        i += 2;
                    else
                        ++i;
                }
                if(i < len)
                    ++i;

                tokens.push_back({TOKEN_STRING, start, i - start});
                continue;
            }

            // Character literal
            if(sv[i] == '\'')
            {
                const int start = i++;
                while(i < len && sv[i] != '\'')
                {
                    if(sv[i] == '\\' && i + 1 < len)
                        i += 2;
                    else
                        ++i;
                }
                if(i < len)
                    ++i;

                tokens.push_back({TOKEN_CHAR, start, i - start});
                continue;
            }

            // Number
            if(text_utils::is_digit(sv[i]) ||
               (sv[i] == '.' && i + 1 < len && text_utils::is_digit(sv[i + 1])))
            {
                const int start = i;
                bool hasHex = false;

                if(sv[i] == '0' && i + 1 < len &&
                   (sv[i + 1] == 'x' || sv[i + 1] == 'X'))
                {
                    hasHex = true;
                    i += 2;
                }

                while(i < len && (text_utils::is_digit(sv[i]) ||
                                  (hasHex && text_utils::is_xdigit(sv[i])) ||
                                  sv[i] == '.' || sv[i] == 'e' ||
                                  sv[i] == 'E' || sv[i] == 'f' ||
                                  sv[i] == 'F' || sv[i] == 'u' ||
                                  sv[i] == 'U' || sv[i] == 'l' ||
                                  sv[i] == 'L'))
                {
                    ++i;
                }

                tokens.push_back({TOKEN_NUMBER, start, i - start});
                continue;
            }

            // Identifier / keyword / type / function
            if(text_utils::is_alpha(sv[i]) || sv[i] == '_')
            {
                const int start = i;
                while(i < len &&
                      (text_utils::is_alnum(sv[i]) || sv[i] == '_' ||
                       sv[i] == ':'))
                    ++i;

                const std::string_view word = sv.substr(start, i - start);
                bool isBangCall = false;
                if(i < len && sv[i] == '!' &&
                   (text_utils::iequals_ascii(word, "print") ||
                    text_utils::iequals_ascii(word, "println") ||
                    text_utils::iequals_ascii(word, "eprint") ||
                    text_utils::iequals_ascii(word, "eprintln")))
                {
                    isBangCall = true;
                    ++i;
                }

                int j = i;
                while(j < len && text_utils::is_space(sv[j]))
                    ++j;

                TokenType type = TOKEN_NORMAL;
                if(auto mapped = lookupMlangTokenType(word))
                {
                    type = *mapped;
                }
                else if(is_keyword(word))
                {
                    type = TOKEN_KEYWORD;
                }
                else if(is_builtin_type(word))
                {
                    type = TOKEN_TYPE;
                }
                else if(isBangCall)
                {
                    type = TOKEN_FUNCTION;
                }
                else if(j < len && sv[j] == '(')
                {
                    type = TOKEN_FUNCTION;
                }

                tokens.push_back({type, start, i - start});
                continue;
            }

            // Operators / punctuation
            if(cpp_constants::is_operator_char(sv[i]))
            {
                const int start = i++;
                if(i < len)
                {
                    const char a = sv[i - 1], b = sv[i];
                    if(is_two_char_op(a, b))
                        ++i;
                }

                tokens.push_back({TOKEN_OPERATOR, start, i - start});
                continue;
            }

            // Fallback: single char
            tokens.push_back({TOKEN_NORMAL, i, 1});
            ++i;
        }

        return tokens;
    }

    if(isCMakeFile())
    {
        std::vector<Token> tokens;
        std::string_view sv{line};
        const int len = static_cast<int>(sv.size());
        int i = 0;

        auto is_keyword = [](std::string_view word) -> bool
        {
            static constexpr std::string_view kKeywords[] = {
                "cmake_minimum_required",
                "project",
                "include",
                "include_guard",
                "add_subdirectory",
                "if",
                "elseif",
                "else",
                "endif",
                "foreach",
                "endforeach",
                "while",
                "endwhile",
                "function",
                "endfunction",
                "macro",
                "endmacro",
                "break",
                "continue",
                "return",
                "set",
                "unset",
                "option",
                "set_property",
                "get_property",
                "get_target_property",
                "set_target_properties",
                "set_source_files_properties",
                "set_directory_properties",
                "add_executable",
                "add_library",
                "add_dependencies",
                "target_sources",
                "target_link_libraries",
                "target_include_directories",
                "target_compile_definitions",
                "target_compile_options",
                "target_compile_features",
                "target_link_options",
                "target_link_directories",
                "target_precompile_headers",
                "add_custom_command",
                "add_custom_target",
                "add_compile_options",
                "add_link_options",
                "add_compile_definitions",
                "add_test",
                "enable_testing",
                "set_tests_properties",
                "ctest",
                "install",
                "export",
                "configure_file",
                "file",
                "list",
                "string",
                "math",
                "execute_process",
                "find_package",
                "find_program",
                "find_library",
                "find_path",
                "find_file",
                "find_dependency",
                "find_host_package",
                "include_directories",
                "link_directories",
                "link_libraries",
                "check_ipo_supported",
                "get_filename_component",
                "cmake_parse_arguments",
                "cmake_dependent_option",
                "mark_as_advanced",
                "try_compile",
                "try_run",
                "cmake_path",
                "cmake_policy",
                "cmake_print_properties",
                "cmake_host_system_information",
                "message",
                "on",
                "off",
                "true",
                "false",
                "yes",
                "no",
            };
            for(const auto& kw : kKeywords)
            {
                if(text_utils::iequals_ascii(word, kw))
                    return true;
            }
            return false;
        };

        int first = 0;
        while(first < len && text_utils::is_space(sv[first]))
            ++first;

        if(first < len && sv[first] == '#')
        {
            tokens.push_back({TOKEN_COMMENT, first, len - first});
            return tokens;
        }

        if(first < len && (text_utils::is_alpha(sv[first]) || sv[first] == '_'))
        {
            int start = first;
            int end = start;
            while(end < len && (text_utils::is_alnum(sv[end]) ||
                                sv[end] == '_' || sv[end] == '-'))
            {
                ++end;
            }
            std::string_view word = sv.substr(start, end - start);
            TokenType type = is_keyword(word) ? TOKEN_KEYWORD : TOKEN_FUNCTION;
            tokens.push_back({type, start, end - start});
            i = end;
        }

        while(i < len)
        {
            if(text_utils::is_space(sv[i]))
            {
                ++i;
                continue;
            }

            if(sv[i] == '#')
            {
                tokens.push_back({TOKEN_COMMENT, i, len - i});
                break;
            }

            if(sv[i] == '"' || sv[i] == '\'')
            {
                char quote = sv[i];
                int start = i++;
                while(i < len && sv[i] != quote)
                {
                    if(sv[i] == '\\' && i + 1 < len)
                        i += 2;
                    else
                        ++i;
                }
                if(i < len)
                    ++i;
                tokens.push_back({TOKEN_STRING, start, i - start});
                continue;
            }

            if(sv[i] == '[')
            {
                int start = i;
                int j = i + 1;
                while(j < len && sv[j] == '=')
                    ++j;
                if(j < len && sv[j] == '[')
                {
                    int eqCount = j - (i + 1);
                    int k = j + 1;
                    while(k < len)
                    {
                        if(sv[k] == ']')
                        {
                            int t = k + 1;
                            int eqSeen = 0;
                            while(eqSeen < eqCount && t < len && sv[t] == '=')
                            {
                                ++t;
                                ++eqSeen;
                            }
                            if(eqSeen == eqCount && t < len && sv[t] == ']')
                            {
                                tokens.push_back(
                                    {TOKEN_STRING, start, t - start + 1});
                                i = t + 1;
                                break;
                            }
                        }
                        ++k;
                    }
                    if(k < len)
                        continue;
                }
            }

            if(sv[i] == '$')
            {
                int start = i;
                if(i + 1 < len && sv[i + 1] == '<')
                {
                    i += 2;
                    while(i < len && sv[i] != '>')
                        ++i;
                    if(i < len)
                        ++i;
                    tokens.push_back({TOKEN_TYPE, start, i - start});
                    continue;
                }
                if(i + 1 < len && sv[i + 1] == '{')
                {
                    i += 2;
                    while(i < len && sv[i] != '}')
                        ++i;
                    if(i < len)
                        ++i;
                    tokens.push_back({TOKEN_TYPE, start, i - start});
                    continue;
                }
                if(i + 5 < len &&
                   text_utils::iequals_ascii(sv.substr(i, 5), "$ENV{"))
                {
                    i += 5;
                    while(i < len && sv[i] != '}')
                        ++i;
                    if(i < len)
                        ++i;
                    tokens.push_back({TOKEN_TYPE, start, i - start});
                    continue;
                }
                if(i + 7 < len &&
                   text_utils::iequals_ascii(sv.substr(i, 7), "$CACHE{"))
                {
                    i += 7;
                    while(i < len && sv[i] != '}')
                        ++i;
                    if(i < len)
                        ++i;
                    tokens.push_back({TOKEN_TYPE, start, i - start});
                    continue;
                }
            }

            if(text_utils::is_digit(sv[i]) ||
               (sv[i] == '-' && i + 1 < len && text_utils::is_digit(sv[i + 1])))
            {
                int start = i++;
                while(i < len && (text_utils::is_digit(sv[i]) || sv[i] == '.'))
                    ++i;
                tokens.push_back({TOKEN_NUMBER, start, i - start});
                continue;
            }

            if(text_utils::is_alpha(sv[i]) || sv[i] == '_')
            {
                int start = i;
                while(i < len && (text_utils::is_alnum(sv[i]) || sv[i] == '_' ||
                                  sv[i] == '-'))
                {
                    ++i;
                }
                std::string_view word = sv.substr(start, i - start);
                if(is_keyword(word))
                {
                    tokens.push_back({TOKEN_KEYWORD, start, i - start});
                }
                else
                {
                    int next = i;
                    while(next < len && text_utils::is_space(sv[next]))
                        ++next;
                    if(next < len && sv[next] == '(')
                        tokens.push_back({TOKEN_FUNCTION, start, i - start});
                }
                continue;
            }

            if(cpp_constants::is_operator_char(sv[i]))
            {
                tokens.push_back({TOKEN_OPERATOR, i, 1});
                ++i;
                continue;
            }

            ++i;
        }

        return tokens;
    }

    if(isShellFile())
    {
        std::vector<Token> tokens;
        std::string_view sv{line};
        const int len = static_cast<int>(sv.size());
        int i = 0;
        bool lastWasFunctionKeyword = false;

        auto is_keyword = [](std::string_view word) -> bool
        {
            static constexpr std::string_view kKeywords[] = {
                "if",    "then",    "else",     "elif",    "fi",
                "for",   "in",      "do",       "done",    "while",
                "until", "case",    "esac",     "select",  "function",
                "time",  "coproc",  "return",   "break",   "continue",
                "local", "export",  "readonly", "declare", "typeset",
                "unset", "shift",   "getopts",  "source",  ".",
                "alias", "unalias", "trap",     "set",     "eval",
                "exec",  "true",    "false",
            };
            for(const auto& kw : kKeywords)
            {
                if(text_utils::iequals_ascii(word, kw))
                    return true;
            }
            return false;
        };

        int first = 0;
        while(first < len && text_utils::is_space(sv[first]))
            ++first;
        if(first < len && sv[first] == '#' && first + 1 < len &&
           sv[first + 1] == '!')
        {
            tokens.push_back({TOKEN_COMMENT, first, len - first});
            return tokens;
        }

        while(i < len)
        {
            if(text_utils::is_space(sv[i]))
            {
                ++i;
                continue;
            }

            if(sv[i] == '#' && (i == 0 || text_utils::is_space(sv[i - 1])))
            {
                tokens.push_back({TOKEN_COMMENT, i, len - i});
                break;
            }

            if(sv[i] == '.')
            {
                tokens.push_back({TOKEN_KEYWORD, i, 1});
                ++i;
                continue;
            }

            if(sv[i] == '"' || sv[i] == '\'')
            {
                char quote = sv[i];
                int start = i++;
                while(i < len && sv[i] != quote)
                {
                    if(sv[i] == '\\' && i + 1 < len)
                        i += 2;
                    else
                        ++i;
                }
                if(i < len)
                    ++i;
                tokens.push_back({TOKEN_STRING, start, i - start});
                continue;
            }

            if(sv[i] == '`')
            {
                int start = i++;
                while(i < len && sv[i] != '`')
                {
                    if(sv[i] == '\\' && i + 1 < len)
                        i += 2;
                    else
                        ++i;
                }
                if(i < len)
                    ++i;
                tokens.push_back({TOKEN_STRING, start, i - start});
                continue;
            }

            if(sv[i] == '$')
            {
                int start = i++;
                if(i < len && sv[i] == '{')
                {
                    ++i;
                    while(i < len && sv[i] != '}')
                        ++i;
                    if(i < len)
                        ++i;
                    tokens.push_back({TOKEN_TYPE, start, i - start});
                    continue;
                }
                if(i < len && sv[i] == '(')
                {
                    int depth = 1;
                    ++i;
                    while(i < len && depth > 0)
                    {
                        if(sv[i] == '(')
                            ++depth;
                        else if(sv[i] == ')')
                            --depth;
                        ++i;
                    }
                    tokens.push_back({TOKEN_TYPE, start, i - start});
                    continue;
                }
                while(i < len && (text_utils::is_alnum(sv[i]) || sv[i] == '_' ||
                                  sv[i] == '?'))
                {
                    ++i;
                }
                tokens.push_back({TOKEN_TYPE, start, i - start});
                continue;
            }

            if(text_utils::is_digit(sv[i]) ||
               (sv[i] == '-' && i + 1 < len && text_utils::is_digit(sv[i + 1])))
            {
                int start = i++;
                while(i < len && (text_utils::is_digit(sv[i]) || sv[i] == '.'))
                    ++i;
                tokens.push_back({TOKEN_NUMBER, start, i - start});
                continue;
            }

            if(text_utils::is_alpha(sv[i]) || sv[i] == '_')
            {
                int start = i;
                while(i < len && (text_utils::is_alnum(sv[i]) || sv[i] == '_' ||
                                  sv[i] == '-'))
                {
                    ++i;
                }
                std::string_view word = sv.substr(start, i - start);
                if(is_keyword(word))
                {
                    tokens.push_back({TOKEN_KEYWORD, start, i - start});
                    lastWasFunctionKeyword =
                        text_utils::iequals_ascii(word, "function");
                }
                else if(lastWasFunctionKeyword)
                {
                    tokens.push_back({TOKEN_FUNCTION, start, i - start});
                    lastWasFunctionKeyword = false;
                }
                else
                {
                    int next = i;
                    while(next < len && text_utils::is_space(sv[next]))
                        ++next;
                    if(next < len && sv[next] == '(')
                        tokens.push_back({TOKEN_FUNCTION, start, i - start});
                }
                continue;
            }

            if(cpp_constants::is_operator_char(sv[i]))
            {
                tokens.push_back({TOKEN_OPERATOR, i, 1});
                ++i;
                continue;
            }

            ++i;
        }

        return tokens;
    }

    if(isJsonFile() || isYamlFile())
    {
        std::vector<Token> tokens;
        std::string_view sv{line};
        const int len = static_cast<int>(sv.size());
        int i = 0;

        auto is_bool_null = [](std::string_view word) -> bool
        {
            return word == "true" || word == "false" || word == "null" ||
                   word == "True" || word == "False" || word == "None";
        };

        while(i < len)
        {
            if(text_utils::is_space(sv[i]))
            {
                ++i;
                continue;
            }

            if(sv[i] == '#')
            {
                tokens.push_back({TOKEN_COMMENT, i, len - i});
                break;
            }
            if(i < len - 1 && sv[i] == '/' && sv[i + 1] == '/')
            {
                tokens.push_back({TOKEN_COMMENT, i, len - i});
                break;
            }

            if(sv[i] == '"' || sv[i] == '\'')
            {
                char quote = sv[i];
                int start = i++;
                while(i < len && sv[i] != quote)
                {
                    if(sv[i] == '\\' && i + 1 < len)
                        i += 2;
                    else
                        ++i;
                }
                if(i < len)
                    ++i;

                int tokenLen = i - start;
                TokenType type = TOKEN_STRING;
                int j = i;
                while(j < len && text_utils::is_space(sv[j]))
                    ++j;
                if(j < len && sv[j] == ':')
                    type = TOKEN_KEYWORD;
                tokens.push_back({type, start, tokenLen});
                continue;
            }

            if(text_utils::is_digit(sv[i]) ||
               (sv[i] == '-' && i + 1 < len && text_utils::is_digit(sv[i + 1])))
            {
                int start = i++;
                while(i < len && (text_utils::is_digit(sv[i]) || sv[i] == '.' ||
                                  sv[i] == 'e' || sv[i] == 'E' ||
                                  sv[i] == '_' || sv[i] == '+' || sv[i] == '-'))
                {
                    ++i;
                }
                tokens.push_back({TOKEN_NUMBER, start, i - start});
                continue;
            }

            if(text_utils::is_alpha(sv[i]))
            {
                int start = i;
                while(i < len && text_utils::is_alnum(sv[i]))
                    ++i;
                std::string_view word = sv.substr(start, i - start);
                if(is_bool_null(word))
                    tokens.push_back({TOKEN_KEYWORD, start, i - start});
                continue;
            }

            if(cpp_constants::is_operator_char(sv[i]))
            {
                tokens.push_back({TOKEN_OPERATOR, i, 1});
                ++i;
                continue;
            }

            ++i;
        }

        return tokens;
    }

    std::vector<Token> tokens;
    std::string_view sv{line};

    const int len = static_cast<int>(sv.size());
    int i = 0;

    while(i < len)
    {
        while(i < len && text_utils::is_space(sv[i]))
            ++i;
        if(i >= len)
            break;

        // In block comment: consume until "*/" or EOL
        if(inBlockComment)
        {
            const int start = i;
            while(i < len && !(i < len - 1 && sv[i] == '*' && sv[i + 1] == '/'))
                ++i;

            if(i < len - 1 && sv[i] == '*' && sv[i + 1] == '/')
            {
                i += 2;
                inBlockComment = false;
            }

            tokens.push_back({TOKEN_COMMENT, start, i - start});
            continue;
        }

        // Preprocessor line (only if first non-space is '#')
        if(i == 0 && sv[i] == '#')
        {
            tokens.push_back({TOKEN_PREPROCESSOR, i, len - i});
            break;
        }

        // Line comment
        if(i < len - 1 && sv[i] == '/' && sv[i + 1] == '/')
        {
            tokens.push_back({TOKEN_COMMENT, i, len - i});
            break;
        }

        // Block comment start
        if(i < len - 1 && sv[i] == '/' && sv[i + 1] == '*')
        {
            const int start = i;
            i += 2;

            while(i < len - 1 && !(sv[i] == '*' && sv[i + 1] == '/'))
                ++i;

            if(i < len - 1 && sv[i] == '*' && sv[i + 1] == '/')
            {
                i += 2;
            }
            else
            {
                inBlockComment = true;
                i = len;
            }

            tokens.push_back({TOKEN_COMMENT, start, i - start});
            continue;
        }

        // String literal
        if(sv[i] == '"')
        {
            const int start = i++;
            while(i < len && sv[i] != '"')
            {
                if(sv[i] == '\\' && i + 1 < len)
                    i += 2;
                else
                    ++i;
            }
            if(i < len)
                ++i;

            tokens.push_back({TOKEN_STRING, start, i - start});
            continue;
        }

        // Character literal
        if(sv[i] == '\'')
        {
            const int start = i++;
            while(i < len && sv[i] != '\'')
            {
                if(sv[i] == '\\' && i + 1 < len)
                    i += 2;
                else
                    ++i;
            }
            if(i < len)
                ++i;

            tokens.push_back({TOKEN_CHAR, start, i - start});
            continue;
        }

        // Number
        if(text_utils::is_digit(sv[i]) ||
           (sv[i] == '.' && i + 1 < len && text_utils::is_digit(sv[i + 1])))
        {
            const int start = i;
            bool hasHex = false;

            if(sv[i] == '0' && i + 1 < len &&
               (sv[i + 1] == 'x' || sv[i + 1] == 'X'))
            {
                hasHex = true;
                i += 2;
            }

            while(i < len && (text_utils::is_digit(sv[i]) ||
                              (hasHex && text_utils::is_xdigit(sv[i])) ||
                              sv[i] == '.' || sv[i] == 'e' || sv[i] == 'E' ||
                              sv[i] == 'f' || sv[i] == 'F' || sv[i] == 'u' ||
                              sv[i] == 'U' || sv[i] == 'l' || sv[i] == 'L'))
            {
                ++i;
            }

            tokens.push_back({TOKEN_NUMBER, start, i - start});
            continue;
        }

        // Identifier / keyword / type / function
        if(text_utils::is_alpha(sv[i]) || sv[i] == '_')
        {
            const int start = i;
            while(i < len &&
                  (text_utils::is_alnum(sv[i]) || sv[i] == '_' || sv[i] == ':'))
                ++i;

            const std::string_view word = sv.substr(start, i - start);

            int j = i;
            while(j < len && text_utils::is_space(sv[j]))
                ++j;

            TokenType type = TOKEN_NORMAL;

            if(j < len && sv[j] == '(')
            {
                type = TOKEN_FUNCTION;
            }
            else if(cpp_constants::is_keyword(word))
            {
                type = TOKEN_KEYWORD;
            }
            else if(cpp_constants::is_type(word))
            {
                type = TOKEN_TYPE;
            }
            else if(!tokens.empty())
            {
                const Token& prev = tokens.back();
                const std::string_view prevWord =
                    sv.substr(prev.start, prev.length);

                if(prevWord == "struct" || prevWord == "class" ||
                   prevWord == "enum" || prevWord == ":" || prevWord == "->")
                {
                    type = TOKEN_TYPE;
                }
            }

            tokens.push_back({type, start, i - start});
            continue;
        }

        // Operators / punctuation
        if(cpp_constants::is_operator_char(sv[i]))
        {
            const int start = i++;
            if(i < len)
            {
                const char a = sv[i - 1], b = sv[i];
                if(is_two_char_op(a, b))
                    ++i;
            }

            tokens.push_back({TOKEN_OPERATOR, start, i - start});
            continue;
        }

        // Fallback: single char
        tokens.push_back({TOKEN_NORMAL, i, 1});
        ++i;
    }

    return tokens;
}

void Editor::renderLineWithSyntax(std::string& output, const std::string& line,
                                  int start, int len, int fileRow)
{
    // fileRow is already the absolute line index in the buffer
    int absoluteLineNum = fileRow;

    // Helper function to scan a line and update block comment state
    // This needs to be careful about string literals and character literals
    auto scanLineForBlockComments =
        [](const std::string& scanLine, bool& inComment)
    {
        size_t pos = 0;
        size_t len = scanLine.length();

        while(pos < len)
        {
            if(inComment)
            {
                // Looking for closing */
                size_t closePos = scanLine.find("*/", pos);
                if(closePos != std::string::npos)
                {
                    inComment = false;
                    pos = closePos + 2;
                }
                else
                {
                    break; // Still in block comment at end of line
                }
            }
            else
            {
                char c = scanLine[pos];

                // Skip string literals
                if(c == '"')
                {
                    pos++;
                    while(pos < len)
                    {
                        if(scanLine[pos] == '\\' && pos + 1 < len)
                        {
                            pos += 2; // Skip escaped character
                        }
                        else if(scanLine[pos] == '"')
                        {
                            pos++;
                            break;
                        }
                        else
                        {
                            pos++;
                        }
                    }
                    continue;
                }

                // Skip character literals
                if(c == '\'')
                {
                    pos++;
                    while(pos < len)
                    {
                        if(scanLine[pos] == '\\' && pos + 1 < len)
                        {
                            pos += 2; // Skip escaped character
                        }
                        else if(scanLine[pos] == '\'')
                        {
                            pos++;
                            break;
                        }
                        else
                        {
                            pos++;
                        }
                    }
                    continue;
                }

                // Check for line comment
                if(pos + 1 < len && scanLine[pos] == '/' &&
                   scanLine[pos + 1] == '/')
                {
                    // Rest of line is a line comment
                    break;
                }

                // Check for block comment start
                if(pos + 1 < len && scanLine[pos] == '/' &&
                   scanLine[pos + 1] == '*')
                {
                    inComment = true;
                    pos += 2;
                    continue;
                }

                pos++;
            }
        }
    };

    // Determine block comment state for this line
    // We scan from the beginning of the file to ensure correctness
    // Performance optimization: this is a lightweight scan (just looking for
    // comment delimiters)
    bool blockCommentState = false;
    if(isCppFile() || isMlaFile())
    {
        for(int i = 0; i < absoluteLineNum && i < (int)lines->size(); i++)
        {
            scanLineForBlockComments((*lines)[i], blockCommentState);
        }
    }

    // Now tokenize the current line
    std::vector<Token> tokens = tokenizeLine(line, blockCommentState);

    std::vector<TokenType> charColors(len, TOKEN_NORMAL);

    for(const auto& token : tokens)
    {
        int tokenEnd = token.start + token.length;
        for(int pos = token.start; pos < tokenEnd; pos++)
        {
            int visiblePos = pos - start;
            if(visiblePos >= 0 && visiblePos < len)
            {
                charColors[visiblePos] = token.type;
            }
        }
    }

    TokenType currentColor = TOKEN_NORMAL;
    for(int x = 0; x < len; x++)
    {
        int col = x + start;

        bool highlighted = false;
        bool showCursor =
            (currentMode == VISUAL || currentMode == VISUAL_LINE ||
             currentMode == VISUAL_BLOCK);
        bool isCursor = showCursor && (fileRow == *cursorY && col == *cursorX);
        if(isCursor)
        {
            output += theme.cursor();
            highlighted = true;
        }
        else if(isInSelection(fileRow, col) || isInVisualBlock(fileRow, col))
        {
            output += theme.selection();
            highlighted = true;
        }
        else if(isInSearchMatch(fileRow, col))
        {
            output += theme.searchMatch();
            highlighted = true;
        }

        if(!highlighted && charColors[x] != currentColor)
        {
            currentColor = charColors[x];
            output += getColorCode(currentColor);
        }

        output += line[col];

        if(highlighted)
        {
            output += theme.reset();
            currentColor = TOKEN_NORMAL;
        }
    }

    if(currentColor != TOKEN_NORMAL)
    {
        output += theme.baseFg();
    }
}
