#pragma once

#include <filesystem>
#include <optional>

namespace project_lsp
{
struct Configuration
{
    std::filesystem::path root;
    std::filesystem::path commandsDirectory;
};

std::optional<Configuration>
findCompileCommandsForFile(const std::filesystem::path& file);

std::optional<Configuration>
findMlangCommandsForFile(const std::filesystem::path& file);
} // namespace project_lsp
