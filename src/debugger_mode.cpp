#include "debugger_mode.h"
#include "ascii.h"
#include "color_constant.h"
#include "debugger_bridge.h"
#include "editor.h"
#include "editor_path_utilities.h"
#include "editor_utils.h"
#include "json_utils.h"
#include "mode_state_machine.h"
#include "picker_query.h"
#include "process_launch.h"
#include "terminal.h"
#include "text_utils.h"
#include "widgets/command_popup.h"
#include "widgets/status_bar.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>
#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace editor::statemachine
{
namespace
{
std::vector<std::string> words(const std::string& input)
{
    std::vector<std::string> result;
    std::string word;
    char quote = 0;
    bool escaped = false, active = false;
    for(char c : input)
    {
        if(escaped)
        {
            word += c;
            escaped = false;
            active = true;
        }
        else if(c == '\\' && quote != '\'')
        {
            escaped = true;
            active = true;
        }
        else if(quote)
        {
            if(c == quote)
                quote = 0;
            else
                word += c;
        }
        else if(c == '\'' || c == '"')
        {
            quote = c;
            active = true;
        }
        else if(std::isspace(static_cast<unsigned char>(c)))
        {
            if(active)
            {
                result.push_back(word);
                word.clear();
                active = false;
            }
        }
        else
        {
            word += c;
            active = true;
        }
    }
    if(quote || escaped)
        throw std::runtime_error("Unterminated quote or escape");
    if(active)
        result.push_back(word);
    return result;
}

std::string safe(std::string text)
{
    for(char& c : text)
        if(static_cast<unsigned char>(c) < 32 || c == 127)
            c = ' ';
    return text;
}

std::string fit(const std::string& text, int width)
{
    std::string result;
    int cells = 0;
    for(int i = 0; i < static_cast<int>(text.size());)
    {
        int end = text_utils::nextUtf8CharStart(text, i);
        auto glyph = std::string_view(text).substr(i, end - i);
        int count = text_utils::utf8DisplayWidth(glyph);
        if(cells + count > width)
            break;
        result += glyph;
        cells += count;
        i = end;
    }
    result.append(std::max(0, width - cells), ' ');
    return result;
}

struct DebuggerRect
{
    int x = 1, y = 3, width = 0, height = 0;
};

std::array<DebuggerRect, 4> paneRects(const Editor& editor, bool stopped,
                                      int pane)
{
    std::array<DebuggerRect, 4> rects{};
    const int width = std::max(1, editor.screenCols);
    const int body = std::max(0, editor.screenRows - 2);
    if(width < 50 || body < 8)
    {
        rects[pane] = {1, 3, width, body};
        return rects;
    }
    const int left = stopped ? width * 2 / 3 : width;
    const int top = body * 2 / 3;
    rects[0] = {1, 3, left, top};
    rects[1] = {1, 3 + top, left, body - top};
    if(stopped)
    {
        rects[2] = {left + 1, 3, width - left, top};
        rects[3] = {left + 1, 3 + top, width - left, body - top};
    }
    return rects;
}

std::filesystem::path debuggerSourcePath(const std::string& path)
{
    std::error_code error;
    const auto result = std::filesystem::weakly_canonical(
        EditorPathUtilities::resolveEditorPath(path), error);
    return error ? std::filesystem::path(path).lexically_normal() : result;
}

struct DebuggerCommandEntry
{
    const char* command;
    const char* description;
    const char* alias;
};

const std::vector<DebuggerCommandEntry>& debuggerCommands()
{
    static const std::vector<DebuggerCommandEntry> commands{
        {"b ", "Set breakpoint at file:line or function", "break"},
        {"bps", "Browse breakpoints; d deletes selection", ""},
        {"delete ", "Delete breakpoint by ID", ""},
        {"disable ", "Disable breakpoint by ID", ""},
        {"enable ", "Enable breakpoint by ID", ""},
        {"continue", "Continue execution", "c"},
        {"next", "Step over the next source line", "n"},
        {"step", "Step into a call", "s"},
        {"finish", "Step out of the current function", "f"},
        {"interrupt", "Pause execution", ""},
        {"run", "Restart the program", "r"},
        {"locals", "Print local variables", ""},
        {"p ", "Evaluate an expression", "print"},
        {"bt", "Print stack frames", ""},
        {"threads", "List threads", ""},
        {"thread ", "Select a thread by ID", ""},
        {"frame ", "Select a stack frame", ""},
        {"up", "Inspect the caller frame", ""},
        {"down", "Inspect a younger frame", ""},
        {"help", "Show native debugger help", ""},
        {"quit", "Close the debugger session", "q"}};
    return commands;
}

void moveMenuCursor(int& cursor, int& offset, int size, int delta,
                    int screenRows)
{
    cursor = std::clamp(cursor + delta, 0, std::max(0, size - 1));
    const int rows = std::max(1, std::min(8, screenRows - 2));
    if(cursor < offset)
        offset = cursor;
    if(cursor >= offset + rows)
        offset = cursor - rows + 1;
}

// The shared popup uses absolute row positions. Fold its rows into the frame
// cache so opening/closing a menu restores only its covered rows.
void overlayPopupRows(std::vector<std::string>& rows, const std::string& popup,
                      const Theme& theme)
{
    size_t start = 0;
    int active = -1;
    for(size_t position = popup.find("\x1b["); position != std::string::npos;
        position = popup.find("\x1b[", position + 2))
    {
        int row = 0, column = 0, length = 0;
        if(std::sscanf(popup.c_str() + position, "\x1b[%d;%dH%n", &row, &column,
                       &length) != 2 ||
           length == 0)
            continue;
        if(active >= 0 && active < static_cast<int>(rows.size()))
            rows[active] += theme.reset() + theme.baseFg() +
                            popup.substr(start, position - start);
        active = row - 1;
        start = position;
    }
    if(active >= 0 && active < static_cast<int>(rows.size()))
        rows[active] += theme.reset() + theme.baseFg() + popup.substr(start);
}

std::string quotedJson(std::string_view value)
{
    std::string result;
    json_utils::append_escaped(result, value);
    return result;
}
} // namespace

struct DebuggerTransport
{
#ifndef _WIN32
    int fd = -1;
    pid_t pid = -1;
#endif
    std::string pending, outgoing;
    bool ended = false;

    ~DebuggerTransport()
    {
#ifndef _WIN32
        if(fd >= 0)
            ::close(fd); // EOF makes the bridge detach/kill its inferior.
        if(pid > 0)
        {
            // Reap away from the UI; a broken backend must not hang mode exit.
            std::thread(
                [child = pid]
                {
                    for(int i = 0; i < 60; ++i)
                    {
                        if(waitpid(child, nullptr, WNOHANG) != 0)
                            return;
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(50));
                    }
                    kill(child, SIGTERM);
                    waitpid(child, nullptr, 0);
                })
                .detach();
        }
#endif
    }

    void start(const std::vector<std::string>& args,
               const std::string& directory)
    {
#ifdef _WIN32
        (void)args;
        (void)directory;
        throw std::runtime_error(
            "Debugger transport currently requires macOS or Linux");
#else
        std::lock_guard<std::mutex> lock(processLaunchMutex());
        std::vector<char*> argv;
        for(const auto& arg : args)
            argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        int sockets[2];
        if(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets))
            throw std::runtime_error("Cannot create debugger socket");
        for(int socket : sockets)
            fcntl(socket, F_SETFD, FD_CLOEXEC);
#ifdef __APPLE__
        int one = 1;
        setsockopt(sockets[0], SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
        pid = fork();
        if(pid == 0)
        {
            ::close(sockets[0]);
            dup2(sockets[1], STDIN_FILENO);
            dup2(sockets[1], STDOUT_FILENO);
            dup2(sockets[1], STDERR_FILENO);
            ::close(sockets[1]);
            if(chdir(directory.c_str()))
                _exit(126);
            execvp(argv[0], argv.data());
            _exit(127);
        }
        ::close(sockets[1]);
        if(pid < 0)
        {
            ::close(sockets[0]);
            throw std::runtime_error("Cannot launch debugger bridge");
        }
        fd = sockets[0];
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
#endif
    }

    void send(const std::string& command)
    {
        outgoing += quotedJson(command) + "\n";
    }

    std::vector<std::string> poll()
    {
        std::vector<std::string> messages;
#ifndef _WIN32
        if(fd < 0 || ended)
            return messages;
        if(!outgoing.empty())
        {
#ifdef MSG_NOSIGNAL
            auto n = ::send(fd, outgoing.data(), outgoing.size(), MSG_NOSIGNAL);
#else
            auto n = ::send(fd, outgoing.data(), outgoing.size(), 0);
#endif
            if(n > 0)
                outgoing.erase(0, n);
            else if(n < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
                    errno != EINTR)
                ended = true;
        }
        char buffer[8192];
        for(int reads = 0; reads < 32; ++reads)
        {
            auto n = read(fd, buffer, sizeof(buffer));
            if(n <= 0)
            {
                if(n == 0)
                    ended = true;
                break;
            }
            pending.append(buffer, n);
            size_t pos;
            while((pos = pending.find('\n')) != std::string::npos)
            {
                messages.push_back(pending.substr(0, pos));
                pending.erase(0, pos + 1);
            }
            if(pending.size() > 1024 * 1024)
            {
                pending.clear();
                ended = true;
                break;
            }
        }
#endif
        return messages;
    }
};

void DebuggerMode::on_enter(ModeContext& ctx)
{
    drawnRows.clear();
    drawnFooter.clear();
    drawnWidth = 0;
    ctx.editor->needsFullRedraw = true;
    try
    {
        auto args = words(arguments);
        if(args.size() >= 2 && args[0] == "--backend")
        {
            backend = args[1];
            args.erase(args.begin(), args.begin() + 2);
        }
        if(backend != "lldb" && backend != "gdb" && backend != "mladbg")
            throw std::runtime_error("Unknown debugger backend: " + backend);
        if(args.empty())
            throw std::runtime_error(
                attach ? "Usage: dbgattach [--backend lldb|gdb|mladbg] PID"
                       : "Usage: dbgrun [--backend lldb|gdb|mladbg] BINARY "
                         "[ARGS...]");
        target = args.front();
        if(attach)
        {
            if(args.size() != 1 ||
               target.find_first_not_of("0123456789") != std::string::npos ||
               std::stoull(target) == 0 || std::stoull(target) > 2147483647)
                throw std::runtime_error(
                    "dbgattach requires a positive process ID");
        }
        else
        {
            target = EditorPathUtilities::resolveEditorPath(target).string();
            programArguments.assign(args.begin() + 1, args.end());
            if(!programArguments.empty() && programArguments[0] == "--")
                programArguments.erase(programArguments.begin());
        }
        output.push_back(attach ? "Attaching to process " + target
                                : "Launching and stopping at entry. :b "
                                  "file:line sets breakpoints; :c continues.");
    }
    catch(const std::exception& error)
    {
        state = "error";
        output.push_back(error.what());
    }
}

void DebuggerMode::on_exit(ModeContext& ctx)
{
#ifdef UVIM_ENABLE_SEARCH_TOOLS
    if(filePicker)
    {
        filePicker->on_exit(ctx);
        filePicker.reset();
    }
#else
    (void)ctx;
#endif
    transport.reset();
}

void DebuggerMode::processIdle(Editor& editor)
{
    if(runningStatusDeadline &&
       std::chrono::steady_clock::now() >= *runningStatusDeadline)
    {
        runningStatusDeadline.reset();
        editor.needsFullRedraw = true;
    }
    if(!started && state != "error")
    {
        started = true;
        transport = std::make_shared<DebuggerTransport>();
        const char* python = std::getenv("MLADBG_PYTHON");
#ifdef __APPLE__
        std::string interpreter =
            python && *python ? python : "/usr/bin/python3";
#else
        std::string interpreter = python && *python ? python : "python3";
#endif
        std::vector<std::string> args{interpreter, "-u",
                                      "-c",        debuggerBridge,
                                      backend,     attach ? "attach" : "run",
                                      target};
        args.insert(args.end(), programArguments.begin(),
                    programArguments.end());
        try
        {
            transport->start(
                args, EditorPathUtilities::currentWorkingDirectory().string());
        }
        catch(const std::exception& error)
        {
            state = "error";
            output.push_back(error.what());
            transport.reset();
        }
        editor.needsFullRedraw = true;
    }
    if(!transport)
        return;
    for(const auto& message : transport->poll())
    {
        applyMessage(message);
        editor.needsFullRedraw = true;
    }
    if(transport->ended && state != "error" && state != "disconnected")
    {
        state = "disconnected";
        output.push_back("Debugger disconnected");
        editor.needsFullRedraw = true;
    }
}

void DebuggerMode::applyMessage(const std::string& message)
{
    json_utils::Document document;
    auto* doc = &document;
    if(!json_utils::parse(document, message) || !doc->IsObject())
    {
        if(!message.empty())
            output.push_back(safe(message));
        return;
    }
    const auto previousState = state;
    if(json_utils::has(*doc, "state"))
        state = json_utils::get_string(*doc, "state");
    if(state == "stopped")
        hasStoppedSnapshot = true;
    if(state != "running")
        runningStatusDeadline.reset();
    else if(previousState != "running" && hasStoppedSnapshot)
        runningStatusDeadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    const bool retainSnapshot = state == "running" && hasStoppedSnapshot;
    auto strings =
        [&](const char* key, std::vector<std::string>& destination, bool append)
    {
        auto list = json_utils::find(*doc, key);
        if(!list || !list->IsArray())
            return;
        if(!append)
            destination.clear();
        for(const auto& item : list->GetArray())
            if(item.IsString())
                destination.push_back(safe(item.GetString()));
    };
    if(!retainSnapshot)
        strings("variables", variables, false);
    strings("output", output, true);
    if(output.size() > 2000)
        output.erase(output.begin(), output.end() - 2000);
    if(auto list = json_utils::find(*doc, "threads");
       !retainSnapshot && list && list->IsArray())
    {
        threads.clear();
        threadIds.clear();
        for(const auto& item : list->GetArray())
        {
            threads.push_back(safe(json_utils::get_string(item, "label")));
            threadIds.push_back(json_utils::get_string(item, "id"));
        }
    }
    if(!retainSnapshot && json_utils::has(*doc, "path"))
    {
        const auto path = json_utils::get_string(*doc, "path");
        const int line = json_utils::get_int(*doc, "line");
        const bool backendMoved =
            (executionPath.empty() ? sourcePath : executionPath) != path ||
            executionLine != line;
        executionPath = path;
        executionLine = line;
        if(state == "stopped" && followExecutionOnStop)
        {
            sourcePinned = false;
            followExecutionOnStop = false;
        }
        if(!sourcePinned)
        {
            const bool sourceChanged = sourcePath != path;
            if(sourceChanged)
            {
                sourcePath = path;
                source.clear();
                highlight = {};
                std::ifstream file(path);
                std::string text;
                while(source.size() < 200000 && std::getline(file, text))
                    source.push_back(safe(text));
            }
            if((sourceChanged || backendMoved || previousState == "running") &&
               line > 0)
            {
                cursor[0] = line - 1;
                if(sourceChanged)
                    offset[0] = std::max(0, cursor[0] - 5);
            }
        }
    }
    if(!retainSnapshot && json_utils::has(*doc, "frame"))
        frameIndex = json_utils::get_int(*doc, "frame");
    if(!retainSnapshot && json_utils::has(*doc, "function"))
        frameFunction = safe(json_utils::get_string(*doc, "function"));
    if(!retainSnapshot && json_utils::has(*doc, "stopFunction"))
        stopFunction = safe(json_utils::get_string(*doc, "stopFunction"));
    if(auto list = json_utils::find(*doc, "breakpoints");
       list && list->IsArray())
    {
        const auto selected =
            breakpointCursor >= 0 &&
                    breakpointCursor < static_cast<int>(breakpoints.size())
                ? breakpoints[breakpointCursor].id
                : std::string{};
        breakpoints.clear();
        for(const auto& item : list->GetArray())
        {
            DebuggerBreakpoint bp;
            bp.id = json_utils::get_string(item, "id");
            bp.description = safe(json_utils::get_string(item, "description"));
            bp.enabled = json_utils::get_bool(item, "enabled", true);
            bp.pending = json_utils::get_bool(item, "pending");
            if(auto locations = json_utils::find(item, "locations");
               locations && locations->IsArray())
                for(const auto& location : locations->GetArray())
                    bp.locations.push_back(
                        {json_utils::get_string(location, "path"),
                         json_utils::get_int(location, "line"),
                         json_utils::get_bool(location, "enabled", true)});
            if(!bp.id.empty())
                breakpoints.push_back(std::move(bp));
        }
        for(int i = 0; i < static_cast<int>(breakpoints.size()); ++i)
            if(breakpoints[i].id == selected)
                breakpointCursor = i;
        breakpointCursor =
            std::clamp(breakpointCursor, 0,
                       std::max(0, static_cast<int>(breakpoints.size()) - 1));
    }
    updateBreakpointLines();
    if(!hasStoppedSnapshot && state != "stopped" && pane > 1)
        pane = 0;
}

void DebuggerMode::updateBreakpointLines()
{
    breakpointLines.clear();
    if(sourcePath.empty())
        return;
    const auto source = debuggerSourcePath(sourcePath);
    for(const auto& bp : breakpoints)
        if(bp.enabled)
            for(const auto& location : bp.locations)
                if(location.enabled && location.line > 0 &&
                   !location.path.empty() &&
                   debuggerSourcePath(location.path) == source)
                    breakpointLines.insert(location.line);
}

bool DebuggerMode::selectSourceFile(const std::string& path, int line)
{
    const auto resolved = EditorPathUtilities::resolveEditorPath(path);
    std::ifstream file(resolved, std::ios::binary);
    if(!file)
    {
        output.push_back("Cannot read debugger source: " + safe(path));
        return false;
    }
    std::vector<std::string> contents;
    std::string text;
    size_t bytes = 0;
    while(std::getline(file, text))
    {
        bytes += text.size();
        if(text.find('\0') != std::string::npos || bytes > 20 * 1024 * 1024 ||
           contents.size() >= 200000)
        {
            output.push_back("Choose a text source file smaller than 20 MB and "
                             "200000 lines");
            return false;
        }
        contents.push_back(safe(text));
    }
    if(file.bad())
    {
        output.push_back("Cannot read debugger source: " + safe(path));
        return false;
    }
    if(contents.empty())
        contents.emplace_back();
    if(executionPath.empty())
        executionPath = sourcePath;
    sourcePath = resolved.string();
    source = std::move(contents);
    highlight = {};
    sourcePinned = true;
    pane = 0;
    cursor[0] = std::clamp(line - 1, 0, static_cast<int>(source.size()) - 1);
    offset[0] = std::max(0, cursor[0] - 5);
    updateBreakpointLines();
    return true;
}

void DebuggerMode::toggleSelectedBreakpoint()
{
    if(pane != 0 || sourcePath.empty() || cursor[0] < 0 ||
       cursor[0] >= static_cast<int>(source.size()))
    {
        output.push_back("Breakpoint: select a source row first");
        return;
    }
    const auto path = debuggerSourcePath(sourcePath);
    const int line = cursor[0] + 1;
    std::vector<std::string> active;
    std::string disabled;
    for(const auto& bp : breakpoints)
        for(const auto& location : bp.locations)
            if(location.line == line && !location.path.empty() &&
               debuggerSourcePath(location.path) == path)
            {
                if(bp.enabled && location.enabled)
                {
                    if(std::find(active.begin(), active.end(), bp.id) ==
                       active.end())
                        active.push_back(bp.id);
                }
                else if(!bp.enabled)
                    disabled = bp.id;
            }
    if(!active.empty())
        for(const auto& id : active)
            execute("delete " + id);
    else if(!disabled.empty())
        execute("enable " + disabled);
    else
        execute("b " + sourcePath + ":" + std::to_string(line));
}

void DebuggerMode::updatePromptMenu()
{
    promptCommands.clear();
    promptMenuEntries.clear();
    promptMenuCursor = promptMenuOffset = 0;
    if(!promptActive || prompt.find_first_of(" \t") != std::string::npos)
        return;
    std::vector<std::pair<int, int>> ranked;
    std::vector<int> positions;
    const auto& commands = debuggerCommands();
    for(int i = 0; i < static_cast<int>(commands.size()); ++i)
    {
        const auto& entry = commands[i];
        int score = editor::helper::fuzzyScoreWithPositions(
            prompt, entry.command, positions);
        if(!prompt.empty() && prompt == entry.alias)
            score = 100000;
        if(score >= 0)
            ranked.emplace_back(i, score);
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const auto& a, const auto& b)
                     { return a.second > b.second; });
    for(const auto& [index, score] : ranked)
    {
        (void)score;
        const auto& entry = commands[index];
        promptCommands.emplace_back(entry.command);
        promptMenuEntries.push_back(
            std::string(entry.command) +
            std::string(
                std::max(2, 14 - static_cast<int>(
                                     std::string_view(entry.command).size())),
                ' ') +
            entry.description);
    }
}

void DebuggerMode::execute(const std::string& command)
{
    if(command.empty())
        return;
    if(command == "bps")
    {
        breakpointListOpen = true;
        if(transport && !transport->ended)
            transport->send("bps");
        return;
    }
    if(!transport || transport->ended)
    {
        output.push_back("Debugger is not connected");
        return;
    }
    const auto verb = command.substr(0, command.find(' '));
    if(verb == "n" || verb == "next" || verb == "s" || verb == "step" ||
       verb == "f" || verb == "finish" || verb == "c" || verb == "continue" ||
       verb == "r" || verb == "run" || verb == "interrupt" ||
       verb == "thread" || verb == "frame" || verb == "up" || verb == "down")
        followExecutionOnStop = true;
    transport->send(command);
}

void DebuggerMode::complete(ModeContext& ctx)
{
    if(completions.empty())
    {
        completionOriginal = prompt;
        const auto space = prompt.find(' ');
        if(space == std::string::npos || (prompt.substr(0, space) != "b" &&
                                          prompt.substr(0, space) != "break"))
            return;
        auto path = prompt.substr(space + 1);
        std::string suffix;
        auto colon = path.rfind(':');
        if(colon != std::string::npos &&
           path.substr(colon + 1).find_first_not_of("0123456789") ==
               std::string::npos)
        {
            suffix = path.substr(colon);
            path.resize(colon);
        }
        if(!path.empty() && path.front() == '"')
            path.erase(0, 1);
        if(!path.empty() && path.back() == '"')
            path.pop_back();
        for(const auto& value : ctx.getPathCompletionsRecursive(path))
            completions.push_back(prompt.substr(0, space + 1) + value + suffix);
        completionIndex = -1;
    }
    if(!completions.empty())
    {
        completionIndex = (completionIndex + 1) % completions.size();
        prompt = completions[completionIndex];
    }
}

void DebuggerMode::clamp(const Editor& editor)
{
    const auto rects =
        paneRects(editor, hasStoppedSnapshot || state == "stopped", pane);
    const std::array<int, 4> sizes{
        static_cast<int>(source.size()), static_cast<int>(output.size()),
        static_cast<int>(variables.size()), static_cast<int>(threads.size())};
    for(int i = 0; i < 4; ++i)
    {
        const int rows = std::max(1, rects[i].height - 2);
        cursor[i] = std::clamp(cursor[i], 0, std::max(0, sizes[i] - 1));
        if(rects[i].height <= 0 || rects[i].width <= 0)
            continue;
        if(cursor[i] < offset[i])
            offset[i] = cursor[i];
        if(cursor[i] >= offset[i] + rows)
            offset[i] = cursor[i] - rows + 1;
    }
}

std::optional<ModeState> DebuggerMode::handle(ModeContext& ctx,
                                              const ModeKeyEvent& event)
{
    const int key = event.key;
    ctx.editor->needsFullRedraw = true;
    const int paneCount = hasStoppedSnapshot || state == "stopped" ? 4 : 2;
#ifdef UVIM_ENABLE_SEARCH_TOOLS
    if(filePicker)
    {
        bool close = key == keyCode(control::ControlKey::ESC);
        if(key == keyCode(control::ControlKey::ENTER))
        {
            const int index = filePicker->cursor;
            if(index >= 0 &&
               index < static_cast<int>(filePicker->matches.size()))
            {
                const auto query = parsePickerQuery(filePicker->query);
                close = selectSourceFile(filePicker->matches[index].file.path,
                                         query.hasLine ? query.line : 1);
                if(!close && !output.empty())
                    ctx.setStatusMessage(output.back());
            }
        }
        else if(!close)
            filePicker->handle(ctx, event);
        if(close)
        {
            filePicker->on_exit(ctx);
            filePicker.reset();
            drawnRows.clear();
            drawnFooter.clear();
        }
        return {};
    }
#endif
    if(promptActive)
    {
        if(key == 27)
        {
            promptActive = false;
            prompt.clear();
            completions.clear();
            updatePromptMenu();
        }
        else if((key == keyCode(control::ControlKey::CTRL_J) ||
                 key == keyCode(navigation::NavigationKey::ARROW_DOWN)) &&
                !promptCommands.empty())
            moveMenuCursor(promptMenuCursor, promptMenuOffset,
                           promptCommands.size(), 1, ctx.editor->screenRows);
        else if((key == keyCode(control::ControlKey::CTRL_K) ||
                 key == keyCode(navigation::NavigationKey::ARROW_UP)) &&
                !promptCommands.empty())
            moveMenuCursor(promptMenuCursor, promptMenuOffset,
                           promptCommands.size(), -1, ctx.editor->screenRows);
        else if(key == keyCode(control::ControlKey::ENTER))
        {
            bool exact = false;
            for(const auto& entry : debuggerCommands())
                if(prompt == entry.command ||
                   (!std::string_view(entry.alias).empty() &&
                    prompt == entry.alias &&
                    !std::string_view(entry.command).ends_with(' ')))
                    exact = true;
            if(!promptCommands.empty() && !exact)
            {
                prompt = promptCommands[promptMenuCursor];
                if(prompt.ends_with(' '))
                {
                    updatePromptMenu();
                    return {};
                }
            }
            if(prompt == "q" || prompt == "quit")
                return defaultExitMode(ctx.editor);
            execute(prompt);
            promptActive = false;
            prompt.clear();
            completions.clear();
            updatePromptMenu();
        }
        else if(key == '\t')
        {
            if(!promptCommands.empty())
            {
                prompt = promptCommands[promptMenuCursor];
                updatePromptMenu();
            }
            else
                complete(ctx);
        }
        else
        {
            completions.clear();
            completionIndex = -1;
            if(key == 127 || key == 8)
            {
                if(!prompt.empty())
                    prompt.pop_back();
            }
            else if(key == 21)
                prompt.clear();
            else if(key >= 32 && key < 127)
                prompt += static_cast<char>(key);
            updatePromptMenu();
        }
        return {};
    }
    if(breakpointListOpen)
    {
        if(key == 27 || key == 'q')
            breakpointListOpen = false;
        else if(key == 'j' || key == keyCode(control::ControlKey::CTRL_J) ||
                key == keyCode(navigation::NavigationKey::ARROW_DOWN))
            moveMenuCursor(breakpointCursor, breakpointOffset,
                           breakpoints.size(), 1, ctx.editor->screenRows);
        else if(key == 'k' || key == keyCode(control::ControlKey::CTRL_K) ||
                key == keyCode(navigation::NavigationKey::ARROW_UP))
            moveMenuCursor(breakpointCursor, breakpointOffset,
                           breakpoints.size(), -1, ctx.editor->screenRows);
        else if(key == 'd' && !breakpoints.empty())
            execute("delete " + breakpoints[breakpointCursor].id);
        else if(key == ':')
        {
            breakpointListOpen = false;
            promptActive = true;
            prompt.clear();
            updatePromptMenu();
        }
        else if(key == 3)
            execute("interrupt");
        return {};
    }
    if(key == keyCode(control::ControlKey::CTRL_P))
    {
#ifdef UVIM_ENABLE_SEARCH_TOOLS
        filePicker = std::make_shared<FuzzyFindMode>();
        filePicker->singleSelection = true;
        filePicker->pickerTitle = "Debugger Source";
        filePicker->on_enter(ctx);
#else
        output.push_back(
            "Debugger file picker requires UVIM_ENABLE_SEARCH_TOOLS");
#endif
        return {};
    }
    if(key == 'q' || key == 27)
        return defaultExitMode(ctx.editor);
    if(key == ':')
    {
        promptActive = true;
        prompt.clear();
        updatePromptMenu();
    }
    else if(key == keyCode(control::ControlKey::TAB))
        pane = (pane + 1) % paneCount;
    else if(key == keyCode(control::ControlKey::SHIFT_TAB))
        pane = (pane + paneCount - 1) % paneCount;
    else if(key == 'v' && paneCount == 4)
        pane = 2;
    else if(key == 't' && paneCount == 4)
        pane = 3;
    else if(key == keyCode(control::ControlKey::SHIFT_CTRL_H))
        pane = pane >= 2 ? pane - 2 : pane;
    else if(key == keyCode(control::ControlKey::SHIFT_CTRL_L) && paneCount == 4)
        pane = pane < 2 ? pane + 2 : pane;
    else if(key == keyCode(control::ControlKey::SHIFT_CTRL_J))
        pane = pane == 2 || pane == 3 ? 3 : 1;
    else if(key == keyCode(control::ControlKey::SHIFT_CTRL_K))
        pane = pane == 2 || pane == 3 ? 2 : 0;
    else if(key == 'j' || key == keyCode(navigation::NavigationKey::ARROW_DOWN))
        ++cursor[pane];
    else if(key == 'k' || key == keyCode(navigation::NavigationKey::ARROW_UP))
        --cursor[pane];
    else if(key == keyCode(control::ControlKey::ENTER) && state == "stopped" &&
            pane == 3 && cursor[3] < static_cast<int>(threadIds.size()))
        execute("thread " + threadIds[cursor[3]]);
    else if(key == 'b')
        toggleSelectedBreakpoint();
    else if(key == 'r')
        execute("run");
    else if(key == 'c')
        execute("continue");
    else if(key == 'n')
        execute("next");
    else if(key == 's')
        execute("step");
    else if(key == 'f')
        execute("finish");
    else if(key == 3)
        execute("interrupt");
    clamp(*ctx.editor);
    return {};
}

void DebuggerMode::draw(Editor& editor)
{
#ifdef UVIM_ENABLE_SEARCH_TOOLS
    if(filePicker)
    {
        Terminal::beginCapture();
        drawnRows.clear();
        drawnFooter.clear();
        drawContent(editor);
        filePicker->draw(editor, false);
        Terminal::endCapture();
        return;
    }
#endif
    drawContent(editor);
}

void DebuggerMode::drawContent(Editor& editor)
{
    const bool displayRunning =
        state == "running" &&
        (!runningStatusDeadline ||
         std::chrono::steady_clock::now() >= *runningStatusDeadline);
    const auto displayState =
        state == "running" && !displayRunning ? std::string("stopped") : state;
    const int width = std::max(1, editor.screenCols);
    const auto rects =
        paneRects(editor, hasStoppedSnapshot || state == "stopped", pane);
    clamp(editor);
    const int sourceRows = std::max(0, rects[0].height - 2);
    editor.highlightPreviewLines(
        highlight, sourcePath, source,
        std::min(source.size(), static_cast<size_t>(offset[0] + sourceRows)));
    std::vector<std::string> rows(std::max(2, editor.screenRows));
    std::string out;
    int activeRow = -1;
    auto saveRow = [&]()
    {
        if(activeRow >= 0 && activeRow < static_cast<int>(rows.size()))
            rows[activeRow] += out;
        out.clear();
    };
    auto atPosition = [&](int row, int column)
    {
        saveRow();
        activeRow = row - 1;
        out += Terminal::cursorPos(row, column);
    };
    const std::array<std::string, 4> names{"Source", "Output", "Variables",
                                           "Threads"};
    auto header = [&](int row, const std::string& text)
    {
        atPosition(row, 1);
        out += editor.theme.reset() + editor.theme.uiAccent() +
               fit(safe(text), width);
    };
    header(1, " Debugger [" + backend + "] " + displayState +
                  "  Focus: " + names[pane] + "  " + target);
    header(2, " j/k rows   b breakpoint   Ctrl-P file   Tab panes   v "
              "variables   t threads   " +
                  std::string(Terminal::shiftChordLabel()) +
                  "-H/J/K/L pane   : command   q quit");
    const std::array<const std::vector<std::string>*, 4> lists{
        &source, &output, &variables, &threads};
    for(int which = 0; which < 4; ++which)
    {
        const auto& rect = rects[which];
        if(rect.width <= 0 || rect.height <= 0)
            continue;
        const int inner = std::max(0, rect.width - 2);
        const auto border =
            editor.theme.reset() +
            (pane == which ? editor.theme.uiAccent() : editor.theme.uiGutter());
        auto at = [&](int row) { atPosition(rect.y + row, rect.x); };
        std::string title =
            std::string(pane == which ? "> " : " ") + names[which];
        if(which == 0 && sourcePinned)
            title += " (browse)";
        else if(displayRunning && hasStoppedSnapshot && which != 1)
            title += " (last stop)";
        if(which == 0)
        {
            if(!sourcePinned && !frameFunction.empty())
                title +=
                    " #" + std::to_string(frameIndex) + " " + frameFunction;
            if(!sourcePath.empty())
                title += " " + sourcePath;
        }
        if(which == 3 && !displayRunning)
            title += " (Enter selects)";
        at(0);
        out += border;
        if(rect.width >= 2)
        {
            ascii::append(out, ascii::BOX_ROUNDED_TOP_LEFT);
            const int titleWidth =
                std::min(inner, text_utils::utf8DisplayWidth(title) + 1);
            out += fit(title, titleWidth);
            for(int column = titleWidth; column < inner; ++column)
                ascii::append(out, ascii::BOX_LIGHT_HORIZONTAL);
            ascii::append(out, ascii::BOX_ROUNDED_TOP_RIGHT);
        }
        else
            out += " ";
        for(int row = 0; row < rect.height - 2; ++row)
        {
            at(row + 1);
            out += border;
            ascii::append(out, ascii::BOX_LIGHT_VERTICAL);
            const int index = offset[which] + row;
            const auto& lines = *lists[which];
            out += editor.theme.reset();
            const bool selected = pane == which && cursor[which] == index;
            const bool executing = displayState == "stopped" && which == 0 &&
                                   index + 1 == executionLine &&
                                   (executionPath.empty() ||
                                    debuggerSourcePath(sourcePath) ==
                                        debuggerSourcePath(executionPath));
            if(selected || executing)
                out += editor.theme.selection();
            if(index >= static_cast<int>(lines.size()))
            {
                std::string empty;
                if(row == 0 && lines.empty())
                {
                    if(which == 0)
                        empty = " No readable source. Try :up or :b file:line, "
                                "then :c.";
                    else if(which == 2)
                        empty = " No locals in this frame. Try :up or select a "
                                "thread.";
                    else if(which == 3)
                        empty = " No stopped threads.";
                    else
                        empty = " Debugger commands and process output appear "
                                "here.";
                }
                out += editor.theme.uiDim() + fit(empty, inner);
            }
            else
            {
                const auto prefix = which == 0
                                        ? std::string(executing  ? ">"
                                                      : selected ? "+"
                                                                 : " ") +
                                              std::to_string(index + 1) + " "
                                        : std::string(selected ? "> " : "  ");
                const int prefixWidth =
                    std::min(inner, text_utils::utf8DisplayWidth(prefix));
                const bool breakpoint =
                    which == 0 && breakpointLines.contains(index + 1);
                if(breakpoint)
                    out +=
                        color::rgbBg(170, 35, 50) + color::rgbFg(255, 255, 255);
                else
                    out += editor.theme.baseFg();
                out += fit(prefix, prefixWidth);
                if(breakpoint)
                {
                    out += editor.theme.reset();
                    if(selected || executing)
                        out += editor.theme.selection();
                    out += editor.theme.baseFg();
                }
                const int available = inner - prefixWidth;
                const auto text = fit(lines[index], available);
                if(which == 0 &&
                   index < static_cast<int>(highlight.tokens.size()))
                {
                    std::vector<TokenType> colors(text.size(), TOKEN_NORMAL);
                    for(const auto& token : highlight.tokens[index])
                        for(int c = std::max(0, token.start);
                            c < std::min(static_cast<int>(text.size()),
                                         token.start + token.length);
                            ++c)
                            colors[c] = token.type;
                    TokenType previous = TOKEN_NORMAL;
                    for(size_t c = 0; c < text.size(); ++c)
                    {
                        if(colors[c] != previous)
                        {
                            previous = colors[c];
                            out += editor.theme.syntax(previous);
                        }
                        out += text[c];
                    }
                }
                else
                    out += editor.theme.baseFg() + text;
            }
            out += border;
            ascii::append(out, ascii::BOX_LIGHT_VERTICAL);
        }
        if(rect.height >= 2)
        {
            at(rect.height - 1);
            out += border;
            ascii::append(out, ascii::BOX_ROUNDED_BOTTOM_LEFT);
            for(int column = 0; column < inner; ++column)
                ascii::append(out, ascii::BOX_LIGHT_HORIZONTAL);
            ascii::append(out, ascii::BOX_ROUNDED_BOTTOM_RIGHT);
        }
    }
    saveRow();
    activeRow = -1;
    std::vector<std::string> menuEntries;
    int menuCursor = 0, menuOffset = 0;
    std::string menuTitle;
    if(breakpointListOpen)
    {
        moveMenuCursor(breakpointCursor, breakpointOffset, breakpoints.size(),
                       0, editor.screenRows);
        for(const auto& bp : breakpoints)
        {
            std::string location = bp.description;
            if(!bp.locations.empty())
            {
                const auto& first = bp.locations.front();
                location =
                    std::filesystem::path(first.path).filename().string() +
                    ":" + std::to_string(first.line);
                if(bp.locations.size() > 1)
                    location += " (+" +
                                std::to_string(bp.locations.size() - 1) +
                                " locations)";
                location += "  " + first.path;
            }
            menuEntries.push_back(
                "#" + bp.id + (bp.enabled ? " enabled " : " disabled ") +
                (bp.pending ? "[pending] " : "") + safe(location));
        }
        menuCursor = breakpointCursor;
        menuOffset = breakpointOffset;
        menuTitle = "Breakpoints " + std::to_string(breakpoints.size()) +
                    "  Ctrl-J/K select  d delete  Esc close";
    }
    else if(promptActive && !promptMenuEntries.empty())
    {
        menuEntries = promptMenuEntries;
        menuCursor = promptMenuCursor;
        menuOffset = promptMenuOffset;
        menuTitle = "Debugger commands [" + backend +
                    "]  Ctrl-J/K select  Tab/Enter choose";
    }
    if(breakpointListOpen || !menuEntries.empty())
    {
        std::vector<int> filtered;
        for(int i = 0; i < static_cast<int>(menuEntries.size()); ++i)
            filtered.push_back(i);
        std::string popup;
        widgets::drawCommandPopup(popup,
                                  {.frame = {.theme = editor.theme,
                                             .screenRows = editor.screenRows,
                                             .screenCols = width},
                                   .entries = menuEntries,
                                   .filtered = filtered,
                                   .offset = menuOffset,
                                   .cursor = menuCursor,
                                   .title = menuTitle,
                                   .emptyMessage = "No breakpoints"});
        overlayPopupRows(rows, popup, editor.theme);
    }
    widgets::ViewStatusBarView status{.theme = editor.theme};
    status.modeLabel = name();
    status.context =
        sourcePinned
            ? "source " + std::filesystem::path(sourcePath).filename().string()
        : frameFunction.empty()
            ? backend
            : "frame #" + std::to_string(frameIndex) + " " + frameFunction;
    status.state = displayState;
    status.flags.push_back(breakpointListOpen ? "Breakpoints" : names[pane]);
    status.flags.push_back(attach ? "q detaches" : "q ends session");
    editor.appendViewStatusBar(out, std::move(status));
    out += Terminal::cursorPos(editor.screenRows + 2, 1);
    out += editor.theme.reset();
    out += Terminal::ESC_CLEAR_LINE;
    if(promptActive)
    {
        out += fit(safe(":" + prompt), width);
        out += Terminal::cursorPos(
            editor.screenRows + 2,
            std::min(width, static_cast<int>(prompt.size()) + 2));
        out += Terminal::ESC_SHOW_CURSOR;
    }
    else if(breakpointListOpen)
        out +=
            fit(" Breakpoints: Ctrl-J/K selects  d deletes  Esc closes", width);
    else if(sourcePinned)
        out += fit(" Browsing source: j/k row  b toggle breakpoint  Ctrl-P "
                   "file  n/step or thread/frame returns to execution",
                   width);
    else if(displayRunning && hasStoppedSnapshot)
        out += fit(" Running; source, variables and threads show the last "
                   "stop. Ctrl-C interrupts.",
                   width);
    else if(displayState == "stopped")
    {
        std::string hint =
            " n next  s into  f out  c continue  :b file:line breakpoint";
        if(frameIndex > 0 && !stopFunction.empty())
            hint = " n next (finish pending call)  c continue  :frame 0 actual "
                   "stop | Caller #" +
                   std::to_string(frameIndex) + "; stopped in " + stopFunction;
        out += fit(safe(hint), width);
    }
    const bool repaint = drawnWidth != width || drawnRows.size() != rows.size();
    std::string changes;
    for(size_t row = 0; row < rows.size(); ++row)
        if(repaint || drawnRows[row] != rows[row])
            changes += rows[row];
    const bool footerChanged = repaint || drawnFooter != out;
    if(footerChanged || !changes.empty())
    {
        changes = std::string(Terminal::ESC_HIDE_CURSOR) + changes;
        // A visible prompt cursor must be restored after content updates.
        if(footerChanged || promptActive)
            changes += out;
        Terminal::beginCapture();
        Terminal::write(changes);
        Terminal::endCapture();
    }
    drawnRows = std::move(rows);
    drawnFooter = std::move(out);
    drawnWidth = width;
}
} // namespace editor::statemachine
