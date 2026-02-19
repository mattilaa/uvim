#include "editor.h"
#include "log.h"
#include "theme.h"
#include <algorithm>
#include <array>
#include <clocale>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <filesystem>

namespace fs = std::filesystem;

static bool is_directory(std::string_view p)
{
    std::error_code ec;
    return fs::is_directory(fs::path(p), ec);
}

static bool is_regular_file(std::string_view p)
{
    std::error_code ec;
    return fs::is_regular_file(fs::path(p), ec);
}

static fs::path find_workspace_root(const std::vector<std::string>& args)
{
    std::error_code ec;
    fs::path base = fs::current_path(ec);
    if(!args.empty())
    {
        fs::path first = fs::path(args[0]);
        if(is_directory(args[0]))
            base = first;
        else if(is_regular_file(args[0]))
            base = first.parent_path();
    }
    if(base.empty())
        return fs::current_path(ec);

    fs::path dir = fs::absolute(base, ec);
    if(ec)
        dir = base;

    for(;;)
    {
        if(fs::exists(dir / ".clangd", ec) || fs::exists(dir / ".mlangd", ec))
            return dir;
        if(dir.has_parent_path())
        {
            fs::path parent = dir.parent_path();
            if(parent == dir)
                break;
            dir = parent;
        }
        else
        {
            break;
        }
    }

    return base;
}

[[noreturn]] static void die(std::string_view msg, std::string_view arg = {})
{
    std::cerr << "Error: " << msg;
    if(!arg.empty())
        std::cerr << ": " << arg;
    std::cerr << "\n";
    std::exit(2);
}

static bool split_eq(std::string_view s, std::string_view& key,
                     std::string_view& value)
{
    if(auto pos = s.find('='); pos != std::string_view::npos)
    {
        key = s.substr(0, pos);
        value = s.substr(pos + 1);
        return true;
    }
    return false;
}

static std::string_view require_value(std::string_view opt, int& i, int argc,
                                      char* argv[],
                                      std::string_view inline_value)
{
    if(!inline_value.empty())
        return inline_value;
    if(i + 1 >= argc)
        die("missing value for option", opt);
    return std::string_view{argv[++i]};
}

static std::string_view default_config_contents()
{
    static constexpr char kDefaultConfig[] = R"(editor:
  tabspaces: 4
  autobraces: true
  autoquotes: true
  autobracesinstrings: true
  autotags: true
  autocomplete: true
  filebrowser:
    fuzzy: false
  autodetectlsps: true
  formatonsave: true
  status:
    lspgap: 7
  gdcenter: true
  syntax:
    json: true
    yaml: true
    cpp:
      semantic_tokens: true
      locals_color: "normal"
      member_color: "member"
    robot:
      keywords: "if, else, end, for, while, try, except, finally, return, break, continue, skip, fail, run, keyword, library, resource, variables, documentation, tags, metadata, setup, teardown, suite, test, task, template, timeout, default, force"
      highlight_titles: true
      highlight_calls: true
      custom_keywords: ""
      settings: "resource, library, variables, documentation, metadata, suite setup, suite teardown, test setup, test teardown, task setup, task teardown, test template, task template, test timeout, task timeout, force tags, default tags"
  python:
    formatter: "ruff"

theme:
  base:
    fg: "#e0def4"
    bg: "#1f1d2e"
  ui:
    dim: "#6e6a86"
    accent: "#c4a7e7"
    info: "#9ccfd8"
    warning: "#f6c177"
    success: "#9ece6a"
    error: "#eb6f92"
    directory: "#3e8fb0"
    gutter: "#3e8fb0"
    prompt: "#9ece6a"
  statusline:
    fg: "#e0def4"
    bg: "#26233a"
  selection:
    fg: "#e0def4"
    bg: "#403d52"
  cursor:
    fg: "#1f1d2e"
    bg: "#f6c177"
  search:
    fg: "#1f1d2e"
    bg: "#ebbcba"
  panel:
    fg: "#e0def4"
    bg: "#2a283e"
  syntax:
    normal: "#e0def4"
    keyword: "#c4a7e7"
    type: "#9ccfd8"
    string: "#9ece6a"
    char: "#9ece6a"
    comment: "#6e6a86"
    preprocessor: "#f6c177"
    number: "#eb6f92"
    operator: "#f6c177"
    function: "#6b86c9"
    member: "#89dceb"
)";
    return std::string_view{kDefaultConfig, sizeof(kDefaultConfig) - 1};
}

static std::vector<std::string> split_args(std::string_view input)
{
    std::vector<std::string> out;
    size_t i = 0;
    while(i < input.size())
    {
        while(i < input.size() && (input[i] == ' ' || input[i] == '\t'))
            ++i;
        if(i >= input.size())
            break;
        size_t start = i;
        while(i < input.size() && input[i] != ' ' && input[i] != '\t')
            ++i;
        out.emplace_back(input.substr(start, i - start));
    }
    return out;
}

static std::string find_in_path(const std::string& exe)
{
    const char* path = std::getenv("PATH");
    if(!path || !*path)
        return "";

    std::string_view pathView{path};
    size_t start = 0;
    while(start < pathView.size())
    {
        size_t end = pathView.find(':', start);
        if(end == std::string_view::npos)
            end = pathView.size();
        if(end > start)
        {
            fs::path candidate =
                fs::path(std::string(pathView.substr(start, end - start))) /
                exe;
            std::error_code ec;
            if(fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec))
                return candidate.string();
        }
        start = end + 1;
    }
    return "";
}

static std::string find_site_packages(const fs::path& venv)
{
    std::error_code ec;
    fs::path lib = venv / "lib";
    if(!fs::exists(lib, ec))
        lib = venv / "lib64";
    if(!fs::exists(lib, ec) || !fs::is_directory(lib, ec))
        return "";

    for(const auto& entry : fs::directory_iterator(lib, ec))
    {
        if(ec)
            break;
        if(!entry.is_directory(ec))
            continue;
        std::string name = entry.path().filename().string();
        if(name.rfind("python", 0) != 0)
            continue;
        fs::path sp = entry.path() / "site-packages";
        if(fs::exists(sp, ec) && fs::is_directory(sp, ec))
            return sp.string();
    }
    return "";
}

static void prepend_env_path(const char* key, const std::string& value)
{
    if(value.empty())
        return;
    const char* old = std::getenv(key);
    std::string merged = value;
    if(old && *old)
    {
        merged += ":";
        merged += old;
    }
    setenv(key, merged.c_str(), 1);
}

static std::string find_project_config()
{
    std::vector<std::string> names = {"uvim.yaml", ".uvim.yaml", "uvim.yml",
                                      ".uvim.yml"};
    std::error_code ec;
    fs::path cwd = fs::current_path(ec);
    if(ec)
        return "";

    for(const auto& name : names)
    {
        fs::path candidate = cwd / name;
        if(fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec))
            return candidate.string();
    }
    return "";
}

namespace cli
{
constexpr std::string_view kHelp = "--help";
constexpr std::string_view kVersion = "--version";
constexpr std::string_view kConfig = "--config";
constexpr std::string_view kInitConfig = "--init-config";
constexpr std::string_view kClangd = "--clangd";
constexpr std::string_view kCcdir = "--ccdir";
constexpr std::string_view kClangdPath = "--clangd-path";
constexpr std::string_view kQueryDriver = "--query-driver";
constexpr std::string_view kRobotLsp = "--robot-lsp";
constexpr std::string_view kRobotLspPath = "--robot-lsp-path";
constexpr std::string_view kRobotLspArgs = "--robot-lsp-args";
constexpr std::string_view kPythonLsp = "--python-lsp";
constexpr std::string_view kPythonLspPath = "--python-lsp-path";
constexpr std::string_view kPythonLspArgs = "--python-lsp-args";
constexpr std::string_view kMlangLsp = "--mlang-lsp";
constexpr std::string_view kMlangLspPath = "--mlang-lsp-path";
constexpr std::string_view kMlangLspArgs = "--mlang-lsp-args";
constexpr std::string_view kHtmlLsp = "--html-lsp";
constexpr std::string_view kHtmlLspPath = "--html-lsp-path";
constexpr std::string_view kHtmlLspArgs = "--html-lsp-args";
constexpr std::string_view kCssLsp = "--css-lsp";
constexpr std::string_view kCssLspPath = "--css-lsp-path";
constexpr std::string_view kCssLspArgs = "--css-lsp-args";
constexpr std::string_view kJsonLsp = "--json-lsp";
constexpr std::string_view kJsonLspPath = "--json-lsp-path";
constexpr std::string_view kJsonLspArgs = "--json-lsp-args";
constexpr std::string_view kTsLsp = "--ts-lsp";
constexpr std::string_view kTsLspPath = "--ts-lsp-path";
constexpr std::string_view kTsLspArgs = "--ts-lsp-args";
constexpr std::string_view kTheme = "--theme";
constexpr std::string_view kLogFile = "--log-file";
constexpr std::string_view kLogColors = "--log-colors";
constexpr std::string_view kLogDir = "--log-dir";
constexpr std::string_view kEnableLog = "--enable-log";
constexpr std::string_view kMlangLogLevel = "--log-level";

struct HelpRow
{
    std::string_view option;
    std::string_view description;
};

constexpr std::array<HelpRow, 2> kHelpGeneral = {{
    {kHelp, "Show this help and exit"},
    {kVersion, "Show version and exit"},
}};

constexpr std::array<HelpRow, 3> kHelpConfig = {{
    {"--config <path>", "Use custom config path"},
    {"--init-config [path]",
     "Write default config to $XDG_CONFIG_HOME/uvim/config.yaml (defaults to "
     "~/.config/uvim/config.yaml) and exit"},
    {"--theme <path>", "Load theme YAML from path"},
}};

constexpr std::array<HelpRow, 4> kHelpClangdLsp = {{
    {kClangd, "Enable clangd LSP"},
    {"--ccdir <dir>", "Compile commands dir for clangd"},
    {"--clangd-path <path>", "Path to clangd binary"},
    {"--query-driver <list>", "clangd query-driver allowlist"},
}};

constexpr std::array<HelpRow, 3> kHelpRobotLsp = {{
    {kRobotLsp, "Enable Robot Framework LSP"},
    {"--robot-lsp-path <path>", "Path to Robot LSP server"},
    {"--robot-lsp-args <args>", "Extra args for Robot LSP (space-separated)"},
}};

constexpr std::array<HelpRow, 3> kHelpPythonLsp = {{
    {kPythonLsp, "Enable Python LSP"},
    {"--python-lsp-path <path>", "Path to Python LSP server"},
    {"--python-lsp-args <args>", "Extra args for Python LSP (space-separated)"},
}};

constexpr std::array<HelpRow, 3> kHelpMlangLsp = {{
    {kMlangLsp, "Enable Mlang LSP"},
    {"--mlang-lsp-path <path>", "Path to Mlang LSP server"},
    {"--mlang-lsp-args <args>", "Extra args for Mlang LSP (space-separated)"},
}};

constexpr std::array<HelpRow, 3> kHelpHtmlLsp = {{
    {kHtmlLsp, "Enable HTML LSP"},
    {"--html-lsp-path <path>", "Path to HTML LSP server"},
    {"--html-lsp-args <args>", "Extra args for HTML LSP (space-separated)"},
}};

constexpr std::array<HelpRow, 3> kHelpCssLsp = {{
    {kCssLsp, "Enable CSS LSP"},
    {"--css-lsp-path <path>", "Path to CSS LSP server"},
    {"--css-lsp-args <args>", "Extra args for CSS LSP (space-separated)"},
}};

constexpr std::array<HelpRow, 3> kHelpJsonLsp = {{
    {kJsonLsp, "Enable JSON LSP"},
    {"--json-lsp-path <path>", "Path to JSON LSP server"},
    {"--json-lsp-args <args>", "Extra args for JSON LSP (space-separated)"},
}};

constexpr std::array<HelpRow, 3> kHelpTsLsp = {{
    {kTsLsp, "Enable TypeScript/JavaScript LSP"},
    {"--ts-lsp-path <path>", "Path to TS/JS LSP server"},
    {"--ts-lsp-args <args>", "Extra args for TS/JS LSP (space-separated)"},
}};

constexpr std::array<HelpRow, 4> kHelpMlangLogging = {{
    {"--enable-log[=info|debug|verbose]",
     "Enable mlangd_mla logging; info=INFO/WARN/ERROR, debug=+DEBUG, verbose=+VERBOSE"},
    {"--log-level <info|debug|verbose>",
     "Set mlangd_mla log level (implies --enable-log)"},
    {"--log-colors",
     "Enable colored [INFO/WARN/ERROR/DEBUG/VERBOSE] tags in mlangd_mla log"},
    {"--log-dir <dir>",
     "Set mlangd_mla log directory (default file: /tmp/mlangd-mla.log)"},
}};

constexpr std::array<HelpRow, 1> kHelpUvimLogging = {{
    {"--log-file <path>", "Debug log file (UVIM_DEBUG_LOGGING)"},
}};

struct Options
{
    bool showHelp = false;
    bool showVersion = false;
    bool initConfig = false;
    bool logColors = false;
    bool mlangEnableLog = false;
    bool mlangLogColors = false;
    std::string mlangEnableLogMode = "info";
    bool useClangd = false;
    bool useRobotLsp = false;
    bool usePythonLsp = false;
    bool useMlangLsp = false;
    bool useHtmlLsp = false;
    bool useCssLsp = false;
    bool useJsonLsp = false;
    bool useTsLsp = false;

    std::string ccdir;
    std::string clangdPath = "clangd";
    std::string queryDriver;
    std::string robotLspPath = "robotframework-lsp";
    std::string robotLspArgs;
    std::string pythonLspPath = "pyright-langserver";
    std::string pythonLspArgs;
    std::string mlangLspPath = "mlangd_mla";
    std::string mlangLspArgs;
    std::string htmlLspPath = "vscode-html-language-server";
    std::string htmlLspArgs;
    std::string cssLspPath = "vscode-css-language-server";
    std::string cssLspArgs;
    std::string jsonLspPath = "vscode-json-language-server";
    std::string jsonLspArgs;
    std::string tsLspPath = "typescript-language-server";
    std::string tsLspArgs;
    std::string logFile;
    std::string mlangLogDir;
    std::string customConfig;
    std::string initConfigPath;
    std::string themePath;
    std::vector<std::string> args;
};

class CommandLine
{
public:
    static void print_help(const char* exe)
    {
        std::cout << "Usage:\n"
                  << "  " << exe << " [options] [file|dir]\n"
                  << "\nOptions:\n";

        size_t maxOptLen = 0;
        auto scan = [&](const auto& rows)
        {
            for(const auto& row : rows)
                maxOptLen = std::max(maxOptLen, row.option.size());
        };
        scan(kHelpGeneral);
        scan(kHelpConfig);
        scan(kHelpClangdLsp);
        scan(kHelpRobotLsp);
        scan(kHelpPythonLsp);
        scan(kHelpMlangLsp);
        scan(kHelpHtmlLsp);
        scan(kHelpCssLsp);
        scan(kHelpJsonLsp);
        scan(kHelpTsLsp);
        scan(kHelpMlangLogging);
        scan(kHelpUvimLogging);

        auto print_section =
            [&](std::string_view title, const auto& rows)
        {
            std::cout << "  " << title << "\n";
            for(const auto& row : rows)
            {
                std::cout << "    " << std::left
                          << std::setw((int)maxOptLen + 2) << row.option
                          << row.description << "\n";
            }
            std::cout << "\n";
        };

        print_section("General:", kHelpGeneral);
        print_section("Config:", kHelpConfig);
        print_section("clangd LSP:", kHelpClangdLsp);
        print_section("Robot LSP:", kHelpRobotLsp);
        print_section("Python LSP:", kHelpPythonLsp);
        print_section("Mlang LSP:", kHelpMlangLsp);
        print_section("HTML LSP:", kHelpHtmlLsp);
        print_section("CSS LSP:", kHelpCssLsp);
        print_section("JSON LSP:", kHelpJsonLsp);
        print_section("TypeScript/JavaScript LSP:", kHelpTsLsp);
        print_section("mlangd_mla Logging:", kHelpMlangLogging);
        print_section("uvim Debug Logging:", kHelpUvimLogging);
    }

    static Options parse(int argc, char* argv[])
    {
        Options opts;
        bool parse_options = true;
        bool sawOptionValue = false;

        for(int i = 1; i < argc; ++i)
        {
            std::string_view a{argv[i]};

            if(parse_options && a == "--")
            {
                parse_options = false;
                continue;
            }

            if(parse_options && a.rfind("--", 0) == 0)
            {
                std::string_view key, val;
                if(!split_eq(a, key, val))
                {
                    key = a;
                    val = {};
                }

                if(key == kHelp)
                {
                    opts.showHelp = true;
                }
                else if(key == kVersion)
                {
                    opts.showVersion = true;
                }
                else if(key == kClangd)
                {
                    opts.useClangd = true;
                }
                else if(key == kCcdir)
                {
                    opts.ccdir =
                        std::string(require_value(key, i, argc, argv, val));
                    sawOptionValue = true;
                }
                else if(key == kClangdPath)
                {
                    opts.clangdPath =
                        std::string(require_value(key, i, argc, argv, val));
                    sawOptionValue = true;
                }
                else if(key == kQueryDriver)
                {
                    opts.queryDriver =
                        std::string(require_value(key, i, argc, argv, val));
                    sawOptionValue = true;
                }
                else if(key == kRobotLsp)
                {
                    opts.useRobotLsp = true;
                }
                else if(key == kRobotLspPath)
                {
                    opts.robotLspPath =
                        std::string(require_value(key, i, argc, argv, val));
                    sawOptionValue = true;
                }
                else if(key == kRobotLspArgs)
                {
                    opts.robotLspArgs =
                        std::string(require_value(key, i, argc, argv, val));
                    sawOptionValue = true;
                }
                else if(key == kPythonLsp)
                {
                    opts.usePythonLsp = true;
                }
                else if(key == kPythonLspPath)
                {
                    opts.pythonLspPath =
                        std::string(require_value(key, i, argc, argv, val));
                    sawOptionValue = true;
                }
                else if(key == kPythonLspArgs)
                {
                    opts.pythonLspArgs =
                        std::string(require_value(key, i, argc, argv, val));
                    sawOptionValue = true;
                }
                else if(key == kMlangLsp)
                {
                    opts.useMlangLsp = true;
                }
                else if(key == kMlangLspPath)
                {
                    opts.mlangLspPath =
                        std::string(require_value(key, i, argc, argv, val));
                    sawOptionValue = true;
                }
                else if(key == kMlangLspArgs)
                {
                    opts.mlangLspArgs =
                        std::string(require_value(key, i, argc, argv, val));
                    sawOptionValue = true;
                }
                else if(key == kHtmlLsp)
                {
                    opts.useHtmlLsp = true;
                }
                else if(key == kHtmlLspPath)
                {
                    opts.htmlLspPath =
                        std::string(require_value(key, i, argc, argv, val));
                    sawOptionValue = true;
                }
                else if(key == kHtmlLspArgs)
                {
                    opts.htmlLspArgs =
                        std::string(require_value(key, i, argc, argv, val));
                    sawOptionValue = true;
                }
                else if(key == kCssLsp)
                {
                    opts.useCssLsp = true;
                }
                else if(key == kCssLspPath)
                {
                    opts.cssLspPath =
                        std::string(require_value(key, i, argc, argv, val));
                    sawOptionValue = true;
                }
                else if(key == kCssLspArgs)
                {
                    opts.cssLspArgs =
                        std::string(require_value(key, i, argc, argv, val));
                    sawOptionValue = true;
                }
                else if(key == kJsonLsp)
                {
                    opts.useJsonLsp = true;
                }
                else if(key == kJsonLspPath)
                {
                    opts.jsonLspPath =
                        std::string(require_value(key, i, argc, argv, val));
                    sawOptionValue = true;
                }
                else if(key == kJsonLspArgs)
                {
                    opts.jsonLspArgs =
                        std::string(require_value(key, i, argc, argv, val));
                    sawOptionValue = true;
                }
                else if(key == kTsLsp)
                {
                    opts.useTsLsp = true;
                }
                else if(key == kTsLspPath)
                {
                    opts.tsLspPath =
                        std::string(require_value(key, i, argc, argv, val));
                    sawOptionValue = true;
                }
                else if(key == kTsLspArgs)
                {
                    opts.tsLspArgs =
                        std::string(require_value(key, i, argc, argv, val));
                    sawOptionValue = true;
                }
                else if(key == kTheme)
                {
                    opts.themePath =
                        std::string(require_value(key, i, argc, argv, val));
                    sawOptionValue = true;
                }
                else if(key == kLogFile)
                {
                    opts.logFile =
                        std::string(require_value(key, i, argc, argv, val));
                    sawOptionValue = true;
                }
                else if(key == kLogColors)
                {
                    opts.logColors = true;
                    opts.mlangLogColors = true;
                }
                else if(key == kLogDir)
                {
                    opts.mlangLogDir =
                        std::string(require_value(key, i, argc, argv, val));
                    opts.mlangEnableLog = true;
                    sawOptionValue = true;
                }
                else if(key == kEnableLog)
                {
                    opts.mlangEnableLog = true;
                    if(val.empty())
                    {
                        opts.mlangEnableLogMode = "info";
                    }
                    else if(val == "debug" || val == "DEBUG")
                    {
                        opts.mlangEnableLogMode = "debug";
                    }
                    else if(val == "verbose" || val == "VERBOSE")
                    {
                        opts.mlangEnableLogMode = "verbose";
                    }
                    else if(val == "info" || val == "INFO")
                    {
                        opts.mlangEnableLogMode = "info";
                    }
                    else
                    {
                        die("invalid value for --enable-log (use info|debug|verbose)", val);
                    }
                }
                else if(key == kMlangLogLevel)
                {
                    std::string_view lvl = require_value(key, i, argc, argv, val);
                    opts.mlangEnableLog = true;
                    if(lvl == "debug" || lvl == "DEBUG")
                    {
                        opts.mlangEnableLogMode = "debug";
                    }
                    else if(lvl == "verbose" || lvl == "VERBOSE")
                    {
                        opts.mlangEnableLogMode = "verbose";
                    }
                    else if(lvl == "info" || lvl == "INFO")
                    {
                        opts.mlangEnableLogMode = "info";
                    }
                    else
                    {
                        die("invalid value for --log-level (use info|debug|verbose)", lvl);
                    }
                    sawOptionValue = true;
                }
                else if(key == kConfig)
                {
                    opts.customConfig =
                        std::string(require_value(key, i, argc, argv, val));
                    sawOptionValue = true;
                }
                else if(key == kInitConfig)
                {
                    opts.initConfig = true;
                    if(!val.empty())
                    {
                        opts.initConfigPath = std::string(val);
                        sawOptionValue = true;
                    }
                    else if(i + 1 < argc && argv[i + 1][0] != '-')
                    {
                        opts.initConfigPath = std::string(argv[++i]);
                        sawOptionValue = true;
                    }
                }
                else
                {
                    opts.args.emplace_back(std::string(a));
                }
            }
            else
            {
                opts.args.emplace_back(std::string(a));
            }
        }

        if(opts.args.empty() && argc > 1 && !sawOptionValue)
        {
            std::string_view fallback{argv[argc - 1]};
            if(!fallback.empty() && fallback[0] != '-')
            {
                opts.args.emplace_back(std::string(fallback));
            }
        }

        return opts;
    }
};
} // namespace cli

struct EditorSettings
{
    bool useClangd = false;
    bool useRobotLsp = false;
    bool usePythonLsp = false;
    bool useMlangLsp = false;
    bool useHtmlLsp = false;
    bool useCssLsp = false;
    bool useJsonLsp = false;
    bool useTsLsp = false;

    std::string ccdir;
    std::string clangdPath = "clangd";
    std::string queryDriver;
    std::string robotLspPath = "robotframework-lsp";
    std::string robotLspArgs;
    std::string pythonLspPath = "pyright-langserver";
    std::string pythonLspArgs;
    std::string mlangLspPath = "mlangd_mla";
    std::string mlangLspArgs;
    std::string htmlLspPath = "vscode-html-language-server";
    std::string htmlLspArgs;
    std::string cssLspPath = "vscode-css-language-server";
    std::string cssLspArgs;
    std::string jsonLspPath = "vscode-json-language-server";
    std::string jsonLspArgs;
    std::string tsLspPath = "typescript-language-server";
    std::string tsLspArgs;
    bool mlangEnableLog = false;
    bool mlangLogColors = false;
    std::string mlangEnableLogMode = "info";
    std::string mlangLogDir;

    static EditorSettings fromOptions(const cli::Options& opts)
    {
        EditorSettings out;
        out.useClangd = opts.useClangd;
        out.useRobotLsp = opts.useRobotLsp;
        out.usePythonLsp = opts.usePythonLsp;
        out.useMlangLsp = opts.useMlangLsp;
        out.useHtmlLsp = opts.useHtmlLsp;
        out.useCssLsp = opts.useCssLsp;
        out.useJsonLsp = opts.useJsonLsp;
        out.useTsLsp = opts.useTsLsp;
        out.ccdir = opts.ccdir;
        out.clangdPath = opts.clangdPath.empty() ? "clangd" : opts.clangdPath;
        out.queryDriver = opts.queryDriver;
        out.robotLspPath = opts.robotLspPath;
        out.robotLspArgs = opts.robotLspArgs;
        out.pythonLspPath = opts.pythonLspPath;
        out.pythonLspArgs = opts.pythonLspArgs;
        out.mlangLspPath = opts.mlangLspPath;
        out.mlangLspArgs = opts.mlangLspArgs;
        out.htmlLspPath = opts.htmlLspPath;
        out.htmlLspArgs = opts.htmlLspArgs;
        out.cssLspPath = opts.cssLspPath;
        out.cssLspArgs = opts.cssLspArgs;
        out.jsonLspPath = opts.jsonLspPath;
        out.jsonLspArgs = opts.jsonLspArgs;
        out.tsLspPath = opts.tsLspPath;
        out.tsLspArgs = opts.tsLspArgs;
        out.mlangEnableLog = opts.mlangEnableLog;
        out.mlangLogColors = opts.mlangLogColors;
        out.mlangEnableLogMode = opts.mlangEnableLogMode;
        out.mlangLogDir = opts.mlangLogDir;
        return out;
    }

    void apply(Editor& editor) const
    {
        const bool autoDetect = editor.autoDetectLsps;
        auto binary_exists = [&](const std::string& path) -> bool
        {
            if(path.empty())
                return false;
            if(path.find('/') != std::string::npos)
            {
                std::error_code ec;
                return fs::exists(path, ec) && fs::is_regular_file(path, ec);
            }
            return !find_in_path(path).empty();
        };

        if(useClangd || autoDetect)
        {
            std::string resolved = clangdPath;
            if(resolved == "clangd")
            {
                std::string found = find_in_path("clangd");
                if(!found.empty())
                    resolved = found;
            }
            if(useClangd || binary_exists(resolved))
            {
                editor.enableClangdLsp(true, ccdir, resolved, queryDriver);
            }
            else if(autoDetect && !useClangd)
            {
                editor.setStatusMessage("clangd: not found");
            }
        }

        if(useRobotLsp || autoDetect)
        {
            std::vector<std::string> args;
            if(!robotLspArgs.empty())
                args = split_args(robotLspArgs);

            std::string robotPath = robotLspPath;
            if(robotPath == "robotframework-lsp")
            {
                fs::path venv = fs::current_path() / ".venv" / "bin";
                fs::path robotPathVenv = venv / "robotframework-lsp";
                fs::path pythonPath = venv / "python";
                std::error_code ec;
                if(fs::exists(robotPathVenv, ec) &&
                   fs::is_regular_file(robotPathVenv, ec))
                {
                    robotPath = robotPathVenv.string();
                }
                else if(fs::exists(pythonPath, ec) &&
                        fs::is_regular_file(pythonPath, ec))
                {
                    robotPath = pythonPath.string();
                    if(args.empty())
                    {
                        args.push_back("-m");
                        args.push_back("robotframework_ls");
                    }
                }
            }
            if(robotPath == "robotframework-lsp")
            {
                std::string found = find_in_path("robotframework-lsp");
                if(!found.empty())
                    robotPath = found;
            }
            {
                fs::path venvRoot = fs::current_path() / ".venv";
                std::error_code ec;
                if(fs::exists(venvRoot, ec) && fs::is_directory(venvRoot, ec))
                {
                    setenv("VIRTUAL_ENV", venvRoot.string().c_str(), 1);
                    prepend_env_path("PATH", (venvRoot / "bin").string());
                    std::string sp = find_site_packages(venvRoot);
                    prepend_env_path("PYTHONPATH", sp);
                    prepend_env_path("ROBOT_PYTHONPATH", sp);
                }
            }
            if(args.empty())
                args.push_back("--stdio");
            if(useRobotLsp || binary_exists(robotPath))
            {
                editor.enableRobotLsp(true, robotPath, args);
            }
            else if(autoDetect && !useRobotLsp)
            {
                editor.setStatusMessage("robot LSP: not found");
            }
        }

        if(usePythonLsp || autoDetect)
        {
            std::string pyPath = pythonLspPath;
            std::vector<std::string> args;
            if(!pythonLspArgs.empty())
            {
                args = split_args(pythonLspArgs);
            }

            {
                fs::path venvRoot = fs::current_path() / ".venv";
                std::error_code ec;
                if(fs::exists(venvRoot, ec) && fs::is_directory(venvRoot, ec))
                {
                    setenv("VIRTUAL_ENV", venvRoot.string().c_str(), 1);
                    prepend_env_path("PATH", (venvRoot / "bin").string());
                    std::string sp = find_site_packages(venvRoot);
                    prepend_env_path("PYTHONPATH", sp);
                }
            }

            if(pyPath == "pyright-langserver")
            {
                fs::path venv = fs::current_path() / ".venv" / "bin";
                fs::path pyrightPath = venv / "pyright-langserver";
                std::error_code ec;
                if(fs::exists(pyrightPath, ec) &&
                   fs::is_regular_file(pyrightPath, ec))
                {
                    pyPath = pyrightPath.string();
                }
                else
                {
                    std::string found = find_in_path("pyright-langserver");
                    if(!found.empty())
                    {
                        pyPath = found;
                    }
                    else
                    {
                        fs::path brewPath =
                            "/opt/homebrew/bin/pyright-langserver";
                        fs::path localPath =
                            "/usr/local/bin/pyright-langserver";
                        if(fs::exists(brewPath, ec) &&
                           fs::is_regular_file(brewPath, ec))
                        {
                            pyPath = brewPath.string();
                        }
                        else if(fs::exists(localPath, ec) &&
                                fs::is_regular_file(localPath, ec))
                        {
                            pyPath = localPath.string();
                        }
                    }
                }
            }
            if(pyPath == "pylsp")
            {
                fs::path venv = fs::current_path() / ".venv" / "bin";
                fs::path pylspPath = venv / "pylsp";
                fs::path pyrightPath = venv / "pyright-langserver";
                fs::path pythonPath = venv / "python";
                std::error_code ec;
                if(fs::exists(pylspPath, ec) &&
                   fs::is_regular_file(pylspPath, ec))
                {
                    pyPath = pylspPath.string();
                }
                else if(fs::exists(pyrightPath, ec) &&
                        fs::is_regular_file(pyrightPath, ec))
                {
                    pyPath = pyrightPath.string();
                }
                else if(fs::exists(pythonPath, ec) &&
                        fs::is_regular_file(pythonPath, ec))
                {
                    pyPath = pythonPath.string();
                    if(args.empty())
                    {
                        args.push_back("-m");
                        args.push_back("pylsp");
                    }
                }
            }
            if(pyPath == "pylsp")
            {
                std::string found = find_in_path("pylsp");
                if(!found.empty())
                {
                    pyPath = found;
                }
                else
                {
                    found = find_in_path("pyright-langserver");
                    if(!found.empty())
                        pyPath = found;
                }
            }

            if(pyPath.find("pyright-langserver") != std::string::npos)
            {
                bool hasStdio = false;
                for(const auto& a : args)
                {
                    if(a == "--stdio")
                    {
                        hasStdio = true;
                        break;
                    }
                }
                if(!hasStdio)
                    args.push_back("--stdio");
            }
            if(usePythonLsp || binary_exists(pyPath))
            {
                editor.enablePythonLsp(true, pyPath, args);
            }
            else if(autoDetect && !usePythonLsp)
            {
                editor.setStatusMessage("python LSP: not found");
            }
        }

        if(useMlangLsp || autoDetect)
        {
            std::string mlangPath = mlangLspPath;
            std::vector<std::string> args;
            if(!mlangLspArgs.empty())
            {
                args = split_args(mlangLspArgs);
            }
            if(mlangPath == "mlangd_mla" || mlangPath == "mlangd")
            {
                std::vector<std::string> candidates = {"mlangd_mla", "mlangd"};
                for(const auto& candidate : candidates)
                {
                    std::string found = find_in_path(candidate);
                    if(!found.empty())
                    {
                        mlangPath = found;
                        break;
                    }
                }

                if(mlangPath == "mlangd_mla" || mlangPath == "mlangd")
                {
                    std::error_code ec;
                    fs::path tmpPath = "/tmp/mlangd_mla";
                    if(fs::exists(tmpPath, ec) && fs::is_regular_file(tmpPath, ec))
                    {
                        mlangPath = tmpPath.string();
                    }
                }

                if(mlangPath == "mlangd_mla" || mlangPath == "mlangd")
                {
                    const char* home = std::getenv("HOME");
                    if(home && *home)
                    {
                        std::vector<fs::path> localCandidates = {
                            fs::path(home) / ".local" / "bin" / "mlangd_mla",
                            fs::path(home) / ".local" / "bin" / "mlangd"};
                        for(const auto& localPath : localCandidates)
                        {
                            std::error_code ec;
                            if(fs::exists(localPath, ec) &&
                               fs::is_regular_file(localPath, ec))
                            {
                                mlangPath = localPath.string();
                                break;
                            }
                        }
                    }
                }

                if(mlangPath == "mlangd_mla" || mlangPath == "mlangd")
                {
                    std::error_code ec;
                    std::vector<fs::path> sysCandidates = {
                        "/opt/homebrew/bin/mlangd_mla",
                        "/usr/local/bin/mlangd_mla",
                        "/opt/homebrew/bin/mlangd",
                        "/usr/local/bin/mlangd"};
                    for(const auto& sysPath : sysCandidates)
                    {
                        if(fs::exists(sysPath, ec) && fs::is_regular_file(sysPath, ec))
                        {
                            mlangPath = sysPath.string();
                            break;
                        }
                    }
                }
            }
            if(args.empty())
            {
                args.push_back("--stdio");
            }

            auto has_arg = [&](std::string_view key) -> bool
            {
                for(const auto& a : args)
                {
                    if(a == key)
                        return true;
                    if(a.rfind(std::string(key) + "=", 0) == 0)
                        return true;
                }
                return false;
            };

            if(mlangEnableLog && mlangPath.find("mlangd_mla") != std::string::npos)
            {
                if(!has_arg("--enable-log"))
                    args.push_back("--enable-log");
                if(!has_arg("--log-level"))
                {
                    if(mlangEnableLogMode == "debug")
                    {
                        args.push_back("--log-level");
                        args.push_back("DEBUG");
                    }
                    else if(mlangEnableLogMode == "verbose")
                    {
                        args.push_back("--log-level");
                        args.push_back("VERBOSE");
                    }
                    else
                    {
                        args.push_back("--log-level");
                        args.push_back("INFO");
                    }
                }
                if(mlangLogColors && !has_arg("--log-colors"))
                    args.push_back("--log-colors");
                if(!mlangLogDir.empty() && !has_arg("--log-dir"))
                {
                    args.push_back("--log-dir");
                    args.push_back(mlangLogDir);
                }
            }

            if(useMlangLsp || binary_exists(mlangPath))
            {
                editor.enableMlangLsp(true, mlangPath, args);
            }
            else if(autoDetect && !useMlangLsp)
            {
                editor.setStatusMessage("mlang LSP: not found");
            }
        }

        if(useHtmlLsp || autoDetect)
        {
            std::string htmlPath = htmlLspPath;
            std::vector<std::string> args;
            if(!htmlLspArgs.empty())
                args = split_args(htmlLspArgs);
            if(args.empty())
                args.push_back("--stdio");

            if(useHtmlLsp || binary_exists(htmlPath))
            {
                editor.enableHtmlLsp(true, htmlPath, args);
            }
            else if(autoDetect && !useHtmlLsp)
            {
                editor.setStatusMessage("html LSP: not found");
            }
        }

        if(useCssLsp || autoDetect)
        {
            std::string cssPath = cssLspPath;
            std::vector<std::string> args;
            if(!cssLspArgs.empty())
                args = split_args(cssLspArgs);
            if(args.empty())
                args.push_back("--stdio");

            if(useCssLsp || binary_exists(cssPath))
            {
                editor.enableCssLsp(true, cssPath, args);
            }
            else if(autoDetect && !useCssLsp)
            {
                editor.setStatusMessage("css LSP: not found");
            }
        }

        if(useJsonLsp || autoDetect)
        {
            std::string jsonPath = jsonLspPath;
            std::vector<std::string> args;
            if(!jsonLspArgs.empty())
                args = split_args(jsonLspArgs);
            if(args.empty())
                args.push_back("--stdio");

            if(useJsonLsp || binary_exists(jsonPath))
            {
                editor.enableJsonLsp(true, jsonPath, args);
            }
            else if(autoDetect && !useJsonLsp)
            {
                editor.setStatusMessage("json LSP: not found");
            }
        }

        if(useTsLsp || autoDetect)
        {
            std::string tsPath = tsLspPath;
            std::vector<std::string> args;
            if(!tsLspArgs.empty())
                args = split_args(tsLspArgs);
            if(args.empty())
                args.push_back("--stdio");

            if(useTsLsp || binary_exists(tsPath))
            {
                editor.enableTsLsp(true, tsPath, args);
            }
            else if(autoDetect && !useTsLsp)
            {
                editor.setStatusMessage("ts LSP: not found");
            }
        }
    }
};

int main(int argc, char* argv[])
{
    std::setlocale(LC_CTYPE, "");
    const cli::Options opts = cli::CommandLine::parse(argc, argv);
    if(opts.showHelp)
    {
        cli::CommandLine::print_help(argv[0]);
        return 0;
    }
    if(opts.showVersion)
    {
#ifdef UVIM_VERSION
        std::cout << "uvim " << UVIM_VERSION << "\n";
#else
        std::cout << "uvim\n";
#endif
        return 0;
    }

    if(opts.initConfig)
    {
        std::string path = opts.initConfigPath.empty()
                               ? Theme::defaultConfigPath()
                               : opts.initConfigPath;
        if(path.empty())
            die("cannot determine config path");
        std::error_code ec;
        fs::path outPath(path);
        if(fs::exists(outPath, ec))
            die("config already exists", path);
        fs::create_directories(outPath.parent_path(), ec);
        std::ofstream out(outPath);
        if(!out.is_open())
            die("cannot write config", path);
        out << default_config_contents();
        out.close();
        std::cout << "Wrote default config to " << outPath.string() << "\n";

        fs::path themesDir = outPath.parent_path() / "themes";
        fs::create_directories(themesDir, ec);
        if(ec)
            die("cannot create themes dir", themesDir.string());

        fs::path exePath = fs::absolute(argv[0], ec);
        fs::path repoRoot = exePath.parent_path();
        fs::path bundledThemes = repoRoot / "themes";
        if(fs::exists(bundledThemes, ec) && fs::is_directory(bundledThemes, ec))
        {
            for(const auto& entry : fs::directory_iterator(bundledThemes, ec))
            {
                if(ec)
                    break;
                if(!entry.is_regular_file(ec))
                    continue;
                auto ext = entry.path().extension().string();
                if(ext != ".yaml" && ext != ".yml")
                    continue;
                fs::path dest = themesDir / entry.path().filename();
                if(fs::exists(dest, ec))
                    continue;
                fs::copy_file(entry.path(), dest, ec);
                if(ec)
                {
                    std::cerr << "theme copy failed: " << entry.path().string()
                              << " -> " << dest.string() << "\n";
                    ec.clear();
                }
            }
        }
        else
        {
            std::cerr << "warning: bundled themes not found at "
                      << bundledThemes.string() << "\n";
        }

        return 0;
    }
    // Set custom log file path if provided
#ifdef UVIM_DEBUG_LOGGING
    if(!opts.logFile.empty())
    {
        mla::log::setLogFilePath(opts.logFile);
    }
    if(opts.logColors)
    {
        mla::log::setUseColors(true);
    }
#endif

    std::string projectConfig = find_project_config();
    std::string defaultConfig = Theme::defaultConfigPath();
    std::string_view configView;
    if(!projectConfig.empty())
        configView = projectConfig;
    else if(!opts.customConfig.empty())
        configView = opts.customConfig;
    else
        configView = defaultConfig;
    std::string configPath = std::string(configView);

    // Create editor with flag indicating whether we have files to open
    Editor editor(!opts.args.empty(), configPath, opts.themePath);
    fs::path workspaceRoot = find_workspace_root(opts.args);
    editor.setProjectRoot(workspaceRoot.string());
    EditorSettings::fromOptions(opts).apply(editor);

    if(!opts.args.empty())
    {
        // If first argument is a directory, open file browser
        if(is_directory(opts.args[0]))
        {
            editor.openFileBrowser(opts.args[0]);
        }
        else
        {
            // Open all files as separate buffers
            for(const auto& f : opts.args)
            {
                editor.openFile(f);
            }
        }
    }
    else
    {
        editor.setMode(WELCOME);
    }

    editor.run();
    return 0;
}
