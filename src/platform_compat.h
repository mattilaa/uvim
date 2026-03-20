#pragma once

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#ifndef popen
#define popen _popen
#endif
#ifndef pclose
#define pclose _pclose
#endif
#else
#include <unistd.h>
#endif

namespace platform
{
inline std::string current_path_string()
{
    std::error_code ec;
    std::filesystem::path path = std::filesystem::current_path(ec);
    if(ec)
        return {};
    return path.string();
}

inline bool set_current_path(const std::string& path)
{
    std::error_code ec;
    std::filesystem::current_path(path, ec);
    return !ec;
}

inline bool path_exists(const std::string& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

inline bool is_directory(const std::string& path)
{
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

inline long process_id()
{
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

inline void remove_file(const std::string& path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

inline bool set_env(const char* key, const std::string& value)
{
#ifdef _WIN32
    return _putenv_s(key, value.c_str()) == 0;
#else
    return setenv(key, value.c_str(), 1) == 0;
#endif
}

inline char path_list_separator()
{
#ifdef _WIN32
    return ';';
#else
    return ':';
#endif
}

inline bool local_time(std::time_t value, std::tm& out)
{
#ifdef _WIN32
    return localtime_s(&out, &value) == 0;
#else
    return localtime_r(&value, &out) != nullptr;
#endif
}

inline int wcwidth_compat(wchar_t wc)
{
#ifdef _WIN32
    if(wc == 0)
        return 0;
    if((wc >= 0 && wc < 32) || (wc >= 0x7f && wc < 0xa0))
        return -1;
    return 1;
#else
    return ::wcwidth(wc);
#endif
}

inline std::time_t to_time_t(std::filesystem::file_time_type value)
{
    using namespace std::chrono;
    const auto systemNow = system_clock::now();
    const auto fileNow = std::filesystem::file_time_type::clock::now();
    const auto converted =
        time_point_cast<system_clock::duration>(value - fileNow + systemNow);
    return system_clock::to_time_t(converted);
}
} // namespace platform
