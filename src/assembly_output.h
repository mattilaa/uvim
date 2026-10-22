#pragma once

#include <string>
#include <string_view>
#include <vector>

// Shared by :emitasm and the assembly browser.
namespace assembly_output
{
std::string sourceText(const std::vector<std::string>& lines, bool cSource);
std::string compact(std::string_view output);
} // namespace assembly_output
