#pragma once

#include "log.h"
#include "text_utils.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

class ProcessPipe
{
public:
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

            std::vector<char*> argv;
            argv.reserve(args.size() + 1);
            for(const auto& arg : args)
                argv.push_back(const_cast<char*>(arg.c_str()));
            argv.push_back(nullptr);

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
        const int status = waitForChild();
        logExitFailure(status);
        return result == 0 ? status : result;
#else
        return result;
#endif
    }

private:
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
        if(arg.empty() || text_utils::is_found(arg.find_first_of(" \t\"")))
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
    int waitForChild()
    {
        if(pid <= 0)
            return -1;

        int status = 0;
        while(::waitpid(pid, &status, 0) == -1)
        {
            if(errno != EINTR)
                break;
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
#ifndef _WIN32
        pid = other.pid;
        other.pid = -1;
#endif
    }

    FILE* file = nullptr;
    std::string commandName;
    std::string commandLine;
#ifndef _WIN32
    pid_t pid = -1;
#endif
};
