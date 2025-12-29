#include "editor.h"
#include <filesystem>
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

int main(int argc, char* argv[])
{
    bool useClangd = false;
    fs::path ccdir;
    fs::path clangdPath = "clangd";
    std::string queryDriver;
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

            if(key == "--clangd")
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
    // Create editor with flag indicating whether we have files to open
    Editor editor(!args.empty());

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

    editor.run();
    return 0;
}
