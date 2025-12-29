#pragma once
#include <string_view>

namespace cpp_constants
{
// true if identifier is in the keyword list
bool is_keyword(std::string_view s) noexcept;

// true if identifier is in the type list
bool is_type(std::string_view s) noexcept;

// operator/punct char classification used by tokenizer
bool is_operator_char(char c) noexcept;
} // namespace cpp_constants
