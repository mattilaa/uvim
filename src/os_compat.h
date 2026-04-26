#pragma once

// Small cross-platform shim for things the rest of the codebase uses
// straight from POSIX. Keeps individual translation units free of
// repetitive #ifdef _WIN32 blocks.

#include <cstdio>

#ifdef _WIN32
// MSVC's CRT names these with a leading underscore. Map back to the POSIX
// names so existing call sites compile unchanged.
#define popen _popen
#define pclose _pclose
#else
#include <unistd.h>
#endif
