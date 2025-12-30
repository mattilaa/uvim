#pragma once
#include <chrono>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>

namespace mla::log
{

enum class LogLevel
{
    INFO,
    WARNING,
    ERROR,
    DEBUG
};

static std::ostream& operator<<(std::ostream& oss, LogLevel level)
{
    switch(level)
    {
        case LogLevel::INFO:
            return oss << "INFO";
        case LogLevel::WARNING:
            return oss << "WARNING";
        case LogLevel::ERROR:
            return oss << "ERROR";
        case LogLevel::DEBUG:
            return oss << "DEBUG";
    }
    return oss; // Because of some stupid compilers
}

static std::string toColor(LogLevel level)
{
    switch(level)
    {
        case LogLevel::INFO:
            return "\x1b[32;1m";
        case LogLevel::WARNING:
            return "\x1b[33;1m";
        case LogLevel::ERROR:
            return "\x1b[31;1m";
        case LogLevel::DEBUG:
            return "\x1b[36;1m";
    }
    return ""; // Because of some stupid compilers
}

// Global log file path - defaults to /tmp/uvim_debug.log
inline std::string& getLogFilePath()
{
    static std::string path = "/tmp/uvim_debug.log";
    return path;
}

inline void setLogFilePath(const std::string& path)
{
    getLogFilePath() = path;
}

class FileLogger
{
public:
    FileLogger(std::string_view ctx = "")
        : ctx(ctx)
    {
    }

    FileLogger(const FileLogger& log, std::string_view new_ctx)
        : ctx(log.ctx.empty() ? std::string(new_ctx)
                              : log.ctx + "/" + std::string(new_ctx))
    {
    }

    static std::string timestamp()
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::floor<std::chrono::microseconds>(now);
        auto ms = std::chrono::duration_cast<std::chrono::microseconds>(
                      time.time_since_epoch())
                      .count() %
                  1000000;

        std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
#ifdef _WIN32
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif

        return std::format("{:02d}:{:02d}:{:02d}.{:06d}",
                          tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    }

    void log(LogLevel level, std::string_view msg)
    {
        std::string levelStr =
            static_cast<int>(level) == 0 ? "INFO" :
            static_cast<int>(level) == 1 ? "WARNING" :
            static_cast<int>(level) == 2 ? "ERROR" : "DEBUG";

        std::string formatted = std::format("{} {} {}{}\n",
            levelStr,
            timestamp(),
            ctx.empty() ? "" : ctx + " ",
            msg);

        static std::mutex log_mutex;
        std::lock_guard<std::mutex> lock(log_mutex);

        std::ofstream file(getLogFilePath(), std::ios::app);
        if(file.is_open())
        {
            file << formatted;
        }
    }

private:
    std::string ctx;
};

// Alias for backward compatibility
using StdLogger = FileLogger;

}  // namespace mla::log

#define LOG_INFO(logger, ...)                                \
    do                                                       \
    {                                                        \
        logger.log(mla::log::LogLevel::INFO, std::format(__VA_ARGS__)); \
    } while(0)

#define LOG_DEBUG(logger, ...)                               \
    do                                                       \
    {                                                        \
        logger.log(mla::log::LogLevel::DEBUG, std::format(__VA_ARGS__)); \
    } while(0)

#define LOG_WARNING(logger, ...)                             \
    do                                                       \
    {                                                        \
        logger.log(mla::log::LogLevel::WARNING, std::format(__VA_ARGS__)); \
    } while(0)

#define LOG_ERROR(logger, ...)                               \
    do                                                       \
    {                                                        \
        logger.log(mla::log::LogLevel::ERROR, std::format(__VA_ARGS__)); \
    } while(0)
