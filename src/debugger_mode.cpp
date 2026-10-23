#include "debugger_mode.h"
#include "debugger_bridge.h"
#include "editor.h"
#include "editor_path_utilities.h"
#include "json_utils.h"
#include "mode_state_machine.h"
#include "process_launch.h"
#include "terminal.h"
#include "text_utils.h"
#include "widgets/status_bar.h"
#include <algorithm>
#include <cctype>
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

void DebuggerMode::on_exit(ModeContext&)
{
    transport.reset();
}

void DebuggerMode::processIdle(Editor& editor)
{
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
    if(json_utils::has(*doc, "state"))
        state = json_utils::get_string(*doc, "state");
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
    strings("variables", variables, false);
    strings("output", output, true);
    if(output.size() > 2000)
        output.erase(output.begin(), output.end() - 2000);
    if(auto list = json_utils::find(*doc, "threads"); list && list->IsArray())
    {
        threads.clear();
        threadIds.clear();
        for(const auto& item : list->GetArray())
        {
            threads.push_back(safe(json_utils::get_string(item, "label")));
            threadIds.push_back(json_utils::get_string(item, "id"));
        }
    }
    if(json_utils::has(*doc, "path"))
    {
        auto path = json_utils::get_string(*doc, "path");
        if(sourcePath != path)
        {
            sourcePath = path;
            source.clear();
            highlight = {};
            std::ifstream file(path);
            std::string line;
            while(source.size() < 200000 && std::getline(file, line))
                source.push_back(safe(line));
        }
        executionLine = json_utils::get_int(*doc, "line");
        if(executionLine > 0)
        {
            cursor[0] = executionLine - 1;
            offset[0] = std::max(0, cursor[0] - 5);
        }
    }
    if(state != "stopped" && pane > 1)
        pane = 0;
}

void DebuggerMode::execute(const std::string& command)
{
    if(command.empty())
        return;
    if(!transport || transport->ended)
    {
        output.push_back("Debugger is not connected");
        return;
    }
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

void DebuggerMode::clamp(int rows)
{
    const std::array<int, 4> sizes{
        static_cast<int>(source.size()), static_cast<int>(output.size()),
        static_cast<int>(variables.size()), static_cast<int>(threads.size())};
    for(int i = 0; i < 4; ++i)
    {
        cursor[i] = std::clamp(cursor[i], 0, std::max(0, sizes[i] - 1));
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
    if(promptActive)
    {
        if(key == 27)
        {
            promptActive = false;
            prompt.clear();
            completions.clear();
        }
        else if(key == keyCode(control::ControlKey::ENTER))
        {
            if(prompt == "q" || prompt == "quit")
                return defaultExitMode(ctx.editor);
            execute(prompt);
            promptActive = false;
            prompt.clear();
            completions.clear();
        }
        else if(key == '\t')
            complete(ctx);
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
        }
        return {};
    }
    if(key == 'q' || key == 27)
        return defaultExitMode(ctx.editor);
    if(key == ':')
    {
        promptActive = true;
        prompt.clear();
    }
    else if(key == keyCode(control::ControlKey::SHIFT_CTRL_H))
        pane = pane >= 2 ? pane - 2 : pane;
    else if(key == keyCode(control::ControlKey::SHIFT_CTRL_L) &&
            state == "stopped")
        pane = pane < 2 ? pane + 2 : pane;
    else if(key == keyCode(control::ControlKey::SHIFT_CTRL_J))
        pane = pane == 2 || pane == 3 ? 3 : 1;
    else if(key == keyCode(control::ControlKey::SHIFT_CTRL_K))
        pane = pane == 2 || pane == 3 ? 2 : 0;
    else if(key == 'j' || key == keyCode(navigation::NavigationKey::ARROW_DOWN))
        ++cursor[pane];
    else if(key == 'k' || key == keyCode(navigation::NavigationKey::ARROW_UP))
        --cursor[pane];
    else if(key == keyCode(control::ControlKey::ENTER) && pane == 3 &&
            cursor[3] < static_cast<int>(threadIds.size()))
        execute("thread " + threadIds[cursor[3]]);
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
    clamp(std::max(1, (ctx.editor->screenRows - 5) / 2));
    return {};
}

void DebuggerMode::draw(Editor& editor)
{
    const int width = std::max(1, editor.screenCols);
    const int height = std::max(1, editor.screenRows);
    const bool compact = width < 40 || height < 8;
    const bool split = state == "stopped" && !compact;
    const int left = split ? std::max(1, (width - 1) * 2 / 3) : width;
    const int right = std::max(0, width - left - 1);
    const int body = std::max(0, height - 2);
    const int top = std::max(1, body * 2 / 3);
    clamp(std::max(1, std::min(top, body - top) - 1));
    editor.highlightPreviewLines(
        highlight, sourcePath, source,
        std::min(source.size(),
                 static_cast<size_t>(offset[0] + (compact ? body : top))));
    std::string out = Terminal::ESC_HIDE_CURSOR;
    out += Terminal::ESC_CURSOR_HOME;
    auto row = [&](const std::string& value)
    {
        out += editor.theme.reset() + editor.theme.uiAccent() +
               fit(safe(value), width) + editor.theme.reset() + "\r\n";
    };
    row(" Debugger [" + backend + "] " + state + "  " + target);
    row(" j/k: rows | " + std::string(Terminal::shiftChordLabel()) +
        "-H/J/K/L: pane | :b file:line | r/c/n/s/f | Ctrl-C | q");
    const std::array<const std::vector<std::string>*, 4> lists{
        &source, &output, &variables, &threads};
    auto cell = [&](int which, int line, int columns)
    {
        if(line == 0)
        {
            const std::array<std::string, 4> titles{"Source " + sourcePath,
                                                    "Output", "Variables",
                                                    "Threads (Enter selects)"};
            out += editor.theme.uiAccent() +
                   fit(std::string(pane == which ? "> " : "  ") +
                           safe(titles[which]),
                       columns);
            return;
        }
        int index = offset[which] + line - 1;
        const auto& lines = *lists[which];
        bool selected = pane == which && cursor[which] == index;
        out += editor.theme.reset();
        if(selected ||
           (state == "stopped" && which == 0 && index + 1 == executionLine))
            out += editor.theme.selection();
        if(index >= static_cast<int>(lines.size()))
        {
            const auto empty =
                which == 0 && source.empty() && line == 1
                    ? " No source location; set :b file:line and :c"
                    : "";
            out += editor.theme.uiDim() + fit(empty, columns);
            return;
        }
        std::string prefix =
            which == 0 ? std::string(index + 1 == executionLine ? ">" : " ") +
                             std::to_string(index + 1) + " "
                       : " ";
        prefix =
            fit(prefix, std::min(columns, static_cast<int>(prefix.size())));
        out += prefix;
        int available = std::max(0, columns - static_cast<int>(prefix.size()));
        auto text = fit(lines[index], available);
        if(which == 0 && index < static_cast<int>(highlight.tokens.size()))
        {
            std::vector<TokenType> colors(text.size(), TOKEN_NORMAL);
            for(const auto& token : highlight.tokens[index])
                for(int c = std::max(0, token.start);
                    c < std::min(static_cast<int>(text.size()),
                                 token.start + token.length);
                    ++c)
                    colors[c] = token.type;
            TokenType previous = TOKEN_NORMAL;
            out += editor.theme.baseFg();
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
    };
    for(int y = 0; y < body; ++y)
    {
        const bool upper = y < top;
        cell(compact ? pane
             : upper ? 0
                     : 1,
             compact ? y
             : upper ? y
                     : y - top,
             left);
        if(split)
        {
            out += editor.theme.reset() + editor.theme.uiGutter() + "|";
            cell(upper ? 2 : 3, upper ? y : y - top, right);
        }
        out += editor.theme.reset();
        if(y + 1 < body)
            out += "\r\n";
    }
    widgets::ViewStatusBarView status{.theme = editor.theme};
    status.modeLabel = name();
    status.context = backend;
    status.state = state;
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
    Terminal::write(out);
    Terminal::flush();
}
} // namespace editor::statemachine
