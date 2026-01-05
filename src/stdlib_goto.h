#pragma once

#include <string>

namespace stdlib_goto
{

// Returns header name (without <>), or empty string if unknown.
std::string headerForSymbol(const std::string& symbol);

} // namespace stdlib_goto
