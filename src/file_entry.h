#pragma once

#include <ctime>
#include <string>

// File entry for file browser (extracted from Editor class)
struct FileEntry
{
    std::string name;
    std::string path;
    bool isDirectory;
    size_t size;
    time_t modTime;
};
