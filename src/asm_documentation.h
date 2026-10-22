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

struct Documentation
{
    std::string title;
    std::string text;
};

// Built-in instruction help; no filesystem or network access.
std::optional<Documentation> embedded(std::string_view line);
std::optional<Location> find(std::string_view line, int cursorX);
std::optional<Location> find(std::string_view line, int cursorX,
                             bool fetchOriginal);
void setFetchOriginalDocs(bool enabled);
bool fetchOriginalDocs();
}
