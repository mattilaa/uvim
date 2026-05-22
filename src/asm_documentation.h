#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace asm_documentation
{
struct Location
{
    std::string path;
    int line = 0;
    std::string mnemonic;
    std::string arch;
};

std::optional<Location> find(std::string_view line, int cursorX);
void setFetchOriginalDocs(bool enabled);
bool fetchOriginalDocs();
}
