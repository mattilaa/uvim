#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace assembly_compiler
{
struct Request
{
    std::string path = ".";
    std::string compiler;
    std::string directory;
    std::vector<std::string> flags;
    std::optional<std::vector<std::string>> sourceLines;
    bool raw = false;
};

struct Result
{
    bool success = false;
    std::string compiler;
    std::string options;
    std::string diagnostics;
    std::vector<std::string> lines;
};

bool parse(std::string_view arguments, Request& request, std::string& error);
Result compile(const Request& request, const std::function<bool()>& cancelled);
} // namespace assembly_compiler
