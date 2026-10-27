#pragma once

#include <ctime>
#include <string>

// File entry for file browser (extracted from Editor class)
struct FileEntry
{
    enum class GitChange
    {
        None,
        Unstaged,
        Staged,
    };

    std::string name;
    std::string path;
    bool isDirectory;
    GitChange gitChange = GitChange::None;
    size_t size = 0;
    time_t modTime = 0;
    bool metadataLoaded = false;
};
