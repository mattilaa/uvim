#pragma once

#include <optional>
#include <string>
#include <vector>

struct CursorCall
{
    std::string callee;
    std::string expression;
    int startLine = 0, startColumn = 0;
    int endLine = 0, endColumn = 0;
};

std::optional<CursorCall> cursorCallAt(
    const std::vector<std::string>& lines,
    int row,
    int column,
    const std::string& callee);
