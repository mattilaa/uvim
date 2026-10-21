#pragma once

#include "file_type.h"
#include <string>
#include <vector>

class Editor;

namespace structural_navigation
{
enum class Target
{
    Function,
    Local
};

struct Position
{
    int line = 0;
    int column = 0;

    bool operator<(const Position& other) const
    {
        return line < other.line ||
               (line == other.line && column < other.column);
    }

    bool operator==(const Position& other) const
    {
        return line == other.line && column == other.column;
    }
};

struct FunctionSymbol
{
    Position position;
    std::string signature;
};

bool supports(FileType type);
FileType fileType(const Editor& editor);
std::vector<FunctionSymbol>
functionSymbols(const std::vector<std::string>& lines, FileType type);
// A lexical outline, independent of LSP and the filesystem. Local targets are
// declarations and calls in the innermost function containing the cursor.
std::vector<Position> targets(const std::vector<std::string>& lines,
                              FileType type, Target target, Position cursor);
bool navigate(Editor& editor, Target target, bool forward, int count = 1);
} // namespace structural_navigation
