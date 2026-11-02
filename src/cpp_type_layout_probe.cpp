#include "cpp_type_layout_probe.h"
#include "cpp_size_probe_options.h"
#include "process_pipe.h"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace
{
struct ProbeDirectory
{
    std::filesystem::path path;

    ~ProbeDirectory()
    {
        std::error_code ec;
        if(!path.empty())
            std::filesystem::remove_all(path, ec);
    }
};
} // namespace

std::optional<DebugTypeLayout> probeCppTypeLayout(
    const std::vector<std::string>& lines,
    const std::string& path,
    const std::string& expression,
    int cursorLine,
    const std::string& typeExpression)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto temp = fs::temp_directory_path(ec);
    if(ec)
        return {};
    const auto stamp = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    ProbeDirectory directory;
    const auto candidate = temp / ("uvim_cpp_layout_" + stamp);
    if(!fs::create_directory(candidate, ec))
        return {};
    directory.path = candidate;
    const auto source = directory.path / "source.cpp";
    const auto output = directory.path / "layout.ll";
    {
        std::ofstream stream(source);
        for(const auto& line : lines)
            stream << line << '\n';
        if(!stream)
            return {};
    }
    const auto options = cppSizeProbeOptions(path);
    const auto sourceDirectory = path.empty()
                                     ? fs::current_path(ec)
                                     : fs::absolute(path, ec).parent_path();
    if(ec)
        return {};
    std::vector<std::string> args{"clang++", "-std=c++20"};
    args.insert(args.end(), options.flags.begin(), options.flags.end());
    args.insert(
        args.end(),
        {"-g",
         "-O0",
         "-S",
         "-emit-llvm",
         "-fstandalone-debug",
         "-Wno-everything",
         "-iquote",
         sourceDirectory.string(),
         source.string(),
         "-o",
         output.string()});
    auto compile = [&]() -> std::optional<std::string>
    {
        ProcessPipe pipe;
        if(!pipe.open(args, "r", true, options.directory))
            return {};
        pipe.readAll();
        if(pipe.close() != 0)
            return {};
        std::ifstream input(output);
        if(!input)
            return {};
        return std::string(std::istreambuf_iterator<char>(input), {});
    };
    auto ir = compile();
    if(!ir)
        return {};
    if(auto result = parseDebugTypeMetadata(*ir, expression, cursorLine))
        return result;
    // Materialize a type name or a variable's inferred type even if the
    // compiler did not retain the declaration's complete debug description.
    {
        std::ofstream append(source, std::ios::app);
        append << "\nvoid __uvim_layout_" << stamp << "("
               << (typeExpression.empty() ? expression : typeExpression)
               << " __uvim_layout_value) {}\n";
        if(!append)
            return {};
    }
    ir = compile();
    return ir ? parseDebugTypeMetadata(
                    *ir,
                    "__uvim_layout_value",
                    static_cast<int>(lines.size()) + 2)
              : std::nullopt;
}
