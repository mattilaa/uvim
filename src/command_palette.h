#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

// Data and ranking for the command palette (Space Space, :palette): what
// uVim can do, by name ("Open file browser", "Change buffer"), with the key
// or command that does it. Choosing an entry runs that key or command.
namespace command_palette
{
struct Entry
{
    // What it does, e.g. "Open file browser". Searched first.
    std::string title;
    // How to do it without the palette, shown dimmed: "Space x", ":w".
    std::string shortcut;
    // Ex command spellings, e.g. {"wa", "wall"}; also searchable. When
    // `keys` is empty the first one is run.
    std::vector<std::string> names;
    // Normal mode keys to replay, e.g. {' ', 'x'} for Space x.
    std::vector<int> keys;
    // The command needs an argument (":e <file>"): choosing it opens the
    // command line with the command typed instead of running it.
    bool takesArgument = false;
};

struct Match
{
    int entry = 0;
    // Byte offsets in Entry::title to highlight.
    std::vector<int> positions;
};

using Describe = std::function<std::string(std::string_view command)>;

// The named actions of this build (features compiled out are left out).
std::vector<Entry> builtinActions();

// Adds the ex commands from `commands` (the completion list; a trailing
// space marks a command taking an argument) that no entry covers yet,
// titled by their description. Commands sharing a description become one
// entry. `describe` gets each command as listed.
void addExCommands(
    std::vector<Entry>& entries,
    const std::vector<std::string>& commands,
    const Describe& describe);

// Entries matching `query`, best first: exact name, title or name prefix,
// title word prefixes, title words, fuzzy. Empty query: list order.
std::vector<Match> rank(
    const std::vector<Entry>& entries,
    const std::string& query);
} // namespace command_palette
