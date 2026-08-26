#pragma once

#include "file_entry.h"
#include <string>
#include <utility>
#include <vector>

// Fuzzy finder match (extracted from Editor class)
struct FuzzyMatch
{
    FileEntry file;
    int score = 0;
    double quality = 0.0;
    std::vector<int> matchPositions; // Character positions that matched
};

// Buffer browser match (extracted from Editor class)
struct BufferMatch
{
    int bufferIndex = -1;
    int score = 0;
    std::string display;
    std::vector<int> matchPositions; // positions within display string
};

// Grep search match (extracted from Editor class)
struct GrepMatch
{
    std::string filename;
    std::string filepath;
    int lineNumber;
    std::string lineContent;
    std::vector<std::pair<int, int>> highlightRanges; // Start and end positions
};

// Search match position (extracted from Editor class)
struct SearchMatch
{
    int row;
    int col;
    int len;
};
