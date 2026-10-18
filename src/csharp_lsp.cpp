#include "csharp_lsp.h"

#include "editor_path_utilities.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace csharp_lsp
{
namespace
{
std::string lower(std::string text)
{
    for(char& ch : text)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}

bool isOmniSharp(const std::string& server)
{
    return lower(fs::path(server).stem().string()) == "omnisharp";
}

bool hasFileWithExtension(const fs::path& dir, const char* extension)
{
    std::error_code ec;
    for(fs::directory_iterator it(dir, ec), end; !ec && it != end;
        it.increment(ec))
    {
        if(it->path().extension() == extension &&
           it->is_regular_file(ec))
            return true;
    }
    return false;
}
} // namespace

std::string resolveServer(const std::string& configured)
{
    if(!configured.empty())
    {
        std::string resolved =
            EditorPathUtilities::resolveExecutablePath(configured);
        if(!resolved.empty())
            return resolved;
    }

    std::vector<std::string> candidates = {"csharp-ls"};
    // `dotnet tool install -g` puts tools in ~/.dotnet/tools, which is
    // often not on PATH.
    const char* home = std::getenv("HOME");
#ifdef _WIN32
    if(const char* profile = std::getenv("USERPROFILE"))
        home = profile;
    const char* exe = ".exe";
#else
    const char* exe = "";
#endif
    if(home && *home)
        candidates.push_back(
            (fs::path(home) / ".dotnet" / "tools" / (std::string("csharp-ls") + exe))
                .string());
    candidates.push_back("OmniSharp");
    candidates.push_back("omnisharp");
    for(const std::string& candidate : candidates)
    {
        std::string resolved =
            EditorPathUtilities::resolveExecutablePath(candidate);
        if(!resolved.empty())
            return resolved;
    }
    return {};
}

std::vector<std::string> serverArgs(const std::string& server,
                                    std::vector<std::string> extra)
{
    std::vector<std::string> args;
    if(isOmniSharp(server) &&
       std::find(extra.begin(), extra.end(), "-lsp") == extra.end())
        args.push_back("-lsp");
    args.insert(args.end(), extra.begin(), extra.end());
    return args;
}

std::string projectRoot(const std::string& file)
{
    if(file.empty())
        return {};
    std::error_code ec;
    fs::path dir = fs::absolute(fs::path(file), ec).parent_path();
    if(ec)
        return {};
    // A solution wins over the project inside it.
    for(const char* extension : {".sln", ".csproj"})
    {
        for(fs::path d = dir; !d.empty(); d = d.parent_path())
        {
            if(hasFileWithExtension(d, extension))
                return d.string();
            if(d == d.parent_path())
                break;
        }
    }
    return {};
}
} // namespace csharp_lsp
