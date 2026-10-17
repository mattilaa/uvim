#pragma once

#include "log.h"
#include "text_utils.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

class ProcessPipe
{
public:
    // Blocking part of a pipe operation; polls `cancelled` while it waits.
    using BlockingWait =
        std::function<void(const std::function<bool()>& cancelled)>;
    // Runs `wait` while keeping the UI responsive (progress, queued input).
    // Returns false when the user cancelled.
    using ForegroundRunner =
        std::function<bool(const std::string& label, const std::string& channel,
                           const BlockingWait& wait)>;

    // Installs the runner for pipes used on the calling (UI) thread. Pipes
    // used from any other thread, or with no runner, block inline as before.
    static void setForegroundRunner(ForegroundRunner runner)
    {
        foregroundRunner() = std::move(runner);
        foregroundOwner() = std::this_thread::get_id();
    }

    ProcessPipe() = default;

    explicit ProcessPipe(const std::vector<std::string>& args)
    {
        open(args);
    }

    ProcessPipe(std::initializer_list<std::string> args)
    {
        open(std::vector<std::string>(args));
    }

    ProcessPipe(const std::vector<std::string>& args, const char* mode)
    {
        open(args, mode);
    }

    ProcessPipe(std::initializer_list<std::string> args, const char* mode)
    {
        open(std::vector<std::string>(args), mode);
    }

    ProcessPipe(const std::string& shellCommand, const char* mode)
    {
        openShell(shellCommand, mode);
    }

    ProcessPipe(const ProcessPipe&) = delete;
    ProcessPipe& operator=(const ProcessPipe&) = delete;

    ProcessPipe(ProcessPipe&& other) noexcept
    {
        moveFrom(other);
    }

    ProcessPipe& operator=(ProcessPipe&& other) noexcept
    {
        if(this != &other)
        {
            close();
            moveFrom(other);
        }
        return *this;
    }

    ~ProcessPipe()
    {
        close();
    }

    bool open(const std::vector<std::string>& args)
    {
        return open(args, "r");
    }

    bool open(const std::vector<std::string>& args, const char* mode)
    {
        close();
        if(args.empty())
            return false;

        commandName = args.front();
        commandLine = joinArgsForLog(args);

#ifdef _WIN32
        return openShell(commandLineFromArgs(args), mode);
#else
        const bool writeMode = mode && mode[0] == 'w';
        // Build argv before fork(): the child of a multithreaded process may
        // only make async-signal-safe calls, so no allocation after fork.
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for(const auto& arg : args)
            argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);

        int pipefd[2];
        if(::pipe(pipefd) != 0)
        {
            logLaunchFailure("pipe", errno);
            return false;
        }

        pid_t child = ::fork();
        if(child == -1)
        {
            const int err = errno;
            ::close(pipefd[0]);
            ::close(pipefd[1]);
            logLaunchFailure("fork", err);
            return false;
        }

        if(child == 0)
        {
            if(writeMode)
            {
                ::close(pipefd[1]);
                ::dup2(pipefd[0], STDIN_FILENO);
                ::close(pipefd[0]);
            }
            else
            {
                ::close(pipefd[0]);
                ::dup2(pipefd[1], STDOUT_FILENO);
                ::close(pipefd[1]);
            }

            int devNull = ::open("/dev/null", O_WRONLY);
            if(devNull != -1)
            {
                ::dup2(devNull, STDERR_FILENO);
                ::close(devNull);
            }

            ::execvp(argv[0], argv.data());
            _exit(127);
        }

        pid = child;
        if(writeMode)
        {
            ::close(pipefd[0]);
            file = ::fdopen(pipefd[1], "w");
            if(!file)
                ::close(pipefd[1]);
        }
        else
        {
            ::close(pipefd[1]);
            file = ::fdopen(pipefd[0], "r");
            if(!file)
                ::close(pipefd[0]);
        }

        if(!file)
        {
            waitForChild();
            logLaunchFailure("fdopen", errno);
            return false;
        }
        return true;
#endif
    }

    bool openShell(const std::string& shellCommand, const char* mode)
    {
        close();
        commandName = shellCommand;
        commandLine = shellCommand;
#ifdef _WIN32
        file = _popen(shellCommand.c_str(), mode);
        if(!file)
            logLaunchFailure("_popen", errno);
#else
        std::vector<std::string> args = {"/bin/sh", "-c", shellCommand};
        bool opened = open(args, mode);
        if(opened)
        {
            commandName = shellCommand;
            commandLine = shellCommand;
        }
        return opened;
#endif
        return file != nullptr;
    }

    bool isOpen() const
    {
        return file != nullptr;
    }

    explicit operator bool() const
    {
        return isOpen();
    }

    FILE* get() const
    {
        return file;
    }

    std::string readLine(std::size_t bufferSize = 4096)
    {
        if(!file)
            return {};
        usedStdio = true;

        std::string line(bufferSize, '\0');
        if(!std::fgets(line.data(), static_cast<int>(line.size()), file))
            return {};

        line.resize(std::char_traits<char>::length(line.c_str()));
        while(!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        return line;
    }

    std::string readAll(std::size_t bufferSize = 4096)
    {
        std::string output;
        if(!file)
            return output;
        if(runsInForeground())
        {
            foregroundRunner()(foregroundLabel(), foregroundChannel(),
                               [&](const std::function<bool()>& cancelled)
                               { output = readAllBlocking(bufferSize, cancelled); });
            return output;
        }
        return readAllBlocking(bufferSize, {});
    }

    std::string readAllBlocking(std::size_t bufferSize,
                                const std::function<bool()>& cancelled)
    {
        std::string output;
        if(!file)
            return output;
#ifndef _WIN32
        // Cancellable path: poll the descriptor so a cancel request can stop
        // the child. Only valid while stdio has not buffered any input.
        if(cancelled && !usedStdio)
        {
            const int fd = ::fileno(file);
            std::string buffer(bufferSize, '\0');
            while(true)
            {
                pollfd pfd{fd, POLLIN, 0};
                const int ready = ::poll(&pfd, 1, 50);
                if(ready < 0 && errno == EINTR)
                    continue;
                if(ready == 0)
                {
                    if(cancelled())
                    {
                        terminate();
                        break;
                    }
                    continue;
                }
                const ssize_t n = ::read(fd, buffer.data(), buffer.size());
                if(n < 0 && errno == EINTR)
                    continue;
                if(n <= 0)
                    break;
                output.append(buffer.data(), static_cast<size_t>(n));
            }
            return output;
        }
#else
        (void)cancelled;
#endif

        std::string buffer(bufferSize, '\0');
        while(true)
        {
            size_t n = std::fread(buffer.data(), 1, buffer.size(), file);
            if(n > 0)
                output.append(buffer.data(), n);
            if(n < buffer.size())
                break;
        }
        return output;
    }

    bool write(std::string_view data)
    {
        return file &&
               std::fwrite(data.data(), 1, data.size(), file) == data.size();
    }

    bool flush()
    {
        return file && std::fflush(file) == 0;
    }

    int close()
    {
#ifndef _WIN32
        // Closing waits for the child to exit, which can be slow (e.g. git
        // commit hooks); run that wait through the foreground runner too.
        if(pid > 0 && runsInForeground())
        {
            int result = -1;
            foregroundRunner()(foregroundLabel(), foregroundChannel(),
                               [&](const std::function<bool()>& cancelled)
                               { result = closeBlocking(cancelled); });
            return result;
        }
#endif
        return closeBlocking({});
    }

    int closeBlocking(const std::function<bool()>& cancelled)
    {
        int result = -1;
        if(file)
        {
#ifdef _WIN32
            result = _pclose(file);
            file = nullptr;
            logExitFailure(result);
            return result;
#else
            result = std::fclose(file);
            file = nullptr;
#endif
        }

#ifndef _WIN32
        const int status = waitForChild(cancelled);
        logExitFailure(status);
        return result == 0 ? status : result;
#else
        (void)cancelled;
        return result;
#endif
    }

private:
    static ForegroundRunner& foregroundRunner()
    {
        static ForegroundRunner runner;
        return runner;
    }

    static std::thread::id& foregroundOwner()
    {
        static std::thread::id owner;
        return owner;
    }

    bool runsInForeground() const
    {
        return foregroundRunner() &&
               std::this_thread::get_id() == foregroundOwner();
    }

    std::string_view toolName() const
    {
        std::string_view name = commandName;
        if(size_t space = name.find(' '); space != std::string_view::npos)
            name = name.substr(0, space);
        if(size_t slash = name.find_last_of("/\\");
           slash != std::string_view::npos)
            name.remove_prefix(slash + 1);
        return name;
    }

    // "git log", "clang-format", "sh: make -j" ...
    std::string foregroundLabel() const
    {
        std::string_view tool = toolName();
        if(tool == "git")
        {
            // Skip "-C <dir>" and option flags to find the subcommand.
            std::string_view rest = std::string_view(commandLine);
            size_t pos = 0;
            std::vector<std::string_view> words;
            while(pos < rest.size())
            {
                size_t end = rest.find(' ', pos);
                if(end == std::string_view::npos)
                    end = rest.size();
                words.push_back(rest.substr(pos, end - pos));
                pos = end + 1;
            }
            for(size_t i = 1; i < words.size(); ++i)
            {
                if(words[i] == "-C")
                {
                    ++i;
                    continue;
                }
                if(!words[i].empty() && words[i][0] != '-')
                    return "git " + std::string(words[i]);
            }
        }
        if(commandName == commandLine && tool != commandName)
        {
            std::string shell(commandName.substr(0, 32));
            return "sh: " + shell;
        }
        return std::string(tool);
    }

    // Git invocations share a channel so they run in the order issued.
    std::string foregroundChannel() const
    {
        return toolName() == "git" ? "git" : std::string{};
    }

#ifndef _WIN32
    void terminate()
    {
        if(pid > 0)
            ::kill(pid, SIGTERM);
    }
#endif

    bool shouldLogToolFailure() const
    {
        std::string_view name = commandName;
        size_t slash = name.find_last_of("/\\");
        if(text_utils::is_found(slash))
            name.remove_prefix(slash + 1);
#ifdef _WIN32
        if(name.size() > 4 && name.substr(name.size() - 4) == ".exe")
            name.remove_suffix(4);
#endif
        return name == "fzf" || name == "rg" || name == "ripgrep" ||
               name.rfind("fzf ", 0) == 0 || name.rfind("rg ", 0) == 0 ||
               name.rfind("ripgrep ", 0) == 0 ||
               text_utils::contains(name, "fzf") ||
               text_utils::contains(name, "ripgrep");
    }

    static std::string joinArgsForLog(const std::vector<std::string>& args)
    {
        std::string out;
        for(size_t i = 0; i < args.size(); ++i)
        {
            if(i > 0)
                out += ' ';
            out += args[i];
        }
        return out;
    }

    void logLaunchFailure(const char* stage, int err) const
    {
#if defined(UVIM_DEBUG_LOGGING) || defined(UVIM_DEBUG_LSP)
        if(!shouldLogToolFailure())
            return;
        static mla::log::FileLogger logger("uvim");
        LOG_ERROR(logger,
                  "external tool '{}' failed to launch at {}: errno={} ({}) command='{}'",
                  commandName, stage, err, std::strerror(err), commandLine);
#else
        (void)stage;
        (void)err;
#endif
    }

    void logExitFailure(int status) const
    {
#if defined(UVIM_DEBUG_LOGGING) || defined(UVIM_DEBUG_LSP)
        if(!shouldLogToolFailure() || status == 0)
            return;
        if(status != 126 && status != 127)
            return;
        static mla::log::FileLogger logger("uvim");
        LOG_ERROR(logger, "external tool '{}' exited with status {} command='{}'",
                  commandName, status, commandLine);
#else
        (void)status;
#endif
    }

#ifdef _WIN32
    static std::string quoteArg(const std::string& arg)
    {
        // _popen runs cmd.exe, which treats ^ & | < > ( ) specially unless
        // they are inside double quotes (e.g. git's "<hash>^{commit}").
        if(arg.empty() ||
           text_utils::is_found(arg.find_first_of(" \t\"^&|<>()")))
        {
            std::string out = "\"";
            for(char ch : arg)
            {
                if(ch == '"')
                    out += "\\\"";
                else
                    out += ch;
            }
            out += "\"";
            return out;
        }
        return arg;
    }

    static std::string commandLineFromArgs(const std::vector<std::string>& args)
    {
        std::string command;
        for(size_t i = 0; i < args.size(); ++i)
        {
            if(i > 0)
                command += ' ';
            command += quoteArg(args[i]);
        }
        command += " 2>NUL";
        return command;
    }
#else
    int waitForChild(const std::function<bool()>& cancelled = {})
    {
        if(pid <= 0)
            return -1;

        int status = 0;
        if(cancelled)
        {
            bool terminated = false;
            while(true)
            {
                const pid_t done = ::waitpid(pid, &status, WNOHANG);
                if(done == pid)
                    break;
                if(done == -1 && errno != EINTR)
                    break;
                if(!terminated && cancelled())
                {
                    terminate();
                    terminated = true;
                }
                ::usleep(10000);
            }
        }
        else
        {
            while(::waitpid(pid, &status, 0) == -1)
            {
                if(errno != EINTR)
                    break;
            }
        }
        pid = -1;

        if(WIFEXITED(status))
            return WEXITSTATUS(status);
        return -1;
    }
#endif

    void moveFrom(ProcessPipe& other) noexcept
    {
        file = other.file;
        other.file = nullptr;
        commandName = std::move(other.commandName);
        commandLine = std::move(other.commandLine);
        usedStdio = other.usedStdio;
#ifndef _WIN32
        pid = other.pid;
        other.pid = -1;
#endif
    }

    FILE* file = nullptr;
    std::string commandName;
    std::string commandLine;
    bool usedStdio = false;
#ifndef _WIN32
    pid_t pid = -1;
#endif
};
