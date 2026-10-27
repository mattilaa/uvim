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
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
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

std::string sourceTextLine(std::string text)
{
    // getline removes LF; binary streams and CRLF files on POSIX retain CR.
    // Remove only that terminator, preserving intentional trailing spaces.
    if(!text.empty() && text.back() == '\r')
        text.pop_back();
    return safe(std::move(text));
}

bool readableFrameSource(const std::string& path)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if(error || size == 0 || size > 20 * 1024 * 1024)
        return false;
    std::ifstream file(path, std::ios::binary);
    std::array<char, 4096> bytes{};
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const auto end = bytes.begin() + file.gcount();
    return end != bytes.begin() && std::find(bytes.begin(), end, '\0') == end;
}

bool assemblyRegister(std::string word)
{
    std::transform(
        word.begin(),
        word.end(),
        word.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if(word.starts_with('%'))
        word.erase(0, 1);
    if(const auto dot = word.find('.'); dot != std::string::npos)
        word.resize(dot);
    static const std::unordered_set<std::string> names{
        "rax",
        "rbx",
        "rcx",
        "rdx",
        "rsi",
        "rdi",
        "rbp",
        "rsp",
        "rip",
        "eax",
        "ebx",
        "ecx",
        "edx",
        "esi",
        "edi",
        "ebp",
        "esp",
        "eip",
        "ax",
        "bx",
        "cx",
        "dx",
        "al",
        "bl",
        "cl",
        "dl",
        "ah",
        "bh",
        "ch",
        "dh",
        "sp",
        "lr",
        "pc",
        "fp",
        "ip",
        "xzr",
        "wzr",
        "zero",
        "ra",
        "gp",
        "tp",
        "fpcr",
        "fpsr",
        "cpsr",
        "nzcv",
        "cs",
        "ds",
        "es",
        "fs",
        "gs",
        "ss"};
    if(names.contains(word))
        return true;
    const auto digit = word.find_first_of("0123456789");
    if(digit == std::string::npos)
        return false;
    const auto prefix = word.substr(0, digit);
    static const std::unordered_set<std::string> numbered{
        "x",
        "w",
        "r",
        "v",
        "q",
        "d",
        "s",
        "a",
        "t",
        "f",
        "p",
        "xmm",
        "ymm",
        "zmm",
        "mm",
        "cr",
        "dr",
        "st"};
    if(!numbered.contains(prefix))
        return false;
    auto end = digit;
    while(end < word.size() &&
          std::isdigit(static_cast<unsigned char>(word[end])))
        ++end;
    return end == word.size() ||
           (prefix == "r" && end + 1 == word.size() &&
            std::string_view("bwd").find(word[end]) != std::string_view::npos);
}

std::vector<Token> disassemblyTokens(const std::string& line)
{
    std::vector<Token> tokens;
    if(!line.starts_with("0x"))
        return tokens;
    auto add = [&](TokenType type, size_t start, size_t end)
    {
        tokens.push_back(
            {type, static_cast<int>(start), static_cast<int>(end - start)});
    };
    size_t pos = 2;
    while(pos < line.size() &&
          std::isxdigit(static_cast<unsigned char>(line[pos])))
        ++pos;
    add(TOKEN_COMMENT, 0, pos);
    while(pos < line.size() && line[pos] == ' ')
        ++pos;
    const auto mnemonic = pos;
    while(pos < line.size() && line[pos] != ' ')
        ++pos;
    add(TOKEN_KEYWORD, mnemonic, pos);
    auto comment = line.find(';', pos);
    for(const auto marker : {"# ", "//"})
        comment = std::min(comment, line.find(marker, pos));
    const size_t end = comment == std::string::npos ? line.size() : comment;
    if(comment != std::string::npos)
        add(TOKEN_COMMENT, comment, line.size());
    auto bytes = line.rfind("  [", end);
    auto bytesEnd = bytes == std::string::npos ? std::string::npos
                                               : line.find(']', bytes + 3);
    if(bytesEnd == std::string::npos || bytesEnd >= end ||
       bytesEnd < bytes + 5 ||
       !std::all_of(
           line.begin() + (bytes == std::string::npos ? 0 : bytes + 3),
           line.begin() + (bytesEnd == std::string::npos ? 0 : bytesEnd),
           [](unsigned char c)
           { return std::isxdigit(c) || c == ' ' || c == '?'; }))
        bytes = std::string::npos;
    while(pos < end)
    {
        if(bytes != std::string::npos && pos == bytes + 2)
        {
            add(TOKEN_STRING, pos, bytesEnd + 1);
            pos = bytesEnd + 1;
            continue;
        }
        const auto start = pos;
        const auto ch = static_cast<unsigned char>(line[pos]);
        if(ch == '<')
        {
            const auto close = line.find('>', pos + 1);
            pos = close != std::string::npos && close < end ? close + 1 : end;
            add(TOKEN_FUNCTION, start, pos);
        }
        else if(
            std::isdigit(ch) ||
            (std::string_view("#$+-").find(ch) != std::string_view::npos &&
             pos + 1 < end &&
             (std::isdigit(static_cast<unsigned char>(line[pos + 1])) ||
              line[pos + 1] == '-')))
        {
            if(line[pos] == '#' || line[pos] == '$')
                ++pos;
            if(pos < end && (line[pos] == '+' || line[pos] == '-'))
                ++pos;
            if(pos + 1 < end && line[pos] == '0' &&
               (line[pos + 1] == 'x' || line[pos + 1] == 'X'))
            {
                pos += 2;
                while(pos < end &&
                      std::isxdigit(static_cast<unsigned char>(line[pos])))
                    ++pos;
            }
            else
                while(pos < end &&
                      (std::isdigit(static_cast<unsigned char>(line[pos])) ||
                       line[pos] == '.'))
                    ++pos;
            if(pos == start)
                ++pos;
            add(TOKEN_NUMBER, start, pos);
        }
        else if(std::isalpha(ch) || ch == '_' || ch == '%')
        {
            ++pos;
            while(pos < end &&
                  (std::isalnum(static_cast<unsigned char>(line[pos])) ||
                   line[pos] == '_' || line[pos] == '.'))
                ++pos;
            const auto word = line.substr(start, pos - start);
            add(assemblyRegister(word) ? TOKEN_MEMBER : TOKEN_FUNCTION,
                start,
                pos);
        }
        else
        {
            ++pos;
            if(std::string_view("[](),:+-*/!").find(ch) !=
               std::string_view::npos)
                add(TOKEN_OPERATOR, start, pos);
        }
    }
    return tokens;
}

std::vector<Token> variableTokens(std::string_view line)
{
    std::vector<Token> tokens;
    auto add = [&](TokenType type, size_t start, size_t end)
    {
        if(end > start)
            tokens.push_back(
                {type, static_cast<int>(start), static_cast<int>(end - start)});
    };
    const size_t equal = line.find('=');
    const size_t end = equal == std::string_view::npos ? line.size() : equal;
    size_t colon = line.find(':');
    if(colon >= end || (colon + 1 < line.size() && line[colon + 1] == ':'))
        colon = std::string_view::npos;
    const size_t nameEnd = colon == std::string_view::npos ? end : colon;
    size_t nameStart = line.find_first_not_of(' ');
    if(nameStart == std::string_view::npos || nameStart >= nameEnd)
        nameStart = 0;
    size_t trimmedNameEnd = nameEnd;
    while(trimmedNameEnd > nameStart && line[trimmedNameEnd - 1] == ' ')
        --trimmedNameEnd;
    add(TOKEN_FUNCTION, nameStart, trimmedNameEnd);
    if(colon != std::string_view::npos)
    {
        add(TOKEN_OPERATOR, colon, colon + 1);
        size_t typeStart = colon + 1;
        while(typeStart < end && line[typeStart] == ' ')
            ++typeStart;
        size_t typeEnd = end;
        while(typeEnd > typeStart && line[typeEnd - 1] == ' ')
            --typeEnd;
        add(TOKEN_TYPE, typeStart, typeEnd);
    }
    if(equal == std::string_view::npos)
        return tokens;
    add(TOKEN_OPERATOR, equal, equal + 1);
    for(size_t pos = equal + 1; pos < line.size();)
    {
        const size_t start = pos;
        const unsigned char ch = static_cast<unsigned char>(line[pos]);
        if(ch == '"' || ch == '\'')
        {
            const char quote = line[pos++];
            while(pos < line.size())
            {
                if(line[pos] == '\\' && pos + 1 < line.size())
                    pos += 2;
                else if(line[pos++] == quote)
                    break;
            }
            add(TOKEN_STRING, start, pos);
        }
        else if(
            std::isdigit(ch) ||
            ((ch == '-' || ch == '+') && pos + 1 < line.size() &&
             std::isdigit(static_cast<unsigned char>(line[pos + 1]))))
        {
            if(ch == '-' || ch == '+')
                ++pos;
            const bool address = pos + 1 < line.size() && line[pos] == '0' &&
                                 (line[pos + 1] == 'x' || line[pos + 1] == 'X');
            if(address)
            {
                pos += 2;
                while(pos < line.size() &&
                      std::isxdigit(static_cast<unsigned char>(line[pos])))
                    ++pos;
            }
            else
                while(pos < line.size() &&
                      (std::isdigit(static_cast<unsigned char>(line[pos])) ||
                       line[pos] == '.'))
                    ++pos;
            add(address ? TOKEN_COMMENT : TOKEN_NUMBER, start, pos);
        }
        else if(std::isalpha(ch) || ch == '_')
        {
            ++pos;
            while(pos < line.size() &&
                  (std::isalnum(static_cast<unsigned char>(line[pos])) ||
                   line[pos] == '_'))
                ++pos;
            const auto word = line.substr(start, pos - start);
            if(word == "true" || word == "false" || word == "null" ||
               word == "nullptr")
                add(TOKEN_KEYWORD, start, pos);
        }
        else
        {
            ++pos;
            if(std::string_view("{}[]()<>,*&+-/").find(ch) !=
               std::string_view::npos)
                add(TOKEN_OPERATOR, start, pos);
        }
    }
    return tokens;
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

std::array<DebuggerRect, 4> paneRects(
    const Editor& editor,
    bool stopped,
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
        EditorPathUtilities::resolveEditorPath(path),
        error);
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
        {"disasm ", "Disassemble a function pointer or address", ""},
        {"mem ", "Read bytes at an address; &var reads variable storage", ""},
        {"layout ", "Show variable size, field offsets and padding", ""},
        {"bt", "Print stack frames", ""},
        {"threads", "List threads", ""},
        {"thread ", "Select a thread by ID", ""},
        {"frame ", "Select frame ID; -- opens fuzzy frame picker", ""},
        {"up", "Inspect the caller frame", ""},
        {"down", "Inspect a younger frame", ""},
        {"help", "Show native debugger help", ""},
        {"quit", "Close the debugger session", "q"}};
    return commands;
}

void moveMenuCursor(
    int& cursor,
    int& offset,
    int size,
    int delta,
    int screenRows,
    int maxRows = 8)
{
    cursor = std::clamp(cursor + delta, 0, std::max(0, size - 1));
    const int rows = std::max(1, std::min(maxRows, screenRows - 2));
    if(cursor < offset)
        offset = cursor;
    if(cursor >= offset + rows)
        offset = cursor - rows + 1;
}

std::vector<std::string> wrapLayoutLines(
    const std::vector<std::string>& lines,
    int screenCols)
{
    const int width = std::max(1, screenCols - 6);
    std::vector<std::string> result;
    for(const auto& line : lines)
    {
        size_t start = 0;
        std::string prefix;
        do
        {
            size_t end = start;
            int cells = static_cast<int>(prefix.size());
            while(end < line.size())
            {
                const auto next = static_cast<size_t>(
                    text_utils::nextUtf8CharStart(line, static_cast<int>(end)));
                const int glyphWidth = text_utils::utf8DisplayWidth(
                    std::string_view(line).substr(end, next - end));
                if(end > start && cells + glyphWidth > width)
                    break;
                cells += glyphWidth;
                end = next;
            }
            result.push_back(prefix + line.substr(start, end - start));
            start = end;
            prefix.assign(std::min(2, width / 3), ' ');
        } while(start < line.size());
    }
    return result;
}

// The shared popup uses absolute row positions. Fold its rows into the frame
// cache so opening/closing a menu restores only its covered rows.
void overlayPopupRows(
    std::vector<std::string>& rows,
    const std::string& popup,
    const Theme& theme)
{
    size_t start = 0;
    int active = -1;
    for(size_t position = popup.find("\x1b["); position != std::string::npos;
        position = popup.find("\x1b[", position + 2))
    {
        int row = 0, column = 0, length = 0;
        if(std::sscanf(
               popup.c_str() + position,
               "\x1b[%d;%dH%n",
               &row,
               &column,
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

std::optional<std::string_view> framePickerQuery(std::string_view prompt)
{
    constexpr std::string_view prefix = "frame --";
    if(!prompt.starts_with(prefix) ||
       (prompt.size() > prefix.size() && prompt[prefix.size()] != ' '))
        return {};
    prompt.remove_prefix(prefix.size());
    while(!prompt.empty() && prompt.front() == ' ')
        prompt.remove_prefix(1);
    return prompt;
}

int framePickerRows(int screenRows)
{
    return std::clamp(screenRows / 3, 1, 8);
}

struct FramePreviewViewport
{
    int rows = 0, base = 0, maximum = 0;
};

FramePreviewViewport framePreviewViewport(
    const Editor& editor,
    int line,
    size_t count)
{
    const auto source = paneRects(editor, true, 0)[0];
    const int rows = std::max(0, source.height - 2);
    const int visible =
        rows > 0 ? std::clamp(
                       editor.screenRows - framePickerRows(editor.screenRows) -
                           1 - (source.y + 1),
                       1,
                       rows)
                 : 0;
    const int maximum = std::max(0, static_cast<int>(count) - visible);
    return {
        visible,
        std::clamp(line > 0 ? line - 1 - visible / 2 : 0, 0, maximum),
        maximum};
}

std::optional<uint64_t> codeAddress(std::string_view text)
{
    if(!text.starts_with("0x"))
        return {};
    text.remove_prefix(2);
    text = text.substr(0, text.find_first_of(" \t"));
    uint64_t value = 0;
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), value, 16);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size()
               ? std::optional<uint64_t>{value}
               : std::nullopt;
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

    void start(
        const std::vector<std::string>& args,
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
            else if(
                n < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
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
        output.push_back(
            attach ? "Attaching to process " + target
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
    closeAssemblyHelp(*ctx.editor);
    assemblyHelp->active = false;
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
    if(assemblyHelpOpen)
        updateAssemblyHelp(editor);
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
        std::vector<std::string> args{
            interpreter,
            "-u",
            "-c",
            debuggerBridge,
            backend,
            attach ? "attach" : "run",
            target};
        args.insert(
            args.end(),
            programArguments.begin(),
            programArguments.end());
        try
        {
            transport->start(
                args,
                EditorPathUtilities::currentWorkingDirectory().string());
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
    if(framePickerOpen && state != "stopped")
    {
        stackFrames.clear();
        frameMatches.clear();
        framePickerLoading = false;
        framePickerError = "Pause execution before browsing stack frames";
    }
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
    {
        strings("variables", variables, false);
        if(json_utils::has(*doc, "variables"))
        {
            variableExpressions.clear();
            strings("variableExpressions", variableExpressions, false);
        }
    }
    const int outputStart = static_cast<int>(output.size());
    assemblyOutputLines.resize(output.size(), false);
    strings("output", output, true);
    assemblyOutputLines.resize(
        output.size(),
        json_utils::get_string(*doc, "outputKind") == "assembly");
    const int discarded = std::max(0, static_cast<int>(output.size()) - 2000);
    if(output.size() > 2000)
    {
        output.erase(output.begin(), output.end() - 2000);
        assemblyOutputLines.erase(
            assemblyOutputLines.begin(),
            assemblyOutputLines.begin() + discarded);
    }
    if(json_utils::get_bool(*doc, "inspection"))
    {
        pane = 1;
        cursor[1] = offset[1] = std::max(0, outputStart - discarded);
    }
    if(auto lines = json_utils::find(*doc, "layoutLines");
       lines && lines->IsArray())
    {
        const bool append = json_utils::get_bool(*doc, "layoutAppend");
        const auto title = safe(json_utils::get_string(*doc, "layoutTitle"));
        if(!append)
        {
            sizePopupOpen = true;
            sizePopupTitle = title;
            sizePopupLines.clear();
            sizePopupCursor = sizePopupOffset = 0;
            breakpointListOpen = promptActive = false;
            framePickerOpen = false;
        }
        if(sizePopupOpen && sizePopupTitle == title)
            strings("layoutLines", sizePopupLines, true);
    }
    if(auto frames = json_utils::find(*doc, "frames");
       framePickerOpen && frames && frames->IsArray())
    {
        stackFrames.clear();
        framePickerLoading = false;
        framePickerError = safe(json_utils::get_string(*doc, "framesError"));
        if(state != "stopped")
            framePickerError = "Pause execution before browsing stack frames";
        else
        {
            std::unordered_map<std::string, bool> sourceFiles;
            for(const auto& frame : frames->GetArray())
            {
                const auto id = json_utils::get_string(frame, "id");
                if(id.empty() ||
                   id.find_first_not_of("0123456789") != std::string::npos)
                    continue;
                const bool selected = json_utils::get_bool(frame, "selected");
                const auto symbol =
                    safe(json_utils::get_string(frame, "function"));
                auto label = std::string(selected ? "* #" : "  #") + id + " ";
                std::vector<Token> colors{
                    {TOKEN_NUMBER, 2, static_cast<int>(id.size() + 1)}};
                const auto append = [&](TokenType type, const std::string& text)
                {
                    colors.push_back(
                        {type,
                         static_cast<int>(label.size()),
                         static_cast<int>(text.size())});
                    label += text;
                };
                const auto returnType =
                    safe(json_utils::get_string(frame, "returnType"));
                if(!returnType.empty() && !symbol.starts_with(returnType + " "))
                    append(TOKEN_TYPE, returnType + " ");
                const auto args = symbol.find('(');
                append(TOKEN_FUNCTION, symbol.substr(0, args));
                if(args != std::string::npos)
                    append(TOKEN_TYPE, symbol.substr(args));
                const auto path = safe(json_utils::get_string(frame, "path"));
                const int line = json_utils::get_int(frame, "line");
                bool sourceAvailable = false;
                if(!path.empty() && line > 0 && line <= 200000)
                {
                    const auto [file, added] =
                        sourceFiles.try_emplace(path, false);
                    if(added)
                        file->second = readableFrameSource(path);
                    sourceAvailable = file->second;
                }
                if(!sourceAvailable)
                    append(TOKEN_COMMENT, " [no source]");
                if(!path.empty())
                {
                    append(TOKEN_STRING, "  " + path);
                    if(line > 0)
                        append(TOKEN_NUMBER, ":" + std::to_string(line));
                }
                const auto address =
                    safe(json_utils::get_string(frame, "address"));
                const auto module =
                    safe(json_utils::get_string(frame, "module"));
                if(!module.empty() && path.empty())
                    append(TOKEN_TYPE, "  [" + module + "]");
                if(!address.empty())
                    append(TOKEN_COMMENT, "  " + address);
                stackFrames.push_back(
                    {id,
                     std::move(label),
                     symbol,
                     path,
                     address,
                     module,
                     line,
                     selected,
                     sourceAvailable,
                     std::move(colors)});
            }
        }
        filterFrames(true);
    }
    if(auto preview = json_utils::find(*doc, "framePreview");
       framePickerOpen && framePreviewAssembly && preview &&
       preview->IsObject() &&
       json_utils::get_string(*preview, "id") == framePreviewId &&
       json_utils::get_int(*preview, "token") == framePreviewToken)
    {
        framePreviewLines.clear();
        const auto error = safe(json_utils::get_string(*preview, "error"));
        if(!error.empty())
            framePreviewLines.push_back(error);
        else if(
            auto lines = json_utils::find(*preview, "lines");
            lines && lines->IsArray())
            for(const auto& line : lines->GetArray())
                if(line.IsString())
                    framePreviewLines.push_back(safe(line.GetString()));
        framePreviewLine = error.empty() && !framePreviewLines.empty() ? 1 : 0;
        if(const auto* frame = previewFrame(); frame && error.empty())
            if(const auto pc = codeAddress(frame->address))
                for(size_t i = 0; i < framePreviewLines.size(); ++i)
                    if(codeAddress(framePreviewLines[i]) == pc)
                    {
                        framePreviewLine = static_cast<int>(i + 1);
                        break;
                    }
        framePreviewCursor = std::max(0, framePreviewLine - 1);
        framePreviewScroll = 0;
        if(framePreviewLines.empty())
            framePreviewLines.push_back(
                "No instructions available for this frame");
        framePreviewHighlight = {};
    }
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
        const bool followingRequested =
            state == "stopped" && followExecutionOnStop;
        if(followingRequested)
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
                    source.push_back(sourceTextLine(text));
            }
            if((sourceChanged || backendMoved || followingRequested ||
                previousState == "running") &&
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
        breakpointCursor = std::clamp(
            breakpointCursor,
            0,
            std::max(0, static_cast<int>(breakpoints.size()) - 1));
    }
    updateBreakpointLines();
    if(breakpointListOpen && json_utils::has(*doc, "breakpoints"))
        followSelectedBreakpoint();
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

void DebuggerMode::followSelectedBreakpoint()
{
    breakpointSourceError.clear();
    if(breakpointCursor < 0 ||
       breakpointCursor >= static_cast<int>(breakpoints.size()))
        return;
    const auto& breakpoint = breakpoints[breakpointCursor];
    if(breakpoint.locations.empty() ||
       breakpoint.locations.front().path.empty() ||
       breakpoint.locations.front().line <= 0)
    {
        breakpointSourceError =
            "No source location for breakpoint #" + breakpoint.id;
        return;
    }
    const auto& location = breakpoint.locations.front();
    if(!source.empty() &&
       debuggerSourcePath(sourcePath) == debuggerSourcePath(location.path))
    {
        sourcePinned = true;
        pane = 0;
        cursor[0] = std::clamp(
            location.line - 1,
            0,
            static_cast<int>(source.size()) - 1);
        offset[0] = std::max(0, cursor[0] - 5);
    }
    else if(!selectSourceFile(location.path, location.line))
        breakpointSourceError =
            "Source unavailable for breakpoint #" + breakpoint.id;
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
            output.push_back(
                "Choose a text source file smaller than 20 MB and "
                "200000 lines");
            return false;
        }
        contents.push_back(sourceTextLine(text));
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

void DebuggerMode::inspectSelectedVariable(char action)
{
    const int row = cursor[2];
    if(state != "stopped" || row < 0 ||
       row >= static_cast<int>(variableExpressions.size()) ||
       variableExpressions[row].empty())
    {
        output.push_back("Select an available variable while stopped first");
        return;
    }
    const auto& expression = variableExpressions[row];
    execute(
        action == 'a'                    ? "disasm " + expression
        : action == 'm'                  ? "mem &(" + expression + ")"
        : action == 's' || action == 'i' ? "layout " + expression
                                         : "p " + expression);
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

bool DebuggerMode::updateFramePicker(bool refresh)
{
    if(!framePickerQuery(prompt))
    {
        framePickerOpen = false;
        return false;
    }
    if(!framePickerOpen || refresh)
    {
        if(!framePickerOpen)
            framePreviewAssembly = false;
        framePreviewKey.clear();
        ++framePreviewToken;
        stackFrames.clear();
        framePickerError.clear();
        framePickerLoading = false;
        if(state != "stopped")
            framePickerError = "Pause execution before browsing stack frames";
        else if(!transport || transport->ended)
            framePickerError = "Debugger is not connected";
        else
        {
            framePickerLoading = true;
            transport->send("frames");
        }
    }
    framePickerOpen = true;
    filterFrames();
    return true;
}

void DebuggerMode::filterFrames(bool selectCurrent)
{
    frameMatches.clear();
    framePickerCursor = framePickerOffset = 0;
    const std::string query(framePickerQuery(prompt).value_or(""));
    std::vector<std::pair<int, int>> ranked;
    std::vector<int> positions;
    for(int i = 0; i < static_cast<int>(stackFrames.size()); ++i)
    {
        int score = query.empty() ? 0
                                  : editor::helper::fuzzyScoreWithPositions(
                                        query,
                                        stackFrames[i].label,
                                        positions);
        if(!query.empty())
        {
            const int symbolScore = editor::helper::fuzzyScoreWithPositions(
                query,
                stackFrames[i].symbol,
                positions);
            if(symbolScore >= 0)
            {
                const bool prefix =
                    !positions.empty() && positions.front() == 0 &&
                    positions.back() == static_cast<int>(query.size()) - 1;
                score = std::max(
                    score,
                    100000 + symbolScore + (prefix ? 10000 : 0));
            }
        }
        if(query == stackFrames[i].id || query == "#" + stackFrames[i].id)
            score = 1000000;
        if(score >= 0)
            ranked.emplace_back(i, score);
    }
    std::stable_sort(
        ranked.begin(),
        ranked.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });
    for(const auto& [index, score] : ranked)
    {
        (void)score;
        if(selectCurrent && query.empty() && stackFrames[index].selected)
            framePickerCursor = static_cast<int>(frameMatches.size());
        frameMatches.push_back(index);
    }
    updateFramePreview();
}

const DebuggerStackFrame* DebuggerMode::previewFrame() const
{
    if(framePickerCursor < 0 ||
       framePickerCursor >= static_cast<int>(frameMatches.size()))
        return nullptr;
    const int index = frameMatches[framePickerCursor];
    return index >= 0 && index < static_cast<int>(stackFrames.size())
               ? &stackFrames[index]
               : nullptr;
}

void DebuggerMode::updateFramePreview()
{
    const auto* frame = previewFrame();
    if(!frame)
    {
        framePreviewKey.clear();
        framePreviewId.clear();
        framePreviewLines.clear();
        framePreviewSourceAvailable = false;
        framePreviewPath.clear();
        framePreviewHighlight = {};
        framePreviewLine = 0;
        ++framePreviewToken;
        return;
    }
    const auto key =
        frame->id + (framePreviewAssembly ? "|assembly|" : "|source|") +
        frame->path + ":" + std::to_string(frame->line) + "@" + frame->address;
    if(key == framePreviewKey)
        return;
    const bool reuseSource = !framePreviewAssembly &&
                             framePreviewSourceAvailable &&
                             framePreviewPath == frame->path;
    framePreviewKey = key;
    framePreviewId = frame->id;
    framePreviewLine = 0;
    framePreviewCursor = 0;
    framePreviewScroll = 0;
    ++framePreviewToken;
    if(framePreviewAssembly)
    {
        framePreviewSourceAvailable = false;
        framePreviewPath = "frame-preview.s";
        framePreviewHighlight = {};
        framePreviewLines = {"Loading frame assembly..."};
        if(transport && !transport->ended && state == "stopped")
            transport->send(
                "frame-disasm " + frame->id + " " +
                std::to_string(framePreviewToken));
        else
            framePreviewLines = {
                "Assembly preview requires a connected, stopped debugger"};
        return;
    }
    framePreviewPath = frame->path;
    if(!reuseSource)
    {
        framePreviewSourceAvailable = false;
        framePreviewHighlight = {};
        framePreviewLines.clear();
        std::error_code error;
        const auto size = std::filesystem::file_size(frame->path, error);
        if(!error && size <= 20 * 1024 * 1024 && frame->line > 0 &&
           frame->line <= 200000)
        {
            std::ifstream file(frame->path);
            std::string line;
            bool binary = false;
            while(framePreviewLines.size() < 200000 && std::getline(file, line))
            {
                if(line.find('\0') != std::string::npos)
                {
                    binary = true;
                    break;
                }
                framePreviewLines.push_back(sourceTextLine(line));
            }
            framePreviewSourceAvailable =
                !binary && file.eof() &&
                frame->line <= static_cast<int>(framePreviewLines.size());
            if(!file.eof() && !binary && framePreviewLines.size() == 200000)
                framePreviewSourceAvailable = frame->line <= 200000;
        }
    }
    if(framePreviewSourceAvailable && frame->line > 0 &&
       frame->line <= static_cast<int>(framePreviewLines.size()))
        framePreviewLine = frame->line;
    else
    {
        framePreviewSourceAvailable = false;
        framePreviewLines = {"Source unavailable for this frame. Ctrl+D "
                             "previews library assembly."};
    }
    stackFrames[frameMatches[framePickerCursor]].sourceAvailable =
        framePreviewSourceAvailable;
}

void DebuggerMode::updatePromptMenu()
{
    promptCommands.clear();
    promptMenuEntries.clear();
    promptMenuCursor = promptMenuOffset = 0;
    if(updateFramePicker())
        return;
    if(!promptActive || (prompt.find_first_of(" \t") != std::string::npos &&
                         !editor::helper::isRegexQuery(prompt)))
        return;
    std::vector<std::pair<int, int>> ranked;
    std::vector<int> positions;
    const auto& commands = debuggerCommands();
    for(int i = 0; i < static_cast<int>(commands.size()); ++i)
    {
        const auto& entry = commands[i];
        int score = editor::helper::fuzzyScoreWithPositions(
            prompt,
            entry.command,
            positions);
        if(!prompt.empty() && prompt == entry.alias)
            score = 100000;
        if(score >= 0)
            ranked.emplace_back(i, score);
    }
    std::stable_sort(
        ranked.begin(),
        ranked.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });
    for(const auto& [index, score] : ranked)
    {
        (void)score;
        const auto& entry = commands[index];
        promptCommands.emplace_back(entry.command);
        promptMenuEntries.push_back(
            std::string(entry.command) +
            std::string(
                std::max(
                    2,
                    14 - static_cast<int>(
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
        followSelectedBreakpoint();
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
        static_cast<int>(source.size()),
        static_cast<int>(output.size()),
        static_cast<int>(variables.size()),
        static_cast<int>(threads.size())};
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

std::string DebuggerMode::selectedDisassembly() const
{
    std::string instruction;
    if(framePickerOpen && framePreviewAssembly && framePreviewCursor >= 0 &&
       framePreviewCursor < static_cast<int>(framePreviewLines.size()))
        instruction = framePreviewLines[framePreviewCursor];
    else if(
        !promptActive && !framePickerOpen && pane == 1 && cursor[1] >= 0 &&
        cursor[1] < static_cast<int>(output.size()) &&
        cursor[1] < static_cast<int>(assemblyOutputLines.size()) &&
        assemblyOutputLines[cursor[1]])
        instruction = output[cursor[1]];
    if(!codeAddress(instruction))
        return {};
    const auto start = instruction.find_first_of(" \t");
    instruction.erase(0, start);
    instruction.erase(0, instruction.find_first_not_of(" \t"));
    // Strip the bridge's raw-byte column; keep instruction memory operands.
    if(const auto bytes = instruction.find("  ["); bytes != std::string::npos)
        instruction.resize(bytes);
    return instruction;
}

void DebuggerMode::closeAssemblyHelp(Editor& editor)
{
    assemblyHelpOpen = false;
    ++assemblyHelp->docsGeneration;
    if(assemblyHelpTask)
        editor.tasks.cancel(assemblyHelpTask);
    assemblyHelpTask = 0;
    assemblyHelp->docsLoading = assemblyHelp->docsIndicator = false;
    assemblyHelpRefreshAt.reset();
    assemblyHelpInstruction.clear();
}

void DebuggerMode::updateAssemblyHelp(Editor& editor, bool immediate)
{
    const auto instruction = selectedDisassembly();
    const auto now = std::chrono::steady_clock::now();
    if(instruction != assemblyHelpInstruction || immediate)
    {
        ++assemblyHelp->docsGeneration;
        if(assemblyHelpTask)
            editor.tasks.cancel(assemblyHelpTask);
        assemblyHelpTask = 0;
        assemblyHelp->docsLoading = assemblyHelp->docsIndicator = false;
        assemblyHelpInstruction = instruction;
        assemblyHelpRefreshAt =
            immediate ? now : now + std::chrono::milliseconds(300);
    }
    if(assemblyHelpRefreshAt && now >= *assemblyHelpRefreshAt)
    {
        assemblyHelpRefreshAt.reset();
        assemblyHelpOffset = 0;
        assemblyHelpTask =
            assembly_instruction_help::load(editor, assemblyHelp, instruction);
        editor.needsFullRedraw = true;
    }
    if(assemblyHelp->docsLoading && !assemblyHelp->docsIndicator &&
       now - assemblyHelp->docsStarted >= std::chrono::milliseconds(300))
    {
        assemblyHelp->docsIndicator = true;
        editor.needsFullRedraw = true;
    }
}

std::optional<ModeState> DebuggerMode::handle(
    ModeContext& ctx,
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
                close = selectSourceFile(
                    filePicker->matches[index].file.path,
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
    if(sizePopupOpen)
    {
        if(key == 27 || key == 'q' ||
           key == keyCode(control::ControlKey::ENTER))
            sizePopupOpen = false;
        else
        {
            const int count = static_cast<int>(
                wrapLayoutLines(sizePopupLines, ctx.editor->screenCols).size());
            const int page =
                std::max(1, std::min(18, ctx.editor->screenRows - 2));
            int delta = 0;
            if(key == 'j' || key == keyCode(control::ControlKey::CTRL_J) ||
               key == keyCode(navigation::NavigationKey::ARROW_DOWN))
                delta = 1;
            else if(
                key == 'k' || key == keyCode(control::ControlKey::CTRL_K) ||
                key == keyCode(navigation::NavigationKey::ARROW_UP))
                delta = -1;
            else if(key == keyCode(navigation::NavigationKey::PAGE_DOWN))
                delta = page;
            else if(key == keyCode(navigation::NavigationKey::PAGE_UP))
                delta = -page;
            else if(
                key == 'g' || key == keyCode(navigation::NavigationKey::HOME))
                delta = -count;
            else if(
                key == 'G' || key == keyCode(navigation::NavigationKey::END))
                delta = count;
            moveMenuCursor(
                sizePopupCursor,
                sizePopupOffset,
                count,
                delta,
                ctx.editor->screenRows,
                18);
        }
        return {};
    }
    const bool assemblyPreview = framePickerOpen && framePreviewAssembly;
    const bool helpToggle = assemblyPreview
                                ? key == keyCode(control::ControlKey::CTRL_I)
                                : !promptActive && pane == 1 && key == 'i';
    if(assemblyHelpOpen &&
       (helpToggle || key == keyCode(control::ControlKey::ESC)))
    {
        closeAssemblyHelp(*ctx.editor);
        return {};
    }
    if(helpToggle && !selectedDisassembly().empty())
    {
        assemblyHelpOpen = true;
        updateAssemblyHelp(*ctx.editor, true);
        return {};
    }
    if(assemblyHelpOpen && (key == keyCode(control::ControlKey::CTRL_J) ||
                            key == keyCode(control::ControlKey::CTRL_K)))
    {
        assemblyHelpOffset = std::max(
            0,
            assemblyHelpOffset +
                (key == keyCode(control::ControlKey::CTRL_J) ? 1 : -1));
        return {};
    }
    if(promptActive)
    {
        if(framePickerOpen)
        {
            if(framePreviewAssembly &&
               (key == keyCode(control::ControlKey::SHIFT_CTRL_J) ||
                key == keyCode(control::ControlKey::SHIFT_CTRL_K)))
            {
                const auto viewport = framePreviewViewport(
                    *ctx.editor,
                    framePreviewLine,
                    framePreviewLines.size());
                framePreviewCursor = std::clamp(
                    framePreviewCursor +
                        (key == keyCode(control::ControlKey::SHIFT_CTRL_J)
                             ? 1
                             : -1),
                    0,
                    std::max(
                        0,
                        static_cast<int>(framePreviewLines.size()) - 1));
                framePreviewScroll = std::clamp(
                                         framePreviewCursor - viewport.rows / 2,
                                         0,
                                         viewport.maximum) -
                                     viewport.base;
                if(assemblyHelpOpen)
                    updateAssemblyHelp(*ctx.editor);
                return {};
            }
            if(key == keyCode(control::ControlKey::CTRL_D))
            {
                framePreviewAssembly = !framePreviewAssembly;
                updateFramePreview();
                return {};
            }
            if(key == 27)
            {
                prompt = "frame ";
                framePickerOpen = false;
                updatePromptMenu();
                return {};
            }
            if(key == keyCode(control::ControlKey::CTRL_J) ||
               key == keyCode(navigation::NavigationKey::ARROW_DOWN) ||
               key == keyCode(control::ControlKey::CTRL_K) ||
               key == keyCode(control::ControlKey::SHIFT_TAB) ||
               key == keyCode(navigation::NavigationKey::ARROW_UP))
            {
                const int delta =
                    key == keyCode(control::ControlKey::CTRL_J) ||
                            key ==
                                keyCode(navigation::NavigationKey::ARROW_DOWN)
                        ? 1
                        : -1;
                moveMenuCursor(
                    framePickerCursor,
                    framePickerOffset,
                    frameMatches.size(),
                    delta,
                    ctx.editor->screenRows,
                    framePickerRows(ctx.editor->screenRows));
                updateFramePreview();
                return {};
            }
            if(key == keyCode(control::ControlKey::CTRL_R))
            {
                updateFramePicker(true);
                return {};
            }
            if(key == keyCode(control::ControlKey::CTRL_U))
            {
                prompt = "frame -- ";
                filterFrames();
                return {};
            }
            if(key == keyCode(control::ControlKey::ENTER) || key == '\t')
            {
                if(framePickerCursor < 0 ||
                   framePickerCursor >= static_cast<int>(frameMatches.size()))
                    return {};
                const auto command =
                    "frame " + stackFrames[frameMatches[framePickerCursor]].id;
                framePickerOpen = false;
                prompt = command;
                if(key == keyCode(control::ControlKey::ENTER))
                {
                    execute(command);
                    promptActive = false;
                    prompt.clear();
                }
                updatePromptMenu();
                return {};
            }
            if(prompt == "frame --" && key > 32 && key < 127)
                prompt += ' ';
        }
        if(key == 27)
        {
            promptActive = false;
            prompt.clear();
            completions.clear();
            updatePromptMenu();
        }
        else if(
            (key == keyCode(control::ControlKey::CTRL_J) ||
             key == keyCode(navigation::NavigationKey::ARROW_DOWN)) &&
            !promptCommands.empty())
            moveMenuCursor(
                promptMenuCursor,
                promptMenuOffset,
                promptCommands.size(),
                1,
                ctx.editor->screenRows);
        else if(
            (key == keyCode(control::ControlKey::CTRL_K) ||
             key == keyCode(navigation::NavigationKey::ARROW_UP)) &&
            !promptCommands.empty())
            moveMenuCursor(
                promptMenuCursor,
                promptMenuOffset,
                promptCommands.size(),
                -1,
                ctx.editor->screenRows);
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
        const int previous = breakpointCursor;
        if(key == 27 || key == 'q')
            breakpointListOpen = false;
        else if(
            key == 'j' || key == keyCode(control::ControlKey::CTRL_J) ||
            key == keyCode(navigation::NavigationKey::ARROW_DOWN))
            moveMenuCursor(
                breakpointCursor,
                breakpointOffset,
                breakpoints.size(),
                1,
                ctx.editor->screenRows);
        else if(
            key == 'k' || key == keyCode(control::ControlKey::CTRL_K) ||
            key == keyCode(navigation::NavigationKey::ARROW_UP))
            moveMenuCursor(
                breakpointCursor,
                breakpointOffset,
                breakpoints.size(),
                -1,
                ctx.editor->screenRows);
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
        if(breakpointListOpen && breakpointCursor != previous)
            followSelectedBreakpoint();
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
    else if(
        pane == 1 && (key == keyCode(control::ControlKey::SHIFT_CTRL_J) ||
                      key == keyCode(control::ControlKey::SHIFT_CTRL_K)))
        cursor[1] += key == keyCode(control::ControlKey::SHIFT_CTRL_J) ? 1 : -1;
    else if(key == 'v' && paneCount == 4)
        pane = 2;
    else if(key == 't' && paneCount == 4)
        pane = 3;
    else if(key == 'j' || key == keyCode(navigation::NavigationKey::ARROW_DOWN))
        ++cursor[pane];
    else if(key == 'k' || key == keyCode(navigation::NavigationKey::ARROW_UP))
        --cursor[pane];
    else if(
        pane == 2 && (key == 'p' || key == 'a' || key == 'm' || key == 'i' ||
                      key == 's' || key == keyCode(control::ControlKey::ENTER)))
        inspectSelectedVariable(
            key == keyCode(control::ControlKey::ENTER)
                ? 'p'
                : static_cast<char>(key));
    else if(
        key == keyCode(control::ControlKey::ENTER) && state == "stopped" &&
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
    const auto* preview = framePickerOpen ? previewFrame() : nullptr;
    const bool previewing = preview != nullptr;
    const auto rects = paneRects(
        editor,
        hasStoppedSnapshot || state == "stopped",
        previewing ? 0 : pane);
    clamp(editor);
    if(assemblyHelpOpen)
    {
        if((!framePickerOpen && (promptActive || pane != 1)) ||
           (framePickerOpen && !framePreviewAssembly) || sizePopupOpen ||
           breakpointListOpen)
            closeAssemblyHelp(editor);
        else
            updateAssemblyHelp(editor);
    }
    const int sourceRows = std::max(0, rects[0].height - 2);
    const auto& shownSource = previewing ? framePreviewLines : source;
    auto& shownHighlight = previewing ? framePreviewHighlight : highlight;
    const auto& shownPath = previewing ? framePreviewPath : sourcePath;
    const int shownLine = previewing ? framePreviewLine : executionLine;
    const auto viewport =
        framePreviewViewport(editor, shownLine, shownSource.size());
    const int sourceOffset =
        previewing ? std::clamp(
                         viewport.base +
                             (framePreviewAssembly ? framePreviewScroll : 0),
                         0,
                         viewport.maximum)
                   : offset[0];
    if(previewing && framePreviewAssembly)
    {
        shownHighlight.tokens.clear();
        for(const auto& line : shownSource)
            shownHighlight.tokens.push_back(disassemblyTokens(line));
    }
    else
        editor.highlightPreviewLines(
            shownHighlight,
            shownPath,
            shownSource,
            std::min(
                shownSource.size(),
                static_cast<size_t>(sourceOffset + sourceRows)));
    auto shownBreakpoints = breakpointLines;
    if(previewing)
    {
        shownBreakpoints.clear();
        if(!framePreviewAssembly && framePreviewSourceAvailable)
            for(const auto& bp : breakpoints)
                if(bp.enabled)
                    for(const auto& location : bp.locations)
                        if(location.enabled &&
                           debuggerSourcePath(location.path) ==
                               debuggerSourcePath(preview->path))
                            shownBreakpoints.insert(location.line);
    }
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
    const std::array<std::string, 4>
        names{"Source", "Output", "Variables", "Threads"};
    auto header = [&](int row, const std::string& text)
    {
        atPosition(row, 1);
        out += editor.theme.reset() + editor.theme.uiAccent() +
               fit(safe(text), width);
    };
    header(
        1,
        " Debugger [" + backend + "] " + displayState +
            "  Focus: " + names[pane] + "  " + target);
    header(
        2,
        framePickerOpen
            ? " Ctrl-J/K frames   Ctrl+D source/assembly   " +
                  std::string(Terminal::shiftChordLabel()) +
                  "+J/K browse assembly   Ctrl-I help   Enter selects   "
                  "Esc cancels"
            : " j/k rows   b breakpoint   Ctrl-P file   Tab next pane   "
              "Shift-Tab previous pane   v variables   t threads   : "
              "command   q quit");
    const std::array<const std::vector<std::string>*, 4>
        lists{&shownSource, &output, &variables, &threads};
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
        if(which == 0 && previewing)
        {
            title =
                " " +
                std::string(
                    framePreviewAssembly ? "Assembly preview #" : "Preview #") +
                preview->id;
            if(framePreviewAssembly)
            {
                title += " " + (preview->module.empty() ? preview->symbol
                                                        : preview->module);
                title += " [" + std::to_string(sourceOffset + 1) + "/" +
                         std::to_string(shownSource.size()) + "]";
            }
            else if(!preview->path.empty())
                title +=
                    " " +
                    std::filesystem::path(preview->path).filename().string() +
                    ":" + std::to_string(preview->line) + " " + preview->symbol;
            else
                title += " " + preview->symbol + " (source unavailable)";
        }
        else if(which == 0 && sourcePinned)
            title += " (browse)";
        else if(displayRunning && hasStoppedSnapshot && which != 1)
            title += " (last stop)";
        if(which == 0 && !previewing)
        {
            if(!sourcePinned && !frameFunction.empty())
                title +=
                    " #" + std::to_string(frameIndex) + " " + frameFunction;
            if(!sourcePath.empty())
                title +=
                    " " + sourcePath +
                    (sourcePinned ? ":" + std::to_string(cursor[0] + 1) : "");
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
            const int index = (which == 0 ? sourceOffset : offset[which]) + row;
            const auto& lines = *lists[which];
            out += editor.theme.reset();
            const bool selected =
                which == 0 && previewing
                    ? framePreviewAssembly && framePreviewCursor == index
                    : pane == which && cursor[which] == index;
            const bool executing = displayState == "stopped" && which == 0 &&
                                   shownLine > 0 && index + 1 == shownLine &&
                                   (previewing || executionPath.empty() ||
                                    debuggerSourcePath(sourcePath) ==
                                        debuggerSourcePath(executionPath));
            const auto rowBackground = [&]() -> std::string
            {
                if(executing)
                    return editor.theme.debuggerRow(
                        Theme::DebuggerRow::Execution);
                if(selected)
                    return editor.theme.selection();
                const bool alternate =
                    index < static_cast<int>(lines.size()) && index % 2 != 0;
                if(which == 0)
                    return editor.theme.debuggerRow(Theme::DebuggerRow::Source);
                if(which == 2)
                    return editor.theme.debuggerRow(
                        Theme::DebuggerRow::Variables);
                if(which == 3)
                    return editor.theme
                        .debuggerRow(Theme::DebuggerRow::Threads, alternate);
                return {};
            }();
            out += rowBackground;
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
                const auto prefix = which == 0 && previewing &&
                                            (framePreviewAssembly ||
                                             !framePreviewSourceAvailable)
                                        ? std::string(
                                              executing  ? "> "
                                              : selected ? "+ "
                                                         : "  ")
                                    : which == 0
                                        ? std::string(
                                              executing  ? ">"
                                              : selected ? "+"
                                                         : " ") +
                                              std::to_string(index + 1) + " "
                                        : std::string(selected ? "> " : "  ");
                const int prefixWidth =
                    std::min(inner, text_utils::utf8DisplayWidth(prefix));
                const bool breakpoint =
                    which == 0 && shownBreakpoints.contains(index + 1);
                if(breakpoint)
                    out += color::rgbBg(105, 38, 50) + editor.theme.baseFg();
                else
                    out += editor.theme.baseFg();
                out += fit(prefix, prefixWidth);
                if(breakpoint)
                {
                    out += editor.theme.reset();
                    out += rowBackground;
                    out += editor.theme.baseFg();
                }
                const int available = inner - prefixWidth;
                const auto text = fit(lines[index], available);
                std::vector<Token> assemblyTokens;
                std::vector<Token> variableColors;
                const std::vector<Token>* tokens = nullptr;
                if(which == 0 &&
                   index < static_cast<int>(shownHighlight.tokens.size()))
                    tokens = &shownHighlight.tokens[index];
                else if(
                    which == 1 &&
                    index < static_cast<int>(assemblyOutputLines.size()) &&
                    assemblyOutputLines[index])
                {
                    assemblyTokens = disassemblyTokens(lines[index]);
                    tokens = &assemblyTokens;
                }
                else if(which == 2)
                {
                    variableColors = variableTokens(text);
                    tokens = &variableColors;
                }
                if(tokens)
                {
                    std::vector<TokenType> colors(text.size(), TOKEN_NORMAL);
                    for(const auto& token : *tokens)
                        for(int c = std::max(0, token.start);
                            c < std::min(
                                    static_cast<int>(text.size()),
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
    std::vector<std::string> menuColors;
    std::vector<std::vector<widgets::PopupColorSpan>> menuSpans;
    int menuCursor = 0, menuOffset = 0;
    std::string menuTitle;
    if(sizePopupOpen)
    {
        menuEntries = wrapLayoutLines(sizePopupLines, width);
        moveMenuCursor(
            sizePopupCursor,
            sizePopupOffset,
            menuEntries.size(),
            0,
            editor.screenRows,
            18);
        menuCursor = sizePopupCursor;
        menuOffset = sizePopupOffset;
        menuTitle = sizePopupTitle + "  " +
                    std::to_string(sizePopupCursor + 1) + "/" +
                    std::to_string(menuEntries.size());
    }
    else if(framePickerOpen)
    {
        moveMenuCursor(
            framePickerCursor,
            framePickerOffset,
            frameMatches.size(),
            0,
            editor.screenRows,
            framePickerRows(editor.screenRows));
        for(const int index : frameMatches)
        {
            menuEntries.push_back(stackFrames[index].label);
            menuColors.push_back(
                !stackFrames[index].sourceAvailable ? editor.theme.uiDim()
                : stackFrames[index].selected       ? editor.theme.uiSuccess()
                                                    : editor.theme.uiInfo());
            auto& spans = menuSpans.emplace_back();
            for(const auto& token : stackFrames[index].colors)
                spans.push_back(
                    {static_cast<size_t>(token.start),
                     static_cast<size_t>(token.length),
                     token.type == TOKEN_COMMENT ? editor.theme.uiDim()
                     : !stackFrames[index].sourceAvailable
                         ? editor.theme.dimmedSyntax(token.type, 2)
                         : editor.theme.syntax(token.type)});
        }
        menuCursor = framePickerCursor;
        menuOffset = framePickerOffset;
        menuTitle = "Stack frames " + std::to_string(frameMatches.size()) +
                    "/" + std::to_string(stackFrames.size());
        if(preview && !framePreviewAssembly && !preview->path.empty())
            menuTitle +=
                "  " +
                std::filesystem::path(preview->path).filename().string() + ":" +
                std::to_string(preview->line);
        menuTitle += "  Ctrl-J/K select  Ctrl-D source/asm  Enter choose  "
                     "Ctrl-R refresh";
    }
    else if(breakpointListOpen)
    {
        moveMenuCursor(
            breakpointCursor,
            breakpointOffset,
            breakpoints.size(),
            0,
            editor.screenRows);
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
                    "  Ctrl-J/K preview source  d delete  Esc close";
    }
    else if(promptActive && !promptMenuEntries.empty())
    {
        menuEntries = promptMenuEntries;
        menuCursor = promptMenuCursor;
        menuOffset = promptMenuOffset;
        menuTitle = "Debugger commands [" + backend +
                    "]  Ctrl-J/K select  Tab/Enter choose";
    }
    const auto frameQuery =
        framePickerOpen ? std::string(framePickerQuery(prompt).value_or(""))
                        : std::string{};
    const auto regexError =
        editor::helper::regexQueryError(framePickerOpen ? frameQuery : prompt);
    if(sizePopupOpen || framePickerOpen || breakpointListOpen ||
       !menuEntries.empty() || (promptActive && !regexError.empty()))
    {
        std::vector<int> filtered;
        for(int i = 0; i < static_cast<int>(menuEntries.size()); ++i)
            filtered.push_back(i);
        std::string popup;
        widgets::drawCommandPopup(
            popup,
            {.frame =
                 {.theme = editor.theme,
                  .screenRows = editor.screenRows,
                  .screenCols = width},
             .entries = menuEntries,
             .filtered = filtered,
             .offset = menuOffset,
             .cursor = menuCursor,
             .title = menuTitle,
             .emptyMessage =
                 !regexError.empty() ? regexError
                 : framePickerOpen
                     ? (framePickerLoading         ? "Loading stack frames..."
                        : framePickerError.empty() ? "No matching stack frames"
                                                   : framePickerError)
                 : sizePopupOpen ? "No layout information"
                                 : "No breakpoints",
             .maxRows =
                 sizePopupOpen
                     ? std::clamp(static_cast<int>(menuEntries.size()), 1, 18)
                 : framePickerOpen ? framePickerRows(editor.screenRows)
                                   : 8,
             .rowColors = menuColors,
             .rowSpans = menuSpans});
        overlayPopupRows(rows, popup, editor.theme);
    }
    if(assemblyHelpOpen)
    {
        std::string popup;
        assembly_instruction_help::draw(
            popup,
            editor,
            *assemblyHelp,
            assemblyHelpOffset,
            framePickerOpen ? "Ctrl-I/Esc: close | Ctrl-J/K: scroll"
                            : "i/Esc: close | Ctrl-J/K: scroll");
        overlayPopupRows(rows, popup, editor.theme);
    }
    widgets::ViewStatusBarView status{.theme = editor.theme};
    status.modeLabel = name();
    status.context =
        sourcePinned
            ? "source " +
                  std::filesystem::path(sourcePath).filename().string() + ":" +
                  std::to_string(cursor[0] + 1)
        : frameFunction.empty()
            ? backend
            : "frame #" + std::to_string(frameIndex) + " " + frameFunction;
    status.state = displayState;
    status.flags.push_back(
        sizePopupOpen        ? "Size/padding"
        : framePickerOpen    ? "Stack frames"
        : breakpointListOpen ? "Breakpoints"
                             : names[pane]);
    status.flags.push_back(
        sizePopupOpen ? "Esc closes popup"
        : attach      ? "q detaches"
                      : "q ends session");
    editor.appendViewStatusBar(out, std::move(status));
    out += Terminal::cursorPos(editor.screenRows + 2, 1);
    out += editor.theme.reset();
    out += Terminal::ESC_CLEAR_LINE;
    if(assemblyHelpOpen && !promptActive)
        out +=
            fit(framePickerOpen
                    ? " Instruction help: Ctrl-I/Esc close  Ctrl-J/K scroll "
                      " Ctrl+Shift-J/K or Alt-J/K browse instructions"
                    : " Instruction help: i/Esc close  Ctrl-J/K scroll  j/k "
                      "browse instructions",
                width);
    else if(sizePopupOpen)
        out +=
            fit(" Size/padding: j/k or Ctrl-J/K scroll  PgUp/PgDn page  "
                "g/G first/last  Esc/Enter close",
                width);
    else if(promptActive)
    {
        out += fit(safe(":" + prompt), width);
        out += Terminal::cursorPos(
            editor.screenRows + 2,
            std::min(width, static_cast<int>(prompt.size()) + 2));
        out += Terminal::ESC_SHOW_CURSOR;
    }
    else if(breakpointListOpen)
        out +=
            fit(breakpointSourceError.empty()
                    ? " Breakpoints: Ctrl-J/K previews file/line  d deletes "
                      " Esc closes"
                    : " " + breakpointSourceError +
                          "  Ctrl-J/K browse  Esc closes",
                width);
    else if(pane == 2)
        out +=
            fit(" Variables: j/k select  p/Enter print  a disassemble  "
                "m raw bytes  s size/padding",
                width);
    else if(pane == 1 && !selectedDisassembly().empty())
        out +=
            fit(" Disassembly: j/k rows  i instruction help  Tab next pane  "
                ": command",
                width);
    else if(sourcePinned)
        out +=
            fit(" Browsing source: j/k row  b toggle breakpoint  Ctrl-P "
                "file  n/step or thread/frame returns to execution",
                width);
    else if(displayRunning && hasStoppedSnapshot)
        out +=
            fit(" Running; source, variables and threads show the last "
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
