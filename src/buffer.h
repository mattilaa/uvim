#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

// Buffer structure to hold file data (extracted from Editor class)
struct Buffer
{
    std::vector<std::string> lines;
    std::string filename;
    bool dirty = false;
    std::filesystem::file_time_type lastModificationTime{};

    // Cursor position per buffer
    int cursorX = 0;
    int cursorY = 0;
    int wantedX = 0;

    // View position per buffer
    int offsetX = 0;
    int offsetY = 0;

    // Marks per buffer
    std::unordered_map<char, std::pair<int, int>> marks;

    // Undo/redo stack per buffer
    struct EditState
    {
        std::vector<std::string> lines;
        int cursorX, cursorY;
    };
    std::vector<EditState> undoStack;
    int undoIndex = -1;
    int savedUndoIndex = -1; // Track which undo state was last saved

    // Search state per buffer
    std::string lastSearchQuery;
    bool lastSearchForward = true;

    // Visual mode state per buffer
    int visualStartX = 0;
    int visualStartY = 0;
    int visualEndX = 0;
    int visualEndY = 0;

    // Visual block mode state per buffer
    int visualBlockStartX = 0;
    int visualBlockStartY = 0;
    int visualBlockEndX = 0;
    int visualBlockEndY = 0;
    std::string visualBlockInsertText; // Text to insert in visual block mode

    Buffer()
    {
        lines.push_back("");
    }
};
