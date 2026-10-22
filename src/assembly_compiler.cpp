#include "assembly_compiler.h"
#include "assembly_output.h"
#include "executable_lookup.h"
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
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>

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
        else if(c == ' ' || c == '\t')
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

std::wstring quoted(std::wstring_view arg)
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
        command += (command.empty() ? L"" : L" ") + quoted(wide(arg));
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
                error = "asm: --compiler requires clang, gcc, or an executable "
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

Result compile(const Request& request, const std::function<bool()>& cancelled)
{
    Result result;
    auto fail = [&](std::string message)
    {
        result.diagnostics = std::move(message);
        return result;
    };
    std::string ext = fs::path(request.path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
    const bool cSource = fs::path(request.path).extension() == ".c";
    if(!ext.empty() && ext != ".c" && ext != ".cc" && ext != ".cpp" &&
       ext != ".cxx" && ext != ".c++" && ext != ".h" && ext != ".hpp" &&
       ext != ".hxx" && ext != ".hh")
        return fail("asm: C/C++ source and header files only");

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

    std::string compiler = request.compiler;
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
    const auto input = temp.path / (cSource ? "source.c" : "source.cpp");
    const auto output = temp.path / "output.s";
    {
        std::ofstream stream(input, std::ios::binary);
        stream << assembly_output::sourceText(source, cSource);
        if(!stream)
            return fail("asm: cannot write temporary source");
    }
    std::vector<std::string> args = {result.compiler, "-x",
                                     cSource ? "c" : "c++",
                                     cSource ? "-std=c11" : "-std=c++20"};
    const bool hasOptimization = std::any_of(
        request.flags.begin(), request.flags.end(),
        [](const std::string& flag) { return flag.rfind("-O", 0) == 0; });
    if(!hasOptimization)
        args.push_back("-O0");
    args.insert(args.end(), request.flags.begin(), request.flags.end());
    result.options = hasOptimization ? "" : "-O0";
    for(const auto& flag : request.flags)
        result.options += (result.options.empty() ? "" : " ") + flag;
    const auto sourceDir = fs::path(request.path).parent_path();
    args.insert(args.end(), {"-S", "-g", "-gdwarf-4", "-fverbose-asm",
                             "-fdiagnostics-color=never", "-iquote",
                             sourceDir.empty() ? "." : sourceDir.string(), "-o",
                             output.string(), input.string()});
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
