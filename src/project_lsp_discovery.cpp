#include "project_lsp_discovery.h"

#include <array>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace
{
constexpr std::array<const char*, 9> build_directories = {
    "build",          "build-debug",       "build-release",
    "build_Debug",    "build_Release",     "Debug",
    "Release",        "cmake-build-debug", "cmake-build-release"};

bool regular_file(const fs::path& path)
{
    std::error_code ec;
    return fs::is_regular_file(path, ec) && !ec;
}

fs::path starting_directory(const fs::path& file)
{
    std::error_code ec;
    fs::path absolute = file;
    if(!absolute.is_absolute())
        absolute = fs::absolute(absolute, ec);
    if(ec)
        absolute = file;

    fs::path dir = absolute.parent_path();
    if(dir.empty())
        dir = absolute;
    return dir.lexically_normal();
}

template <typename Finder>
std::optional<project_lsp::Configuration>
walk_parents(const fs::path& file, Finder&& finder)
{
    fs::path dir = starting_directory(file);
    while(!dir.empty())
    {
        if(auto found = finder(dir))
            return found;

        fs::path parent = dir.parent_path();
        if(parent == dir)
            break;
        dir = std::move(parent);
    }
    return std::nullopt;
}
} // namespace

namespace project_lsp
{
std::optional<Configuration>
findCompileCommandsForFile(const fs::path& file)
{
    return walk_parents(file, [](const fs::path& dir)
    {
        if(regular_file(dir / "compile_commands.json"))
            return std::optional<Configuration>{{dir, dir}};

        // CMake commonly writes the database below the source root. Check
        // only conventional direct children so opening a file never causes a
        // recursive project scan.
        for(const char* buildDirectory : build_directories)
        {
            fs::path commandsDirectory = dir / buildDirectory;
            if(regular_file(commandsDirectory / "compile_commands.json"))
                return std::optional<Configuration>{
                    {dir, std::move(commandsDirectory)}};
        }

        fs::path outBuild = dir / "out" / "build";
        if(regular_file(outBuild / "compile_commands.json"))
            return std::optional<Configuration>{{dir, std::move(outBuild)}};
        return std::optional<Configuration>{};
    });
}

std::optional<Configuration>
findMlangCommandsForFile(const fs::path& file)
{
    return walk_parents(file, [](const fs::path& dir)
    {
        if(regular_file(dir / "mlang_commands.json"))
            return std::optional<Configuration>{{dir, dir}};

        for(const char* buildDirectory : build_directories)
        {
            fs::path commandsDirectory = dir / buildDirectory;
            if(regular_file(commandsDirectory / "mlang_commands.json"))
                return std::optional<Configuration>{
                    {dir, std::move(commandsDirectory)}};
        }

        fs::path outBuild = dir / "out" / "build";
        if(regular_file(outBuild / "mlang_commands.json"))
            return std::optional<Configuration>{{dir, std::move(outBuild)}};
        return std::optional<Configuration>{};
    });
}
} // namespace project_lsp
