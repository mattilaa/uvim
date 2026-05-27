#include "asm_documentation.h"

namespace asm_documentation
{
std::optional<Location> find(std::string_view, int)
{
    return std::nullopt;
}

void setFetchOriginalDocs(bool) {}

bool fetchOriginalDocs()
{
    return false;
}
} // namespace asm_documentation
