#include "mlang_size_probe.h"

#include "process_pipe.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>
#include <unordered_map>

namespace
{
std::string quote(const std::string& value)
{
    std::string result = "'";
    for(char ch : value)
        result += ch == '\'' ? "'\\''" : std::string(1, ch);
    return result + "'";
}

struct ProbeFiles
{
    std::filesystem::path directory, source;

    ~ProbeFiles()
    {
        std::error_code ec;
        if(!source.empty())
            std::filesystem::remove(source, ec);
        if(!directory.empty())
            std::filesystem::remove_all(directory, ec);
    }
};
} // namespace

std::optional<DebugTypeLayout> probeMlangSize(
    const std::vector<std::string>& lines,
    const std::string& path,
    const std::string& expression,
    int cursorLine,
    std::string& error)
{
    namespace fs = std::filesystem;
    const auto stamp = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    ProbeFiles files;
    std::error_code ec;
    const auto temp = fs::temp_directory_path(ec);
    if(ec || !fs::create_directory(temp / ("uvim_mlang_size_" + stamp), ec))
    {
        error = "cannot create temporary compiler directory";
        return {};
    }
    files.directory = temp / ("uvim_mlang_size_" + stamp);
    auto sourceDirectory = path.empty()
                               ? fs::current_path(ec)
                               : fs::absolute(fs::path(path), ec).parent_path();
    if(ec)
    {
        error = "cannot resolve source directory";
        return {};
    }
    files.source = sourceDirectory / (".uvim_size_" + stamp + ".mla");
    FILE* stream = std::fopen(files.source.string().c_str(), "wbx");
    if(!stream)
    {
        files.source.clear();
        error = "cannot create compiler snapshot beside source";
        return {};
    }
    bool written = true;
    for(const auto& line : lines)
        written =
            (std::fwrite(line.data(), 1, line.size(), stream) == line.size() &&
             std::fputc('\n', stream) != EOF) &&
            written;
    const bool closed = std::fclose(stream) == 0;
    if(!written || !closed)
    {
        error = "cannot write compiler snapshot";
        return {};
    }
    const char* configured = std::getenv("UVIM_MLANG_COMPILER");
    const std::string compiler =
        configured && *configured ? configured : "mlang";
    const auto output = files.directory / "layout.ll";
    auto compile = [&]() -> std::optional<std::string>
    {
        fs::remove(output, ec);
        ProcessPipe pipe(
            "cd " + quote(files.directory.string()) + " && " + quote(compiler) +
                " -g -O0 -emit-llvm " + quote(files.source.string()) + " -o " +
                quote(output.string()) + " 2>&1",
            "r");
        const auto diagnostics =
            pipe ? pipe.readAll() : "compiler could not start";
        const int status = pipe.close();
        if(status != 0)
        {
            error = "mlang failed: " + diagnostics.substr(0, 240);
            return {};
        }
        std::ifstream input(output);
        if(!input)
        {
            error = "mlang did not produce debug type metadata";
            return {};
        }
        return std::string(std::istreambuf_iterator<char>(input), {});
    };
    auto ir = compile();
    if(!ir)
        return {};
    if(auto result = parseDebugTypeMetadata(*ir, expression, cursorLine))
        return result;
    // Unused types are not always retained in DWARF. A never-executed function
    // parameter asks the compiler to describe the exact requested type.
    const std::string probeName = "__uvim_size_" + stamp;
    {
        std::ofstream append(files.source, std::ios::app);
        append << "\nfn " << probeName << "(value: " << expression
               << ") -> u64 { return size_of(value); }\n";
        if(!append)
        {
            error = "cannot write type probe";
            return {};
        }
    }
    ir = compile();
    if(!ir)
        return {};
    // The synthetic parameter has the instantiated type, including generics.
    if(auto result = parseDebugTypeMetadata(
           *ir,
           "value",
           static_cast<int>(lines.size()) + 2))
        return result;
    error = "type metadata unavailable for " + expression;
    return {};
}
