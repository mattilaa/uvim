#include "cpp_size_probe_options.h"
#include "json_utils.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <unordered_set>

namespace
{
namespace fs = std::filesystem;

bool splitArguments(std::string_view command, std::vector<std::string>& args)
{
    std::string word;
    char quote = 0;
    bool started = false;
    for(size_t i = 0; i < command.size(); ++i)
    {
        const char ch = command[i];
        if(ch == '\\' && i + 1 < command.size() && quote != '\'' &&
           (command[i + 1] == '"' || command[i + 1] == '\\' ||
            (!quote && (command[i + 1] == ' ' || command[i + 1] == '\''))))
        {
            word += command[++i];
            started = true;
        }
        else if(quote)
        {
            if(ch == quote)
                quote = 0;
            else
                word += ch;
            started = true;
        }
        else if(ch == '\'' || ch == '"')
        {
            quote = ch;
            started = true;
        }
        else if(ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
        {
            if(started)
                args.push_back(std::move(word));
            word.clear();
            started = false;
        }
        else
        {
            word += ch;
            started = true;
        }
    }
    if(started)
        args.push_back(std::move(word));
    return !quote;
}

fs::path absolute(const fs::path& path, const fs::path& base)
{
    std::error_code ec;
    auto result =
        fs::weakly_canonical(path.is_absolute() ? path : base / path, ec);
    return ec ? (path.is_absolute() ? path : base / path).lexically_normal()
              : result;
}

std::vector<std::string> probeFlags(const std::vector<std::string>& args)
{
    std::vector<std::string> result;
    static const std::unordered_set<std::string> operands =
        {"-I",
         "-isystem",
         "-iquote",
         "-idirafter",
         "-include",
         "-imacros",
         "-D",
         "-U",
         "-isysroot",
         "--sysroot",
         "-resource-dir",
         "-target",
         "--target",
         "-arch",
         "-F",
         "-iframework",
         "-std"};
    for(size_t i = 1; i < args.size(); ++i)
    {
        const auto& flag = args[i];
        if(operands.contains(flag))
        {
            if(i + 1 < args.size())
            {
                result.push_back(flag);
                result.push_back(args[++i]);
            }
        }
        else if(
            flag.starts_with("-I") || flag.starts_with("-D") ||
            flag.starts_with("-U") || flag.starts_with("-F") ||
            flag.starts_with("-std=") || flag.starts_with("-stdlib=") ||
            flag.starts_with("--sysroot=") || flag.starts_with("--target=") ||
            flag.starts_with("-isystem") || flag.starts_with("-iquote") ||
            (flag.starts_with("-m") && flag != "-mllvm") ||
            flag.starts_with("-fpack-struct") || flag == "-fshort-enums" ||
            flag == "-fshort-wchar" || flag == "-fsigned-char" ||
            flag == "-funsigned-char" || flag == "-fms-extensions" ||
            flag == "-fms-compatibility" || flag == "-pthread")
            result.push_back(flag);
    }
    return result;
}
} // namespace

CppSizeProbeOptions cppSizeProbeOptions(const std::string& sourcePath)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto cwd = fs::current_path(ec);
    const auto source = absolute(sourcePath, cwd);
    CppSizeProbeOptions result{cwd.string(), {}};
    if(sourcePath.empty())
        return result;
    std::vector<fs::path> databases;
    for(auto directory = source.parent_path(); !directory.empty();)
    {
        databases.push_back(directory / "compile_commands.json");
        for(const char* build :
            {"build",
             "build-debug",
             "build-release",
             "cmake-build-debug",
             "cmake-build-release",
             "out/build"})
            databases.push_back(directory / build / "compile_commands.json");
        const auto parent = directory.parent_path();
        if(parent == directory)
            break;
        directory = parent;
    }
    const auto extension = source.extension().string();
    const bool header = extension == ".h" || extension == ".hpp" ||
                        extension == ".hh" || extension == ".hxx";
    for(const auto& database : databases)
    {
        std::ifstream input(database);
        json_utils::Document document;
        if(!input || !json_utils::parse(document, input) || !document.IsArray())
            continue;
        std::optional<CppSizeProbeOptions> closest;
        size_t best = std::numeric_limits<size_t>::max();
        for(const auto& entry : document.GetArray())
        {
            const auto file = json_utils::get_string(entry, "file");
            if(file.empty())
                continue;
            const auto directory = absolute(
                json_utils::get_string(
                    entry,
                    "directory",
                    database.parent_path().string()),
                database.parent_path());
            const auto candidate = absolute(file, directory);
            if(candidate != source && !header)
                continue;
            std::vector<std::string> args;
            if(const auto* values = json_utils::find(entry, "arguments");
               values && values->IsArray())
            {
                for(const auto& value : values->GetArray())
                    if(value.IsString())
                        args.emplace_back(
                            value.GetString(),
                            value.GetStringLength());
            }
            else if(!splitArguments(
                        json_utils::get_string(entry, "command"),
                        args))
                continue;
            if(args.empty())
                continue;
            CppSizeProbeOptions options{directory.string(), probeFlags(args)};
            if(candidate == source)
                return options;
            // Headers normally inherit the command of a nearby translation
            // unit.
            const auto relative = candidate.parent_path().lexically_relative(
                source.parent_path());
            const size_t score =
                static_cast<size_t>(
                    std::distance(relative.begin(), relative.end())) *
                    2 +
                (candidate.stem() == source.stem() ? 0 : 1);
            if(score < best)
            {
                closest = std::move(options);
                best = score;
            }
        }
        if(closest)
            return *closest;
    }
    return result;
}
