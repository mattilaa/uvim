#include "assembly_compiler.h"
#include "assembly_output.h"
#include "executable_lookup.h"
#include "json_utils.h"
#include "process_pipe.h"
#ifdef _WIN32
#define NOMINMAX
#include "process_launch.h"
#include <windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace assembly_compiler
{
namespace
{
bool splitArguments(std::string_view input, std::vector<std::string>& words)
{
    std::string word;
    char quote = 0;
    bool started = false;
    for(size_t i = 0; i < input.size(); ++i)
    {
        const char c = input[i];
        // Preserve Windows path separators. Backslashes escape only quotes,
        // a second backslash, or an unquoted space.
        if(c == '\\' && i + 1 < input.size() && quote != '\'' &&
           (input[i + 1] == '"' || input[i + 1] == '\\' ||
            (!quote && (input[i + 1] == ' ' || input[i + 1] == '\''))))
        {
            word += input[++i];
            started = true;
        }
        else if(quote)
        {
            if(c == quote)
                quote = 0;
            else
                word += c;
            started = true;
        }
        else if(c == '\'' || c == '"')
        {
            quote = c;
            started = true;
        }
        else if(c == ' ' || c == '\t' || c == '\r' || c == '\n')
        {
            if(started)
                words.push_back(std::move(word));
            word.clear();
            started = false;
        }
        else
        {
            word += c;
            started = true;
        }
    }
    if(quote)
        return false;
    if(started)
        words.push_back(std::move(word));
    return true;
}

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while(std::getline(stream, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(std::move(line));
    }
    return lines;
}

std::string readFile(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}

fs::path absolutePath(const fs::path& path, const fs::path& directory)
{
    return (path.is_absolute() ? path : directory / path).lexically_normal();
}

bool sameFile(const fs::path& left, const fs::path& right)
{
    std::error_code ec;
    if(fs::equivalent(left, right, ec))
        return true;
    const auto a = fs::weakly_canonical(left, ec);
    if(ec)
        return left.lexically_normal() == right.lexically_normal();
    const auto b = fs::weakly_canonical(right, ec);
    return !ec && a == b;
}

struct CompileCommand
{
    std::string database{};
    std::string directory{};
    std::string compiler{};
    std::vector<std::string> flags{};
    std::string error{};
};

bool expandResponseFiles(const std::vector<std::string>& args,
                         const fs::path& directory,
                         std::vector<std::string>& expanded, std::string& error,
                         int depth = 0)
{
    for(const auto& arg : args)
    {
        if(!arg.starts_with('@'))
        {
            expanded.push_back(arg);
            continue;
        }
        const auto path = absolutePath(arg.substr(1), directory);
        std::ifstream stream(path);
        std::vector<std::string> nested;
        if(depth >= 8 || !stream ||
           !splitArguments(
               std::string(std::istreambuf_iterator<char>(stream), {}), nested))
        {
            error = "asm: cannot read compiler response file: " + path.string();
            return false;
        }
        if(!expandResponseFiles(nested, directory, expanded, error, depth + 1))
            return false;
    }
    return true;
}

bool includeSearchOption(std::string_view flag)
{
    return flag == "-I" || flag == "-isystem" || flag == "-iquote" ||
           flag == "-idirafter" || flag == "-F" || flag == "-iframework";
}

bool pathOption(std::string_view flag)
{
    return includeSearchOption(flag) || flag == "-include" ||
           flag == "-imacros" || flag == "-isysroot" || flag == "--sysroot" ||
           flag == "-include-pch" || flag == "-fmodule-map-file";
}

std::vector<std::string> promptPaths(const std::vector<std::string>& flags,
                                     const fs::path& directory)
{
    std::vector<std::string> result;
    for(size_t i = 0; i < flags.size(); ++i)
    {
        const auto& flag = flags[i];
        result.push_back(flag);
        if(pathOption(flag) && i + 1 < flags.size())
            result.push_back(absolutePath(flags[++i], directory).string());
        else
        {
            for(std::string_view prefix :
                {"-isystem", "-iquote", "-idirafter", "-iframework",
                 "--sysroot=", "-fmodule-map-file=", "-I", "-F"})
                if(flag.starts_with(prefix) && flag.size() > prefix.size())
                {
                    result.back() =
                        std::string(prefix) +
                        absolutePath(flag.substr(prefix.size()), directory)
                            .string();
                    break;
                }
        }
    }
    return result;
}

std::string overrideKey(const std::string& flag, const std::string& next)
{
    if(flag.starts_with("-O"))
        return "optimization";
    if(flag.starts_with("-std=") || flag.starts_with("--std="))
        return "standard";
    if(flag.starts_with("-D") || flag.starts_with("-U"))
    {
        const std::string macro = flag.size() == 2 ? next : flag.substr(2);
        return "macro:" + macro.substr(0, macro.find_first_of("=("));
    }
    return {};
}

std::vector<std::string> commandFlags(const std::vector<std::string>& args,
                                      size_t start, const fs::path& file,
                                      const fs::path& directory,
                                      const std::vector<std::string>& prompt)
{
    std::unordered_set<std::string> overrides;
    for(size_t i = 0; i < prompt.size(); ++i)
    {
        const auto key =
            overrideKey(prompt[i], i + 1 < prompt.size() ? prompt[i + 1] : "");
        if(!key.empty())
            overrides.insert(key);
        if((pathOption(prompt[i]) || prompt[i] == "-D" || prompt[i] == "-U") &&
           i + 1 < prompt.size())
            ++i;
    }
    std::vector<std::string> flags;
    for(size_t i = start; i < args.size(); ++i)
    {
        const auto& flag = args[i];
        // The assembly view owns its input, output and line-table generation.
        if(flag == "-o" || flag == "--output" || flag == "-MF" ||
           flag == "-MT" || flag == "-MQ" || flag == "-MJ" ||
           flag == "-serialize-diagnostics")
        {
            if(i + 1 < args.size())
                ++i;
            continue;
        }
        if(flag == "-c" || flag == "-S" || flag == "-E" || flag == "-M" ||
           flag == "-MM" || flag == "-MD" || flag == "-MMD" || flag == "-MP" ||
           flag == "-MG" || flag == "-fsyntax-only" || flag == "--analyze" ||
           flag == "-emit-llvm" || flag == "--" || flag.starts_with("-o") ||
           flag.starts_with("--output=") || flag.starts_with("-MF") ||
           flag.starts_with("-MT") || flag.starts_with("-MQ") ||
           flag.starts_with("-MJ") || flag.starts_with("-Wp,-M") ||
           flag.starts_with("-save-temps") ||
           flag.starts_with("--save-temps") ||
           flag.starts_with("-ftime-trace") ||
           flag.starts_with("--serialize-diagnostics="))
            continue;
        const auto key =
            overrideKey(flag, i + 1 < args.size() ? args[i + 1] : "");
        if(!key.empty() && overrides.contains(key))
        {
            if((flag == "-D" || flag == "-U") && i + 1 < args.size())
                ++i;
            continue;
        }
        if(!flag.empty() && flag.front() != '-' &&
           sameFile(absolutePath(flag, directory), file))
            continue;
        flags.push_back(flag);
        // Keep option operands intact, even if they resemble another flag.
        if((pathOption(flag) || flag == "-D" || flag == "-U" || flag == "-x" ||
            flag == "-target" || flag == "--target" || flag == "-Xclang" ||
            flag == "-Xpreprocessor") &&
           i + 1 < args.size())
            flags.push_back(args[++i]);
    }
    return flags;
}

CompileCommand findCompileCommand(const Request& request,
                                  const std::vector<std::string>& prompt,
                                  const std::function<bool()>& cancelled)
{
    std::vector<fs::path> candidates;
    if(!request.compileCommandsDirectory.empty())
    {
        fs::path configured(request.compileCommandsDirectory);
        candidates.push_back(configured.filename() == "compile_commands.json"
                                 ? configured
                                 : configured / "compile_commands.json");
    }
    std::unordered_set<std::string> seenDirectories;
    auto addAncestors = [&](fs::path directory)
    {
        while(!directory.empty() &&
              seenDirectories.insert(directory.string()).second)
        {
            candidates.push_back(directory / "compile_commands.json");
            for(const char* build :
                {"build", "build-debug", "build-release", "build_Debug",
                 "build_Release", "Debug", "Release", "cmake-build-debug",
                 "cmake-build-release", "out/build"})
                candidates.push_back(directory / build /
                                     "compile_commands.json");
            const auto parent = directory.parent_path();
            if(parent == directory)
                break;
            directory = parent;
        }
    };
    addAncestors(fs::path(request.path).parent_path());
    addAncestors(request.directory);
    std::unordered_set<std::string> seenDatabases;
    for(const auto& path : candidates)
    {
        if(cancelled())
            return {};
        if(!seenDatabases.insert(path.string()).second)
            continue;
        std::ifstream in(path);
        if(!in)
            continue;
        json_utils::Document document;
        if(!json_utils::parse(document, in) || !document.IsArray())
            return {.error =
                        "asm: invalid compilation database: " + path.string()};
        for(const auto& item : document.GetArray())
        {
            if(cancelled())
                return {};
            const auto filename = json_utils::get_string(item, "file");
            if(filename.empty())
                continue;
            const auto directory = absolutePath(
                json_utils::get_string(item, "directory",
                                       path.parent_path().string()),
                path.parent_path());
            if(!sameFile(absolutePath(filename, directory), request.path))
                continue;
            CompileCommand command;
            command.database = path.string();
            command.directory = directory.string();
            std::vector<std::string> args, expanded;
            const auto* arguments = json_utils::find(item, "arguments");
            if(arguments && arguments->IsArray())
            {
                for(const auto& arg : arguments->GetArray())
                {
                    if(!arg.IsString())
                    {
                        command.error =
                            "asm: non-string compilation database argument: " +
                            path.string();
                        return command;
                    }
                    args.emplace_back(arg.GetString(), arg.GetStringLength());
                }
            }
            else if(!splitArguments(json_utils::get_string(item, "command"),
                                    args))
                command.error =
                    "asm: unmatched quote in compilation database: " +
                    path.string();
            if(!command.error.empty())
                return command;
            if(args.empty() || args.front().empty())
            {
                command.error =
                    "asm: missing compiler in compilation database: " +
                    path.string();
                return command;
            }
            if(!expandResponseFiles(args, directory, expanded, command.error))
                return command;
            if(expanded.empty())
            {
                command.error =
                    "asm: empty compilation command: " + path.string();
                return command;
            }
            size_t compiler = 0;
            while(compiler + 1 < expanded.size())
            {
                auto name = fs::path(expanded[compiler]).stem().string();
                if(name != "ccache" && name != "sccache" && name != "distcc" &&
                   name != "icecc")
                    break;
                ++compiler;
            }
            command.compiler = expanded[compiler];
            if(fs::path(command.compiler).has_parent_path())
                command.compiler =
                    absolutePath(command.compiler, directory).string();
            command.flags = commandFlags(expanded, compiler + 1, request.path,
                                         directory, prompt);
            return command;
        }
    }
    return {};
}

std::vector<int> sourceLocations(const std::vector<std::string>& lines,
                                 const fs::path& source, size_t sourceCount)
{
    std::unordered_map<int, bool> mainFiles;
    for(const auto& line : lines)
    {
        std::istringstream stream(line);
        std::string directive, filename, second;
        int number;
        if(!(stream >> directive) || directive != ".file" ||
           !(stream >> number >> std::quoted(filename)))
            continue;
        stream >> std::ws;
        if(stream.peek() == '"' && stream >> std::quoted(second))
            filename = (fs::path(filename) / second).string();
        std::error_code ec;
        mainFiles[number] = fs::equivalent(fs::path(filename), source, ec);
    }
    std::vector<int> locations;
    int current = 0;
    for(const auto& line : lines)
    {
        std::istringstream stream(line);
        std::string word;
        stream >> word;
        if(word == ".loc")
        {
            int file = -1, row = 0;
            stream >> file >> row;
            current = mainFiles[file] && row > 0 &&
                              static_cast<size_t>(row) <= sourceCount
                          ? row
                          : 0;
        }
        // Directives, labels and comments do not represent source statements.
        const bool instruction =
            !word.empty() &&
            std::isalpha(static_cast<unsigned char>(word.front())) &&
            word.back() != ':';
        locations.push_back(instruction ? current : 0);
        if(word == ".cfi_endproc" || word == ".seh_endproc" ||
           line.find("-- Begin function") != std::string::npos)
            current = 0;
    }
    return locations;
}

struct TemporaryDirectory
{
    fs::path path;

    ~TemporaryDirectory()
    {
        std::error_code ec;
        if(!path.empty())
            fs::remove_all(path, ec);
    }
};

struct TemporarySource
{
    fs::path path;

    ~TemporarySource()
    {
        std::error_code ec;
        if(!path.empty())
            fs::remove(path, ec);
    }
};

#ifdef _WIN32
struct Handle
{
    HANDLE value = INVALID_HANDLE_VALUE;

    ~Handle()
    {
        if(value != INVALID_HANDLE_VALUE && value != nullptr)
            CloseHandle(value);
    }
};

std::wstring wide(std::string_view text)
{
    const int length =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(length, L'\0');
    if(length)
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), result.data(),
                            length);
    return result;
}

std::wstring quoteWindowsArgument(std::wstring_view arg)
{
    std::wstring result = L"\"";
    size_t slashes = 0;
    for(wchar_t c : arg)
    {
        if(c == L'\\')
            ++slashes;
        else
        {
            result.append(c == L'"' ? 2 * slashes + 1 : slashes, L'\\');
            result += c;
            slashes = 0;
        }
    }
    result.append(2 * slashes, L'\\');
    return result + L'"';
}

int runCompiler(const std::vector<std::string>& args, const fs::path& log,
                const std::string& directory,
                const std::function<bool()>& cancelled)
{
    // Launch directly, without cmd.exe: paths with spaces and compiler flags
    // containing shell metacharacters must reach the compiler unchanged.
    std::wstring command;
    for(const auto& arg : args)
        command += (command.empty() ? L"" : L" ") + quoteWindowsArgument(wide(arg));
    const auto executable = wide(args.front());
    const auto workingDirectory = wide(directory);
    std::unique_lock<std::mutex> lock(processLaunchMutex());
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle output{CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                              &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                              nullptr)};
    Handle input{CreateFileW(L"NUL", GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if(output.value == INVALID_HANDLE_VALUE ||
       input.value == INVALID_HANDLE_VALUE)
        return -1;
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.value;
    startup.StartupInfo.hStdOutput = output.value;
    startup.StartupInfo.hStdError = output.value;
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<unsigned char> storage(size);
    startup.lpAttributeList =
        reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if(!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size))
        return -1;
    HANDLE inherited[] = {input.value, output.value};
    const BOOL attributes = UpdateProcThreadAttribute(
        startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        inherited, sizeof(inherited), nullptr, nullptr);
    PROCESS_INFORMATION process{};
    const BOOL launched =
        attributes &&
        CreateProcessW(
            executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
            workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
            &startup.StartupInfo, &process);
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    if(!launched)
        return -1;
    Handle processHandle{process.hProcess};
    Handle threadHandle{process.hThread};
    lock.unlock();
    while(WaitForSingleObject(processHandle.value, 50) == WAIT_TIMEOUT)
        if(cancelled())
            TerminateProcess(processHandle.value, 1);
    DWORD status = 1;
    GetExitCodeProcess(processHandle.value, &status);
    return static_cast<int>(status);
}
#endif
} // namespace

bool isMlangSource(std::string_view path)
{
    std::string extension = fs::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
    return extension == ".mla";
}

bool parse(std::string_view arguments, Request& request, std::string& error)
{
    request = Request{};
    std::vector<std::string> words;
    if(!splitArguments(arguments, words))
    {
        error = "asm: unmatched quote";
        return false;
    }
    bool hasPath = false;
    bool flagsStarted = false;
    for(size_t i = 0; i < words.size(); ++i)
    {
        const auto& word = words[i];
        if(word == "--")
        {
            request.flags.insert(request.flags.end(), words.begin() + i + 1,
                                 words.end());
            break;
        }
        if(word == "--compiler")
        {
            if(++i == words.size() || words[i].empty())
            {
                error = "asm: --compiler requires clang, gcc, mlang, or an "
                        "executable "
                        "path";
                return false;
            }
            request.compiler = words[i];
        }
        else if(word.rfind("--compiler=", 0) == 0)
        {
            request.compiler = word.substr(11);
            if(request.compiler.empty())
            {
                error = "asm: --compiler requires a value";
                return false;
            }
        }
        else if(word == "--raw")
            request.raw = true;
        else if(word == "--split")
            request.split = true;
        else if(!hasPath && !flagsStarted && !word.empty() &&
                word.front() != '-')
        {
            request.path = word;
            hasPath = true;
        }
        else
        {
            flagsStarted = true;
            request.flags.push_back(word);
        }
    }
    return true;
}

Result compile(const Request& inputRequest,
               const std::function<bool()>& cancelled)
{
    Request request = inputRequest;
    Result result;
    auto fail = [&](std::string message)
    {
        result.diagnostics = std::move(message);
        return result;
    };
    std::error_code pathError;
    const auto current = fs::current_path(pathError);
    if(pathError)
        return fail("asm: cannot resolve working directory");
    const auto promptDirectory = request.directory.empty()
                                     ? current
                                     : absolutePath(request.directory, current);
    request.directory = promptDirectory.string();
    request.path = absolutePath(request.path, promptDirectory).string();
    if(!request.compileCommandsDirectory.empty())
        request.compileCommandsDirectory =
            absolutePath(request.compileCommandsDirectory, promptDirectory)
                .string();
    if(fs::path(request.compiler).has_parent_path())
        request.compiler =
            absolutePath(request.compiler, promptDirectory).string();
    std::string ext = fs::path(request.path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
    const bool cSource = fs::path(request.path).extension() == ".c";
    const bool mlangSource = isMlangSource(request.path);
    if(!ext.empty() && ext != ".c" && ext != ".cc" && ext != ".cpp" &&
       ext != ".cxx" && ext != ".c++" && ext != ".h" && ext != ".hpp" &&
       ext != ".hxx" && ext != ".hh" && !mlangSource)
        return fail("asm: C/C++ source, headers, and Mlang (.mla) files only");

    std::vector<std::string> source;
    if(request.sourceLines)
        source = *request.sourceLines;
    else
    {
        std::ifstream in(request.path);
        if(!in)
            return fail("asm: cannot read " + request.path);
        source =
            splitLines(std::string(std::istreambuf_iterator<char>(in), {}));
    }
    if(cancelled())
        return fail("asm: compilation cancelled");

    result.sourceLines = source;
    result.sourcePath = request.path;

    std::vector<std::string> prompt;
    std::string responseError;
    if(!expandResponseFiles(request.flags, promptDirectory, prompt,
                            responseError))
        return fail(std::move(responseError));
    const auto command = mlangSource
                             ? CompileCommand{}
                             : findCompileCommand(request, prompt, cancelled);
    result.compilationDatabase = command.database;
    if(!command.error.empty())
        return fail(command.error);
    if(cancelled())
        return fail("asm: compilation cancelled");
    if(!command.database.empty())
    {
        request.directory = command.directory;
        prompt = promptPaths(prompt, promptDirectory);
    }
    // User include directories are searched first; other user flags follow
    // inherited flags so ordinary compiler last-option-wins rules apply.
    std::vector<std::string> flags, trailing;
    for(size_t i = 0; i < prompt.size(); ++i)
    {
        const bool separate = includeSearchOption(prompt[i]);
        bool search = separate;
        for(std::string_view prefix :
            {"-I", "-F", "-isystem", "-iquote", "-idirafter", "-iframework"})
            search = search || (prompt[i].starts_with(prefix) &&
                                prompt[i].size() > prefix.size());
        auto& destination =
            search && !command.database.empty() ? flags : trailing;
        destination.push_back(prompt[i]);
        if((separate || pathOption(prompt[i]) || prompt[i] == "-D" ||
            prompt[i] == "-U" || prompt[i] == "-x" || prompt[i] == "-target" ||
            prompt[i] == "--target" || prompt[i] == "-Xclang" ||
            prompt[i] == "-Xpreprocessor") &&
           i + 1 < prompt.size())
            destination.push_back(prompt[++i]);
    }
    flags.insert(flags.end(), command.flags.begin(), command.flags.end());
    flags.insert(flags.end(), trailing.begin(), trailing.end());

    std::string compiler =
        request.compiler.empty() ? command.compiler : request.compiler;
    if(mlangSource && compiler.empty())
        compiler = "mlang";
    if(compiler == "clang")
        compiler = cSource ? "clang" : "clang++";
    else if(compiler == "gcc")
        compiler = cSource ? "gcc" : "g++";
    executable_lookup::Result found;
    if(!compiler.empty())
        found = executable_lookup::find(compiler);
    else
    {
        found = executable_lookup::find(cSource ? "clang" : "clang++");
#ifdef _WIN32
        if(!found.found)
        {
            const char* programs = std::getenv("ProgramFiles");
            const fs::path llvm =
                fs::path(programs ? programs : "C:/Program Files") / "LLVM" /
                "bin" / (cSource ? "clang.exe" : "clang++.exe");
            found = executable_lookup::find(llvm.string());
        }
#endif
        if(!found.found)
            found = executable_lookup::find(cSource ? "gcc" : "g++");
    }
    if(!found.found)
        return fail(compiler.empty()
                        ? "asm: install Clang or GCC, or use --compiler PATH"
                        : "asm: compiler not found: " + compiler);
    result.compiler = found.path;

    std::error_code ec;
    const auto base = fs::temp_directory_path(ec);
    if(ec)
        return fail("asm: cannot locate temporary directory");
    static std::atomic<unsigned long long> sequence{0};
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory temp{base / ("uvim_asm_" + std::to_string(stamp) + "_" +
                                    std::to_string(sequence++))};
    if(!fs::create_directory(temp.path, ec))
    {
        temp.path.clear();
        return fail("asm: cannot create temporary directory");
    }
    TemporarySource snapshot;
    const auto input = mlangSource
                           ? fs::path(request.path).parent_path() /
                                 ("." + temp.path.filename().string() + ".mla")
                           : temp.path / (cSource ? "source.c" : "source.cpp");
    const auto output = temp.path / "output.s";
    if(mlangSource)
    {
        // Keep relative modules/manifest discovery rooted at the original
        // source. Exclusive creation guarantees an existing file is never
        // overwritten.
#ifdef _WIN32
        FILE* stream = _wfopen(input.c_str(), L"wbx");
#else
        FILE* stream = std::fopen(input.c_str(), "wbx");
#endif
        if(!stream)
            return fail("asm: cannot create Mlang snapshot beside source "
                        "(directory must be writable): " +
                        input.string());
        snapshot.path = input;
        bool written = true;
        for(const auto& line : source)
        {
            written = (std::fwrite(line.data(), 1, line.size(), stream) ==
                       line.size()) &&
                      written;
            written = (std::fputc('\n', stream) != EOF) && written;
        }
        const bool closed = std::fclose(stream) == 0;
        if(!written || !closed)
            return fail("asm: cannot write temporary Mlang source");
        // Mlang writes its indexing database into cwd; keep it out of the
        // project.
        request.directory = temp.path.string();
    }
    else
    {
        std::ofstream stream(input, std::ios::binary);
        stream << assembly_output::sourceText(source, cSource);
        if(!stream)
            return fail("asm: cannot write temporary source");
    }
    std::vector<std::string> args = {result.compiler};
    if(!mlangSource)
    {
        args.insert(args.end(), {"-x", cSource ? "c" : "c++"});
        if(command.database.empty())
            args.push_back(cSource ? "-std=c11" : "-std=c++20");
    }
    const bool hasOptimization =
        std::any_of(flags.begin(), flags.end(), [](const std::string& flag)
                    { return flag.rfind("-O", 0) == 0; });
    if(!hasOptimization)
        args.push_back("-O0");
    const auto sourceDir = fs::path(request.path).parent_path();
    if(!mlangSource)
        args.insert(args.end(),
                    {"-iquote", sourceDir.empty() ? "." : sourceDir.string()});
    else
    {
        // Prompt paths still refer to the editor cwd, despite the isolated cwd.
        for(size_t i = 0; i < flags.size(); ++i)
        {
            if(flags[i] == "-L" && i + 1 < flags.size())
            {
                ++i;
                flags[i] = absolutePath(flags[i], promptDirectory).string();
            }
            else if(flags[i].starts_with("-L") && flags[i].size() > 2)
                flags[i] =
                    "-L" +
                    absolutePath(flags[i].substr(2), promptDirectory).string();
        }
    }
    args.insert(args.end(), flags.begin(), flags.end());
    result.options = hasOptimization ? "" : "-O0";
    for(const auto& flag : flags)
        result.options += (result.options.empty() ? "" : " ") + flag;
    args.insert(args.end(), {"-S", "-g"});
    if(!mlangSource)
        args.insert(args.end(), {"-gdwarf-4", "-fverbose-asm",
                                 "-fdiagnostics-color=never"});
    args.insert(args.end(), {"-o", output.string(), input.string()});
#ifdef _WIN32
    const auto log = temp.path / "compiler.log";
    const int status = runCompiler(args, log, request.directory, cancelled);
    result.diagnostics = readFile(log);
#else
    ProcessPipe pipe;
    if(!pipe.open(args, "r", true, request.directory))
        return fail("asm: failed to launch " + result.compiler);
    result.diagnostics = pipe.readAllBlocking(4096, cancelled);
    const int status = pipe.close();
#endif
    if(cancelled())
        return fail("asm: compilation cancelled");
    if(status != 0)
    {
        if(result.diagnostics.empty())
            result.diagnostics =
                "asm: compiler failed (exit " + std::to_string(status) + ")";
        return result;
    }
    const std::string assembly = readFile(output);
    const auto rawLines = splitLines(assembly);
    const auto locations = sourceLocations(rawLines, input, source.size());
    if(request.raw)
    {
        result.lines = rawLines;
        result.sourceLineForAssembly = locations;
    }
    else
    {
        std::vector<int> originalRows;
        result.lines =
            splitLines(assembly_output::compact(assembly, &originalRows));
        for(int row : originalRows)
            result.sourceLineForAssembly.push_back(
                row >= 0 && static_cast<size_t>(row) < locations.size()
                    ? locations[row]
                    : 0);
    }
    result.success = !result.lines.empty();
    if(!result.success)
        result.diagnostics = "asm: compiler produced no assembly";
    return result;
}
} // namespace assembly_compiler
