#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace assembly_compiler
{
struct Request
{
    std::string path = ".";
    std::string compiler;
    std::string directory;
    std::string compileCommandsDirectory;
    std::vector<std::string> flags;
    std::optional<std::vector<std::string>> sourceLines;
    // Inclusive one-based source lines; compile the full file, filter output.
    std::optional<std::pair<int, int>> sourceRange;
    bool raw = false;
    bool split = false;
};

struct SourceOrigin
{
    std::string path;
    int line = 0;
    bool mainSource = false;
};

struct Result
{
    bool success = false;
    std::string compiler;
    std::string architecture;
    std::string options;
    std::string diagnostics;
    std::string compilationDatabase;
    std::vector<std::string> lines;
    std::string sourcePath;
    std::vector<std::string> sourceLines;
    // One-based source line per assembly row; 0 means no source correspondence.
    std::vector<int> sourceLineForAssembly;
    // Includes label origins inferred from their first source-mapped
    // instruction.
    std::vector<SourceOrigin> sourceOrigins;
};

bool isMlangSource(std::string_view path);
bool parse(std::string_view arguments, Request& request, std::string& error);
Result compile(const Request& request, const std::function<bool()>& cancelled);
} // namespace assembly_compiler
