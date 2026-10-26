#pragma once

// Small cross-platform shim for things the rest of the codebase uses
// straight from POSIX. Keeps individual translation units free of
// repetitive #ifdef _WIN32 blocks.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
// MSVC's CRT names these with a leading underscore. Map back to the POSIX
// names so existing call sites compile unchanged.
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <random>
#include <stdlib.h>
#include <sys/stat.h>
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

// Creates an empty, uniquely named file in the system temp directory
// (`<stem>-XXXXXX`, like mkstemp) and returns its path, or "" on failure.
// The caller removes the file.
inline std::string os_make_temp_file(const std::string& stem)
{
    std::error_code ec;
    const std::string base =
        (std::filesystem::temp_directory_path(ec) / (stem + "-")).string();
#ifdef _WIN32
    static const char kChars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> pick(0, sizeof(kChars) - 2);
    for(int attempt = 0; attempt < 100; ++attempt)
    {
        std::string path = base;
        for(int i = 0; i < 6; ++i)
            path += kChars[pick(rng)];
        int fd = -1;
        if(_sopen_s(
               &fd,
               path.c_str(),
               _O_CREAT | _O_EXCL | _O_RDWR,
               _SH_DENYNO,
               _S_IREAD | _S_IWRITE) == 0)
        {
            _close(fd);
            return path;
        }
    }
    return {};
#else
    std::vector<char> name(base.begin(), base.end());
    for(int i = 0; i < 6; ++i)
        name.push_back('X');
    name.push_back('\0');
    const int fd = ::mkstemp(name.data());
    if(fd < 0)
        return {};
    ::close(fd);
    return name.data();
#endif
}
