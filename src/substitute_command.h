#pragma once

#include <string_view>

namespace command::execution
{
// Recognizes substitution input, including unfinished expressions, so prompt
// completion never rewrites a pattern or replacement while it is being typed.
bool isSubstituteCommand(std::string_view input);
} // namespace command::execution
