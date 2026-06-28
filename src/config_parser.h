#pragma once

#include <string>
#include <unordered_map>

namespace editor::config
{

/// @brief Parses a simple TOML mapping into flattened dotted keys.
/// @param input TOML text.
/// @return Key-value map of parsed entries.
std::unordered_map<std::string, std::string>
parseTomlMap(const std::string& input);

} // namespace editor::config
