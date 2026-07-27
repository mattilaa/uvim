#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

namespace executable_lookup
{
struct Result
{
    bool found = false;
    std::string path;
};

Result find(std::string_view name);
Result findAny(std::initializer_list<std::string_view> names);
} // namespace executable_lookup
