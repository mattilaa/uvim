#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

// Data and ranking for the command palette (Space Space, :palette): every
// ex command with its description, searchable by name and description.
namespace command_palette
{
struct Entry
{
    // Equivalent spellings, e.g. {"q", "quit"}; the first one is run.
    std::vector<std::string> names;
    std::string description;
    // The command needs an argument (":e <file>"): choosing it opens the
    // command line with the command typed instead of running it.
    bool takesArgument = false;

    // ":q, :quit" / ":e …"
    std::string label() const;
};

struct Match
{
    int entry = 0;
    // Byte offsets in Entry::label() to highlight.
    std::vector<int> positions;
};

using Describe = std::function<std::string(std::string_view command)>;

// Groups `commands` (the ex command list; a trailing space marks a command
// that takes an argument) into entries. Commands with the same description
// are aliases of one entry, in first-seen order. `describe` gets each
// command as listed, trailing space included.
std::vector<Entry> buildEntries(const std::vector<std::string>& commands,
                                const Describe& describe);

// Entries matching `query`, best first: exact name, name prefix, fuzzy
// name, all query words in the description, fuzzy description. An empty
// query keeps every entry in list order.
std::vector<Match> rank(const std::vector<Entry>& entries,
                        const std::string& query);
} // namespace command_palette
