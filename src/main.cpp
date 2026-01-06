#include "editor.h"
#include "log.h"
#include "theme.h"
#include <fstream>
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
    std::string_view ccdirArg;
    std::string_view clangdPathArg = "clangd";
    std::string_view queryDriverArg;
    std::string_view logFileArg;
    bool logColors = false;
    std::string_view customConfigArg;
    std::string_view initConfigArg;
    bool initConfig = false;
    std::vector<std::string_view> args;

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
                ccdirArg = require_value(key, i, argc, argv, val);
            }
            else if(key == "--clangd-path")
            {
                clangdPathArg = require_value(key, i, argc, argv, val);
            }
            else if(key == "--query-driver")
            {
                queryDriverArg = require_value(key, i, argc, argv, val);
            }
            else if(key == "--log-file")
            {
                logFileArg = require_value(key, i, argc, argv, val);
            }
            else if(key == "--log-colors")
            {
                logColors = true;
            }
            else if(key == "--config")
            {
                customConfigArg = require_value(key, i, argc, argv, val);
            }
            else if(key == "--init-config")
            {
                initConfig = true;
                if(!val.empty())
                {
                    initConfigArg = val;
                }
                else if(i + 1 < argc && argv[i + 1][0] != '-')
                {
                    initConfigArg = std::string_view{argv[++i]};
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
            initConfigArg.empty() ? Theme::defaultConfigPath()
                                  : std::string(initConfigArg);
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
    if(!logFileArg.empty())
    {
        mla::log::setLogFilePath(std::string(logFileArg));
    }
    if(logColors)
    {
        mla::log::setUseColors(true);
    }
#endif

    std::string projectConfig = find_project_config();
    std::string defaultConfig = Theme::defaultConfigPath();
    std::string_view configView;
    if(!projectConfig.empty())
        configView = projectConfig;
    else if(!customConfigArg.empty())
        configView = customConfigArg;
    else
        configView = defaultConfig;
    std::string configPath = std::string(configView);

    // Create editor with flag indicating whether we have files to open
    Editor editor(!args.empty(), configPath);

    if(useClangd)
    {
        std::string ccdir = ccdirArg.empty() ? std::string()
                                             : std::string(ccdirArg);
        std::string clangdPath =
            clangdPathArg.empty() ? std::string("clangd")
                                  : std::string(clangdPathArg);
        std::string queryDriver =
            queryDriverArg.empty() ? std::string()
                                   : std::string(queryDriverArg);
        editor.enableClangdLsp(true, ccdir, clangdPath, queryDriver);
    }

    if(!args.empty())
    {
        // If first argument is a directory, open file browser
        if(is_directory(args[0]))
        {
            editor.openFileBrowser(std::string(args[0]));
        }
        else
        {
            // Open all files as separate buffers
            for(const auto& f : args)
            {
                editor.openFile(std::string(f));
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
