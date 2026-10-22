#pragma once

#include <string>
#include <string_view>
#include <vector>

// Shared by :emitasm and the assembly browser.
namespace assembly_output
{
std::string sourceText(const std::vector<std::string>& lines, bool cSource);
// When requested, each displayed row records its original assembly row.
// Blank separators use -1.
std::string compact(std::string_view output,
                    std::vector<int>* originalRows = nullptr);
} // namespace assembly_output
