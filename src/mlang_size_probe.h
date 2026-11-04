#pragma once

#include "debug_type_layout.h"

std::optional<DebugTypeLayout> probeMlangSize(
    const std::vector<std::string>& lines,
    const std::string& path,
    const std::string& expression,
    int cursorLine,
    std::string& error,
    bool functionReturn = false,
    int cursorColumn = 0);
