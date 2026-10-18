#include "command_palette.h"

#include "editor_utils.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace command_palette
{
namespace
{
std::string lower(std::string_view text)
{
    std::string out(text);
    for(char& ch : out)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

std::string trimRight(std::string text)
{
    while(!text.empty() && text.back() == ' ')
        text.pop_back();
    return text;
}

bool containsAllWords(const std::string& haystack, const std::string& query)
{
    std::istringstream words(query);
    std::string word;
    bool any = false;
    while(words >> word)
    {
        any = true;
        if(haystack.find(word) == std::string::npos)
            return false;
    }
    return any;
}

// Lower tier = better match.
enum Tier
{
    ExactName = 0,
    NamePrefix,
    FuzzyName,
    DescriptionWords,
    FuzzyDescription,
    NoMatch
};
} // namespace

std::string Entry::label() const
{
    std::string out;
    for(size_t i = 0; i < names.size(); ++i)
    {
        if(i > 0)
            out += ", ";
        out += ":" + names[i];
    }
    if(takesArgument)
        out += " \xE2\x80\xA6"; // …
    return out;
}

std::vector<Entry> buildEntries(const std::vector<std::string>& commands,
                                const Describe& describe)
{
    std::vector<Entry> entries;
    for(const std::string& raw : commands)
    {
        const bool takesArgument = !raw.empty() && raw.back() == ' ';
        const std::string name = trimRight(raw);
        if(name.empty())
            continue;
        const std::string description = describe ? describe(raw) : "";

        auto same = std::find_if(
            entries.begin(), entries.end(),
            [&](const Entry& entry)
            {
                if(entry.takesArgument != takesArgument)
                    return false;
                if(std::find(entry.names.begin(), entry.names.end(), name) !=
                   entry.names.end())
                    return true;
                return !description.empty() &&
                       entry.description == description;
            });
        if(same == entries.end())
        {
            entries.push_back({{name}, description, takesArgument});
        }
        else if(std::find(same->names.begin(), same->names.end(), name) ==
                same->names.end())
        {
            same->names.push_back(name);
        }
    }
    return entries;
}

std::vector<Match> rank(const std::vector<Entry>& entries,
                        const std::string& query)
{
    std::vector<Match> out;
    const std::string q = lower(query);
    if(q.find_first_not_of(' ') == std::string::npos)
    {
        for(int i = 0; i < (int)entries.size(); ++i)
            out.push_back({i, {}});
        return out;
    }

    struct Scored
    {
        int entry;
        Tier tier;
        int score;
    };
    std::vector<Scored> scored;
    std::vector<int> positions;
    for(int i = 0; i < (int)entries.size(); ++i)
    {
        const Entry& entry = entries[i];
        Tier tier = NoMatch;
        int score = 0;
        for(const std::string& name : entry.names)
        {
            const std::string n = lower(name);
            if(n == q)
            {
                tier = ExactName;
                break;
            }
            if(n.rfind(q, 0) == 0)
            {
                // Shorter names first: "w" before "wall" for "w".
                const int s = 1000 - (int)n.size();
                if(tier > NamePrefix || score < s)
                {
                    tier = NamePrefix;
                    score = s;
                }
                continue;
            }
            const int s = editor::helper::fuzzyScoreWithPositions(q, n, positions);
            if(s >= 0 && (tier > FuzzyName || (tier == FuzzyName && s > score)))
            {
                tier = FuzzyName;
                score = s;
            }
        }
        if(tier == NoMatch && !entry.description.empty())
        {
            const std::string d = lower(entry.description);
            if(containsAllWords(d, q))
            {
                tier = DescriptionWords;
                // Earlier mentions rank higher ("buffer" in "Next buffer").
                score = 1000 - (int)d.find(q.substr(0, q.find(' ')));
            }
            else
            {
                const int s =
                    editor::helper::fuzzyScoreWithPositions(q, d, positions);
                if(s >= 0)
                {
                    tier = FuzzyDescription;
                    score = s;
                }
            }
        }
        if(tier != NoMatch)
            scored.push_back({i, tier, score});
    }

    std::stable_sort(scored.begin(), scored.end(),
                     [](const Scored& a, const Scored& b)
                     {
                         if(a.tier != b.tier)
                             return a.tier < b.tier;
                         return a.score > b.score;
                     });

    for(const Scored& s : scored)
    {
        Match match{s.entry, {}};
        if(s.tier <= FuzzyName)
        {
            // Highlight the query in the label (":q, :quit").
            const std::string label = lower(entries[s.entry].label());
            if(editor::helper::fuzzyScoreWithPositions(q, label, positions) >= 0)
                match.positions = positions;
        }
        out.push_back(std::move(match));
    }
    return out;
}
} // namespace command_palette
