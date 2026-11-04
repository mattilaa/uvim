#include "cpp_type_layout_probe.h"
#include "cpp_size_probe_options.h"
#include "process_pipe.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <regex>

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

std::optional<std::string> compileDebugSource(
    const std::vector<std::string>& lines,
    const std::string& path,
    std::string* error = nullptr)
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
    const bool cSource = std::filesystem::path(path).extension() == ".c";
    const auto source = directory.path / (cSource ? "source.c" : "source.cpp");
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
    std::vector<std::string> args{
        cSource ? "clang" : "clang++",
        cSource ? "-std=c11" : "-std=c++20"};
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
    ProcessPipe pipe;
    if(!pipe.open(args, "r", true, options.directory))
    {
        if(error)
            *error = "could not start clang";
        return {};
    }
    const auto diagnostics = pipe.readAll();
    if(pipe.close() != 0)
    {
        if(error)
        {
            static const std::regex pattern(
                R"((?:fatal error|error): ([^\r\n]+))");
            std::smatch match;
            *error = std::regex_search(diagnostics, match, pattern)
                         ? match[1].str()
                         : "clang could not compile the source";
        }
        return {};
    }
    if(error)
        error->clear();
    std::ifstream input(output);
    if(!input)
        return {};
    return std::string(std::istreambuf_iterator<char>(input), {});
}
} // namespace

std::optional<DebugTypeLayout> probeCppTypeLayout(
    const std::vector<std::string>& lines,
    const std::string& path,
    const std::string& expression,
    int cursorLine,
    const std::string& typeExpression)
{
    auto ir = compileDebugSource(lines, path);
    if(!ir)
        return {};
    if(auto result = parseDebugTypeMetadata(*ir, expression, cursorLine))
        return result;
    auto probe = lines;
    probe.push_back("");
    probe.push_back(
        "void __uvim_layout_type_" +
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) +
        "(" + (typeExpression.empty() ? expression : typeExpression) +
        " __uvim_layout_value) {}");
    ir = compileDebugSource(probe, path);
    return ir ? parseDebugTypeMetadata(
                    *ir,
                    "__uvim_layout_value",
                    static_cast<int>(lines.size()) + 2)
              : std::nullopt;
}

std::optional<DebugTypeLayout> probeCppReturnLayout(
    const std::vector<std::string>& lines,
    const std::string& path,
    const CursorCall& call,
    int cursorColumn,
    std::string& error)
{
    const bool cSource = std::filesystem::path(path).extension() == ".c";
    const auto stamp = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string marker = "__uvim_return_probe_" + stamp;
    for(const std::string capture : {"[&]", "[]"})
    {
        // Return types and overloads are resolved without executing user code.
        // Keep the original call's value category in this compiler-only copy.
        const std::string replacement =
            cSource ? "({ __typeof__(" + call.expression + ") (*" + marker +
                          ")() = 0; (void)" + marker +
                          "; volatile int __uvim_barrier = 0; "
                          "(void)__uvim_barrier; " +
                          call.expression + "; })"
                    : "(" + capture + "() -> decltype(" + call.expression +
                          ") { decltype(" + call.expression + ") (*" + marker +
                          ")() = nullptr; (void)" + marker +
                          "; volatile int __uvim_barrier = 0; "
                          "(void)__uvim_barrier; return " +
                          call.expression + "; }())";
        auto probe = lines;
        probe[call.startLine] =
            lines[call.startLine].substr(0, call.startColumn) + replacement +
            lines[call.endLine].substr(call.endColumn);
        probe.erase(
            probe.begin() + call.startLine + 1,
            probe.begin() + call.endLine + 1);
        if(auto ir = compileDebugSource(probe, path, &error))
            if(auto result = parseDebugFunctionPointerReturn(*ir, marker))
                return result;
        if(cSource)
            break;
    }
    // Definitions can use the compiler's subroutine metadata directly.
    if(auto ir = compileDebugSource(lines, path, &error))
        if(auto result = parseDebugCallReturn(
               *ir,
               call.callee,
               call.startLine + 1,
               cursorColumn + 1))
            return result;
    // A constant expression at file scope can expose its exact type directly.
    auto probe = lines;
    probe.push_back(
        (cSource ? "__typeof__(" : "decltype(") + call.expression + ") (*" +
        marker + ")() = 0;");
    if(auto ir = compileDebugSource(probe, path))
        if(auto result = parseDebugFunctionPointerReturn(*ir, marker))
            return result;
    // An unambiguous external declaration need not have a definition here.
    probe = lines;
    probe.push_back(
        (cSource ? "__typeof__(&" : "decltype(&") + call.callee + ") " +
        marker + " = 0;");
    if(auto ir = compileDebugSource(probe, path))
        return parseDebugFunctionPointerReturn(*ir, marker);
    return {};
}
