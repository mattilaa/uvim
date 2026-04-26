#pragma once

// Small cross-platform shim for things the rest of the codebase uses
// straight from POSIX. Keeps individual translation units free of
// repetitive #ifdef _WIN32 blocks.

#include <cstdio>

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
