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
  autocomplete: true
  gdcenter: true
  syntax:
    json: true
    yaml: true
    robot:
      keywords: "if, else, end, for, while, try, except, finally, return, break, continue, skip, fail, run, keyword, library, resource, variables, documentation, tags, metadata, setup, teardown, suite, test, task, template, timeout, default, force"
      highlight_titles: true
      highlight_calls: true
      custom_keywords: ""
      settings: "resource, library, variables, documentation, metadata, suite setup, suite teardown, test setup, test teardown, task setup, task teardown, test template, task template, test timeout, task timeout, force tags, default tags"

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
    function: "#7aa2f7"
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
constexpr std::string_view kLogFile = "--log-file";
constexpr std::string_view kLogColors = "--log-colors";

struct HelpRow
{
    std::string_view option;
    std::string_view description;
};

constexpr std::array<HelpRow, 19> kHelpRows = {{
    {kHelp, "Show this help and exit"},
    {kVersion, "Show version and exit"},
    {"--config <path>", "Use custom config path"},
    {"--init-config [path]", "Write default config and exit"},
    {kClangd, "Enable clangd LSP"},
    {"--ccdir <dir>", "Compile commands dir for clangd"},
    {"--clangd-path <path>", "Path to clangd binary"},
    {"--query-driver <list>", "clangd query-driver allowlist"},
    {kRobotLsp, "Enable Robot Framework LSP"},
    {"--robot-lsp-path <path>", "Path to Robot LSP server"},
    {"--robot-lsp-args <args>", "Extra args for Robot LSP (space-separated)"},
    {kPythonLsp, "Enable Python LSP"},
    {"--python-lsp-path <path>", "Path to Python LSP server"},
    {"--python-lsp-args <args>", "Extra args for Python LSP (space-separated)"},
    {kMlangLsp, "Enable Mlang LSP"},
    {"--mlang-lsp-path <path>", "Path to Mlang LSP server"},
    {"--mlang-lsp-args <args>", "Extra args for Mlang LSP (space-separated)"},
    {"--log-file <path>", "Debug log file (UVIM_DEBUG_LOGGING)"},
    {kLogColors, "Enable colored log output"},
}};

struct Options
{
    bool showHelp = false;
    bool showVersion = false;
    bool initConfig = false;
    bool logColors = false;
    bool useClangd = false;
    bool useRobotLsp = false;
    bool usePythonLsp = false;
    bool useMlangLsp = false;

    std::string ccdir;
    std::string clangdPath = "clangd";
    std::string queryDriver;
    std::string robotLspPath = "robotframework-lsp";
    std::string robotLspArgs;
    std::string pythonLspPath = "pyright-langserver";
    std::string pythonLspArgs;
    std::string mlangLspPath = "python3";
    std::string mlangLspArgs;
    std::string logFile;
    std::string customConfig;
    std::string initConfigPath;
    std::vector<std::string> args;
};

class CommandLine
{
public:
    static void print_help(const char* exe)
    {
        std::cout << "Usage: " << exe << " [options] [file|dir]\n"
#ifdef UVIM_VERSION
                  << "Version: " << UVIM_VERSION << "\n"
#endif
                  << "\nOptions:\n";

        size_t maxOptLen = 0;
        for(const auto& row : kHelpRows)
            maxOptLen = std::max(maxOptLen, row.option.size());

        for(const auto& row : kHelpRows)
        {
            std::cout << "  " << std::left << std::setw((int)maxOptLen + 2)
                      << row.option << row.description << "\n";
        }
    }

    static Options parse(int argc, char* argv[])
    {
        Options opts;
        bool parse_options = true;

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
                }
                else if(key == kClangdPath)
                {
                    opts.clangdPath =
                        std::string(require_value(key, i, argc, argv, val));
                }
                else if(key == kQueryDriver)
                {
                    opts.queryDriver =
                        std::string(require_value(key, i, argc, argv, val));
                }
                else if(key == kRobotLsp)
                {
                    opts.useRobotLsp = true;
                }
                else if(key == kRobotLspPath)
                {
                    opts.robotLspPath =
                        std::string(require_value(key, i, argc, argv, val));
                }
                else if(key == kRobotLspArgs)
                {
                    opts.robotLspArgs =
                        std::string(require_value(key, i, argc, argv, val));
                }
                else if(key == kPythonLsp)
                {
                    opts.usePythonLsp = true;
                }
                else if(key == kPythonLspPath)
                {
                    opts.pythonLspPath =
                        std::string(require_value(key, i, argc, argv, val));
                }
                else if(key == kPythonLspArgs)
                {
                    opts.pythonLspArgs =
                        std::string(require_value(key, i, argc, argv, val));
                }
                else if(key == kMlangLsp)
                {
                    opts.useMlangLsp = true;
                }
                else if(key == kMlangLspPath)
                {
                    opts.mlangLspPath =
                        std::string(require_value(key, i, argc, argv, val));
                }
                else if(key == kMlangLspArgs)
                {
                    opts.mlangLspArgs =
                        std::string(require_value(key, i, argc, argv, val));
                }
                else if(key == kLogFile)
                {
                    opts.logFile =
                        std::string(require_value(key, i, argc, argv, val));
                }
                else if(key == kLogColors)
                {
                    opts.logColors = true;
                }
                else if(key == kConfig)
                {
                    opts.customConfig =
                        std::string(require_value(key, i, argc, argv, val));
                }
                else if(key == kInitConfig)
                {
                    opts.initConfig = true;
                    if(!val.empty())
                    {
                        opts.initConfigPath = std::string(val);
                    }
                    else if(i + 1 < argc && argv[i + 1][0] != '-')
                    {
                        opts.initConfigPath = std::string(argv[++i]);
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

        if(opts.args.empty() && argc > 1)
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

    std::string ccdir;
    std::string clangdPath = "clangd";
    std::string queryDriver;
    std::string robotLspPath = "robotframework-lsp";
    std::string robotLspArgs;
    std::string pythonLspPath = "pyright-langserver";
    std::string pythonLspArgs;
    std::string mlangLspPath = "python3";
    std::string mlangLspArgs;

    static EditorSettings fromOptions(const cli::Options& opts)
    {
        EditorSettings out;
        out.useClangd = opts.useClangd;
        out.useRobotLsp = opts.useRobotLsp;
        out.usePythonLsp = opts.usePythonLsp;
        out.useMlangLsp = opts.useMlangLsp;
        out.ccdir = opts.ccdir;
        out.clangdPath = opts.clangdPath.empty() ? "clangd" : opts.clangdPath;
        out.queryDriver = opts.queryDriver;
        out.robotLspPath = opts.robotLspPath;
        out.robotLspArgs = opts.robotLspArgs;
        out.pythonLspPath = opts.pythonLspPath;
        out.pythonLspArgs = opts.pythonLspArgs;
        out.mlangLspPath = opts.mlangLspPath;
        out.mlangLspArgs = opts.mlangLspArgs;
        return out;
    }

    void apply(Editor& editor) const
    {
        if(useClangd)
        {
            editor.enableClangdLsp(true, ccdir, clangdPath, queryDriver);
        }

        if(useRobotLsp)
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
            editor.enableRobotLsp(true, robotPath, args);
        }

        if(usePythonLsp)
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

            if(args.empty())
                args.push_back("--stdio");
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
            editor.enablePythonLsp(true, pyPath, args);
        }

        if(useMlangLsp)
        {
            std::string mlangPath = mlangLspPath;
            std::vector<std::string> args;
            if(!mlangLspArgs.empty())
            {
                args = split_args(mlangLspArgs);
            }
            if(args.empty())
            {
                bool isPython =
                    (mlangPath.find("python") != std::string::npos);
                if(isPython)
                {
                    fs::path localLsp = fs::current_path() / "tools" /
                                        "mlang_lsp" / "mlang_lsp.py";
                    std::error_code ec;
                    if(fs::exists(localLsp, ec))
                        args.push_back(localLsp.string());
                    else
                        args.push_back("tools/mlang_lsp/mlang_lsp.py");
                }
                args.push_back("--stdio");
            }
            editor.enableMlangLsp(true, mlangPath, args);
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
    Editor editor(!opts.args.empty(), configPath);
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
