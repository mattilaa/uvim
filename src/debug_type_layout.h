#pragma once

#include <optional>
#include <string>
#include <vector>

struct DebugTypeMember
{
    std::string name;
    long long sizeBits = 0;
    long long offsetBits = 0;
    std::vector<DebugTypeMember> children;
    bool complete = false;
};

struct DebugTypeLayout
{
    long long sizeBits = 0;
    std::vector<DebugTypeMember> members;
    bool complete = false;
    std::string typeName;
};

// Read the compiler's debug type metadata, without executing the source.
std::optional<DebugTypeLayout> parseDebugTypeMetadata(
    const std::string& ir,
    const std::string& expression,
    int cursorLine);

std::optional<DebugTypeLayout> parseDebugFunctionPointerReturn(
    const std::string& ir,
    const std::string& variable);

std::optional<DebugTypeLayout> parseDebugCallReturn(
    const std::string& ir,
    const std::string& callee,
    int cursorLine,
    int cursorColumn = 0);
