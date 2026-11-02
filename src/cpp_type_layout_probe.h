#pragma once

#include "debug_type_layout.h"

std::optional<DebugTypeLayout> probeCppTypeLayout(
    const std::vector<std::string>& lines,
    const std::string& path,
    const std::string& expression,
    int cursorLine,
    const std::string& typeExpression);
