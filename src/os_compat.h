#pragma once

// Small cross-platform shim for things the rest of the codebase uses
// straight from POSIX. Keeps individual translation units free of
// repetitive #ifdef _WIN32 blocks.

#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
// MSVC's CRT names these with a leading underscore. Map back to the POSIX
// names so existing call sites compile unchanged.
#include <process.h>
#include <stdlib.h>
#define popen _popen
#define pclose _pclose
#define getpid _getpid

// POSIX setenv shim. _putenv_s always overwrites, so the `overwrite` flag
// is intentionally ignored — matches our existing call sites which all
// pass 1 anyway.
inline int setenv(const char* name, const char* value, int /*overwrite*/)
{
    return _putenv_s(name, value);
}
#else
#include <unistd.h>
#endif

namespace os_compat
{
inline char path_list_separator()
{
#ifdef _WIN32
    return ';';
#else
    return ':';
#endif
}

inline std::vector<std::string> executable_extensions()
{
#ifdef _WIN32
    std::vector<std::string> out;
    if(const char* pathext = std::getenv("PATHEXT"))
    {
        std::string_view value(pathext);
        size_t start = 0;
        while(start <= value.size())
        {
            size_t end = value.find(';', start);
            if(end == std::string_view::npos)
                end = value.size();
            if(end > start)
            {
                std::string ext(value.substr(start, end - start));
                for(char& ch : ext)
                    ch = static_cast<char>(std::tolower((unsigned char)ch));
                out.push_back(ext);
            }
            if(end == value.size())
                break;
            start = end + 1;
        }
    }
    if(out.empty())
        out = {".exe", ".cmd", ".bat", ".ps1"};
    return out;
#else
    return {""};
#endif
}

inline bool has_executable_extension(const std::filesystem::path& path)
{
#ifdef _WIN32
    std::string ext = path.extension().string();
    for(char& ch : ext)
        ch = static_cast<char>(std::tolower((unsigned char)ch));
    for(const auto& candidate : executable_extensions())
    {
        if(ext == candidate)
            return true;
    }
    return false;
#else
    (void)path;
    return true;
#endif
}

inline std::string find_executable(std::string_view exe)
{
    if(exe.empty())
        return "";

    std::filesystem::path exePath{std::string(exe)};
    std::error_code ec;
    auto check = [&](const std::filesystem::path& candidate) -> std::string
    {
        if(std::filesystem::exists(candidate, ec) &&
           std::filesystem::is_regular_file(candidate, ec))
        {
            return candidate.string();
        }
        return "";
    };

    if(exePath.has_parent_path())
    {
        if(auto found = check(exePath); !found.empty())
            return found;
#ifdef _WIN32
        if(!has_executable_extension(exePath))
        {
            for(const auto& ext : executable_extensions())
            {
                if(auto found = check(std::filesystem::path(exePath.string() + ext));
                   !found.empty())
                    return found;
            }
        }
#endif
        return "";
    }

    const char* path = std::getenv("PATH");
    if(!path || !*path)
        return "";

    std::string_view pathView(path);
    size_t start = 0;
    while(start <= pathView.size())
    {
        size_t end = pathView.find(path_list_separator(), start);
        if(end == std::string_view::npos)
            end = pathView.size();
        if(end > start)
        {
            std::filesystem::path dir{
                std::string(pathView.substr(start, end - start))};
            std::filesystem::path candidate = dir / exePath;
            if(auto found = check(candidate); !found.empty())
                return found;
#ifdef _WIN32
            if(!has_executable_extension(candidate))
            {
                for(const auto& ext : executable_extensions())
                {
                    if(auto found = check(
                           std::filesystem::path(candidate.string() + ext));
                       !found.empty())
                        return found;
                }
            }
#endif
        }
        if(end == pathView.size())
            break;
        start = end + 1;
    }
    return "";
}

inline std::string shell_quote(std::string_view value)
{
#ifdef _WIN32
    std::string out = "'";
    for(char ch : value)
    {
        if(ch == '\'')
            out += "''";
        else
            out += ch;
    }
    out += "'";
    return out;
#else
    std::string out = "'";
    for(char ch : value)
    {
        if(ch == '\'')
            out += "'\\''";
        else
            out += ch;
    }
    out += "'";
    return out;
#endif
}

inline std::string popen_quote(std::string_view value)
{
#ifdef _WIN32
    std::string out = "\"";
    for(char ch : value)
    {
        if(ch == '"')
            out += "\\\"";
        else
            out += ch;
    }
    out += "\"";
    return out;
#else
    return shell_quote(value);
#endif
}

inline std::string discard_stderr()
{
#ifdef _WIN32
    return "2>$null";
#else
    return "2>/dev/null";
#endif
}

inline std::string popen_discard_stderr()
{
#ifdef _WIN32
    return "2>NUL";
#else
    return "2>/dev/null";
#endif
}
} // namespace os_compat
