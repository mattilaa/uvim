#include "asm_documentation.h"

namespace asm_documentation
{
std::optional<Documentation> embedded(std::string_view)
{
    return std::nullopt;
}

std::optional<Location> find(std::string_view, int)
{
    return std::nullopt;
}

std::optional<Location> find(std::string_view, int, bool)
{
    return std::nullopt;
}

void setFetchOriginalDocs(bool) {}

bool fetchOriginalDocs()
{
    return false;
}
} // namespace asm_documentation
