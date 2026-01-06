#include "editor.h"
#include "log.h"
#include "theme.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <filesystem>

namespace fs = std::filesystem;

static bool is_directory(const std::filesystem::path& p)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    return fs::is_directory(p, ec);
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

static std::string default_config_contents()
{
    return R"(editor:
  tabspaces: 4
  autobraces: true

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

static void print_help(const char* exe)
{
    std::cout
        << "Usage: " << exe << " [options] [file|dir]\n"
        << "\nOptions:\n"
        << "  --help                 Show this help and exit\n"
        << "  --version              Show version and exit\n"
        << "  --config <path>         Use custom config path\n"
        << "  --init-config [path]    Write default config and exit\n"
        << "  --clangd               Enable clangd LSP\n"
        << "  --ccdir <dir>           Compile commands dir for clangd\n"
        << "  --clangd-path <path>    Path to clangd binary\n"
        << "  --query-driver <list>   clangd query-driver allowlist\n"
        << "  --log-file <path>       Debug log file (UVIM_DEBUG_LOGGING)\n"
        << "  --log-colors           Enable colored log output\n";
}

int main(int argc, char* argv[])
{
    bool useClangd = false;
    fs::path ccdir;
    fs::path clangdPath = "clangd";
    std::string queryDriver;
    std::string logFile;
    bool logColors = false;
    std::string customConfigPath;
    bool initConfig = false;
    std::string initConfigPath;
    std::vector<fs::path> args;

    auto require_value = [&](std::string_view opt, int& i,
                             std::string_view inline_value) -> std::string_view
    {
        if(!inline_value.empty())
            return inline_value;
        if(i + 1 >= argc)
        {
            std::cerr << "Missing value for option: " << opt << "\n";
            std::exit(2);
        }
        return std::string_view{argv[++i]};
    };

    bool parse_options = true;

    for(int i = 1; i < argc; ++i)
    {
        std::string_view a{argv[i]};

        if(parse_options && a == "--")
        { // stop option parsing
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

            if(key == "--help")
            {
                print_help(argv[0]);
                return 0;
            }
            else if(key == "--version")
            {
#ifdef UVIM_VERSION
                std::cout << "uvim " << UVIM_VERSION << "\n";
#else
                std::cout << "uvim\n";
#endif
                return 0;
            }
            else if(key == "--clangd")
            {
                useClangd = true;
            }
            else if(key == "--ccdir")
            {
                ccdir = require_value(key, i, val);
            }
            else if(key == "--clangd-path")
            {
                clangdPath = require_value(key, i, val);
            }
            else if(key == "--query-driver")
            {
                queryDriver = std::string(require_value(key, i, val));
            }
            else if(key == "--log-file")
            {
                logFile = std::string(require_value(key, i, val));
            }
            else if(key == "--log-colors")
            {
                logColors = true;
            }
            else if(key == "--config")
            {
                customConfigPath = std::string(require_value(key, i, val));
            }
            else if(key == "--init-config")
            {
                initConfig = true;
                if(!val.empty())
                {
                    initConfigPath = std::string(val);
                }
                else if(i + 1 < argc && argv[i + 1][0] != '-')
                {
                    initConfigPath = std::string(argv[++i]);
                }
            }
            else
            {
                // unknown option: keep it as a positional, or error out if you
                // prefer
                args.emplace_back(a);
            }
        }
        else
        {
            args.emplace_back(a);
        }
    }

    if(args.empty() && argc > 1)
    {
        std::string_view fallback{argv[argc - 1]};
        if(!fallback.empty() && fallback[0] != '-')
        {
            args.emplace_back(fallback);
        }
    }

    if(initConfig)
    {
        std::string path =
            initConfigPath.empty() ? Theme::defaultConfigPath()
                                   : initConfigPath;
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
    if(!logFile.empty())
    {
        mla::log::setLogFilePath(logFile);
    }
    if(logColors)
    {
        mla::log::setUseColors(true);
    }
#endif

    std::string projectConfig = find_project_config();
    std::string configPath;
    if(!projectConfig.empty())
        configPath = projectConfig;
    else if(!customConfigPath.empty())
        configPath = customConfigPath;
    else
        configPath = Theme::defaultConfigPath();

    // Create editor with flag indicating whether we have files to open
    Editor editor(!args.empty(), configPath);

    if(useClangd)
    {
        editor.enableClangdLsp(true, ccdir, clangdPath, queryDriver);
    }

    if(!args.empty())
    {
        // If first argument is a directory, open file browser
        if(is_directory(args[0].c_str()))
        {
            editor.openFileBrowser(args[0]);
        }
        else
        {
            // Open all files as separate buffers
            for(const auto& f : args)
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
