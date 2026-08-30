#pragma once
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>

#include <fmt/format.h>

namespace mla::log
{

enum class LogLevel
{
    Info,
    Warning,
    Error,
    Debug
};

inline std::ostream& operator<<(std::ostream& oss, LogLevel level)
{
    switch(level)
    {
    case LogLevel::Info:
        return oss << "INFO";
    case LogLevel::Warning:
        return oss << "WARNING";
    case LogLevel::Error:
        return oss << "ERROR";
    case LogLevel::Debug:
        return oss << "DEBUG";
    }
    return oss; // Because of some stupid compilers
}

static std::string toColor(LogLevel level)
{
    switch(level)
    {
    case LogLevel::Info:
        return "\x1b[32;1m";
    case LogLevel::Warning:
        return "\x1b[33;1m";
    case LogLevel::Error:
        return "\x1b[31;1m";
    case LogLevel::Debug:
        return "\x1b[36;1m";
    }
    return ""; // Because of some stupid compilers
}

static constexpr const char* resetColor()
{
    return "\x1b[0m";
}

inline std::string defaultLogFilePath()
{
#ifdef _WIN32
    const char* userProfile = std::getenv("USERPROFILE");
    const char* userName = std::getenv("USERNAME");
    std::filesystem::path base =
        userProfile ? std::filesystem::path(userProfile)
                    : std::filesystem::path("/Users") /
                          (userName ? userName : "Default");
    return (base / "Documents" / "uvim" / "uvim.log").string();
#else
    return "/tmp/uvim.log";
#endif
}

// Global log file path. Logging is compiled in only for UVIM_DEBUG_LOGGING or
// UVIM_DEBUG_LSP builds.
inline std::string& getLogFilePath()
{
    static std::string path = defaultLogFilePath();
    return path;
}

inline void setLogFilePath(const std::string& path)
{
    getLogFilePath() = path;
}

// Global color flag - defaults to false for file output
inline bool& getUseColors()
{
    static bool use_colors = false;
    return use_colors;
}

inline void setUseColors(bool use_colors)
{
    getUseColors() = use_colors;
}

class FileLogger
{
public:
    FileLogger(std::string_view ctx = "") : ctx(ctx) {}

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

        return fmt::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:06d}",
                           tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                           tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    }

    void log(LogLevel level, std::string_view msg)
    {
        bool use_color = getUseColors();

        const char* levelStr = level == LogLevel::Info      ? "INFO"
                               : level == LogLevel::Warning ? "WARNING"
                               : level == LogLevel::Error   ? "ERROR"
                                                            : "DEBUG";

        const std::string signature =
            ctx.empty() ? "" : fmt::format("[{}] ", ctx);

        std::string formatted = fmt::format(
            "{}{}{} {} {}{}\n", use_color ? toColor(level) : "", levelStr,
            use_color ? resetColor() : "", timestamp(), signature, msg);

        static std::mutex log_mutex;
        std::lock_guard<std::mutex> lock(log_mutex);

        const std::filesystem::path path(getLogFilePath());
        if(path.has_parent_path())
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
        }

        std::ofstream file(path, std::ios::app);
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

} // namespace mla::log

#if defined(UVIM_DEBUG_LOGGING) || defined(UVIM_DEBUG_LSP)

#define LOG_INFO(logger, ...)                                                  \
    do                                                                         \
    {                                                                          \
        logger.log(mla::log::LogLevel::Info, fmt::format(__VA_ARGS__));        \
    } while(0)

#define LOG_DEBUG(logger, ...)                                                 \
    do                                                                         \
    {                                                                          \
        logger.log(mla::log::LogLevel::Debug, fmt::format(__VA_ARGS__));       \
    } while(0)

#define LOG_WARNING(logger, ...)                                               \
    do                                                                         \
    {                                                                          \
        logger.log(mla::log::LogLevel::Warning, fmt::format(__VA_ARGS__));     \
    } while(0)

#define LOG_ERROR(logger, ...)                                                 \
    do                                                                         \
    {                                                                          \
        logger.log(mla::log::LogLevel::Error, fmt::format(__VA_ARGS__));       \
    } while(0)

#else

#define LOG_INFO(logger, ...)                                                  \
    do                                                                         \
    {                                                                          \
    } while(0)
#define LOG_DEBUG(logger, ...)                                                 \
    do                                                                         \
    {                                                                          \
    } while(0)
#define LOG_WARNING(logger, ...)                                               \
    do                                                                         \
    {                                                                          \
    } while(0)
#define LOG_ERROR(logger, ...)                                                 \
    do                                                                         \
    {                                                                          \
    } while(0)

#endif
