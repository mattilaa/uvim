#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace diagnostics_common
{
std::string shellQuote(const std::string& text);
std::string resolveExecutablePath(const std::string& exe);
std::filesystem::path makeTempPath(const std::string& stem,
                                   const std::string& extension);
std::string relativeDisplayPath(const std::filesystem::path& path,
                                const std::filesystem::path& root);
std::string normalizedPathString(const std::filesystem::path& path);
std::vector<std::string> readLines(const std::filesystem::path& path);
} // namespace diagnostics_common
