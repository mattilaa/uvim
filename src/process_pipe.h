#pragma once

#include "os_compat.h"
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

class ProcessPipe
{
public:
    ProcessPipe() = default;

    ProcessPipe(const std::string& cmd, const char* mode)
    {
        open(cmd, mode);
    }

    ProcessPipe(const ProcessPipe&) = delete;
    ProcessPipe& operator=(const ProcessPipe&) = delete;

    ProcessPipe(ProcessPipe&&) noexcept = default;
    ProcessPipe& operator=(ProcessPipe&&) noexcept = default;

    bool open(const std::string& cmd, const char* mode)
    {
        pipe.reset(popen(cmd.c_str(), mode));
        return isOpen();
    }

    bool isOpen() const
    {
        return pipe != nullptr;
    }

    explicit operator bool() const
    {
        return isOpen();
    }

    FILE* get() const
    {
        return pipe.get();
    }

    int close()
    {
        if(!pipe)
            return -1;
        FILE* raw = pipe.release();
        return pclose(raw);
    }

    std::string readAll(size_t bufferSize = 4096)
    {
        std::string output;
        if(!pipe)
            return output;

        std::string buffer(bufferSize, '\0');
        while(true)
        {
            size_t n = fread(buffer.data(), 1, buffer.size(), pipe.get());
            if(n > 0)
                output.append(buffer.data(), n);
            if(n < buffer.size())
                break;
        }
        return output;
    }

    bool write(std::string_view data)
    {
        if(!pipe)
            return false;
        return fwrite(data.data(), 1, data.size(), pipe.get()) == data.size();
    }

    bool flush()
    {
        return pipe && fflush(pipe.get()) == 0;
    }

private:
    struct PipeCloser
    {
        void operator()(FILE* file) const
        {
            if(file)
                pclose(file);
        }
    };

    std::unique_ptr<FILE, PipeCloser> pipe;
};
