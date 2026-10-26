#include "command_palette.h"

#include "editor_utils.h"
#include "key_enums.h"

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

std::vector<std::string> words(const std::string& text)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string word;
    while(in >> word)
        out.push_back(word);
    return out;
}

int ctrl(control::ControlKey key)
{
    return keyCode(key);
}

constexpr int kSpace = ' ';

// Entry for a normal mode key sequence.
Entry keys(std::string title, std::string shortcut, std::vector<int> sequence,
           std::vector<std::string> names = {})
{
    return Entry{std::move(title), std::move(shortcut), std::move(names),
                 std::move(sequence), false};
}

// Entry for an ex command (names.front() runs).
Entry ex(std::string title, std::vector<std::string> names,
         bool takesArgument = false)
{
    std::string shortcut;
    for(size_t i = 0; i < names.size(); ++i)
        shortcut += (i ? ", :" : ":") + names[i];
    return Entry{std::move(title), std::move(shortcut), std::move(names), {},
                 takesArgument};
}

// Lower tier = better match.
enum Tier
{
    Exact = 0,
    Prefix,
    WordPrefixes,
    Words,
    Fuzzy,
    NoMatch
};
} // namespace

std::vector<Entry> builtinActions()
{
    std::vector<Entry> a;
    // Files and search.
#ifdef UVIM_ENABLE_SEARCH_TOOLS
    a.push_back(keys("Find file", "Ctrl-P", {ctrl(control::ControlKey::CTRL_P)}));
    a.push_back(keys("Search in project (grep)", "Ctrl-A",
                     {ctrl(control::ControlKey::CTRL_A)}));
    a.push_back(keys("Regex search", "Ctrl-X",
                     {ctrl(control::ControlKey::CTRL_X)}));
#endif
#ifdef UVIM_ENABLE_BROWSER_TOOLS
    a.push_back(keys("Open file browser", "Space x", {kSpace, 'x'}));
    a.push_back(keys("Change buffer", "Ctrl-W",
                     {ctrl(control::ControlKey::CTRL_W)}, {"ls", "buffers"}));
#endif
    a.push_back(ex("Open file\xE2\x80\xA6", {"e", "edit"}, true));
    a.push_back(keys("Next buffer", "Ctrl-L", {ctrl(control::ControlKey::CTRL_L)},
                     {"bn", "bnext"}));
    a.push_back(keys("Previous buffer", "Ctrl-H",
                     {ctrl(control::ControlKey::CTRL_H)},
                     {"bp", "bprev", "bprevious"}));
    a.push_back(keys("Close buffer", "Space b d", {kSpace, 'b', 'd'},
                     {"bd", "bdelete"}));
    a.push_back(ex("New empty buffer", {"enew"}));
    a.push_back(ex("Save file", {"w"}));
    a.push_back(ex("Save all files", {"wa", "wall"}));
    a.push_back(ex("Save as\xE2\x80\xA6", {"w"}, true));
    a.push_back(ex("Reload file from disk", {"e%", "edit%"}));
    a.push_back(ex("Quit", {"q"}));
    a.push_back(ex("Quit without saving", {"q!"}));
    a.push_back(ex("Save and quit", {"wq", "x"}));
    a.push_back(ex("Save all and quit", {"wqa", "wqall", "xa"}));
    // Editing.
#ifdef UVIM_ENABLE_FORMATTERS
    a.push_back(keys("Format buffer", "Space f", {kSpace, 'f'},
                     {"format", "fmt"}));
#endif
    a.push_back(keys("Clear search highlight", "Space n", {kSpace, 'n'}));
#ifdef UVIM_ENABLE_SYSTEM_CLIPBOARD
    a.push_back(keys("Paste from system clipboard", "Space p", {kSpace, 'p'}));
#endif
#ifdef UVIM_ENABLE_DEBUGGER
    a.push_back(ex("Debug executable", {"dbgrun"}));
    a.push_back(ex("Attach debugger to process", {"dbgattach"}));
#endif
    // Code navigation.
#ifdef UVIM_ENABLE_ASM_VIEW
    a.push_back(ex("Compile and browse assembly", {"asm"}));
#endif
#ifdef UVIM_ENABLE_BROWSER_TOOLS
    a.push_back(keys("Browse function signatures", "Space o", {kSpace, 'o'}, {"functions"}));
#endif
    a.push_back(keys("Next function signature", "]f", {']', 'f'}));
    a.push_back(keys("Previous function signature", "[f", {'[', 'f'}));
    a.push_back(keys("Next local declaration or call", "]s", {']', 's'}));
    a.push_back(keys("Previous local declaration or call", "[s", {'[', 's'}));
    a.push_back(keys("Go to definition", "Space d", {kSpace, 'd'}));
    a.push_back(keys("Find references", "Space g r", {kSpace, 'g', 'r'}));
    a.push_back(keys("Show signature of symbol", "Space s", {kSpace, 's'}));
    a.push_back(keys("Diagnostic details on this line", "Space e",
                     {kSpace, 'e'}));
    a.push_back(keys("List LSP errors", "Space m e", {kSpace, 'm', 'e'}));
    a.push_back(keys("List LSP warnings", "Space m w", {kSpace, 'm', 'w'}));
    // Windows.
    a.push_back(ex("Split vertically", {"vs"}));
    a.push_back(ex("Split horizontally", {"hs"}));
    a.push_back(ex("Close split pane", {"sc"}));
    a.push_back(ex("New tab", {"tabnew"}));
    // Git.
#ifdef UVIM_ENABLE_GIT_TOOLS
    a.push_back(ex("Show unsaved changes", {"showchanges"}));
    a.push_back(ex("Git: stage changes", {"git stage"}));
    a.push_back(ex("Git: commit", {"git commit"}));
    a.push_back(ex("Git: log", {"git log"}));
    a.push_back(ex("Git: log (split view)", {"git prettylog"}));
    a.push_back(ex("Git: diff", {"git diff"}));
    a.push_back(ex("Git: toggle blame", {"git blame"}));
    a.push_back(ex("Git: stash changes", {"git stash"}));
    a.push_back(ex("Git: pop stash", {"git stash pop"}));
#endif
    // Tools and info.
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
    a.push_back(ex("Help", {"help", "h"}));
    a.push_back(ex("Background tasks", {"tasks"}));
    a.push_back(ex("LSP status", {"lspinfo"}));
    a.push_back(ex("External tools status", {"toolinfo"}));
    a.push_back(ex("Lines of code in project", {"loctotal"}));
    a.push_back(ex("Insert emoji", {"emoji", "em"}));
    a.push_back(ex("Insert glyph", {"glyphselect"}));
#endif
#ifdef UVIM_ENABLE_COLOR_TOOLS
    a.push_back(keys("Color picker", "Space cp", {kSpace, 'c', 'p'},
                     {"colorpicker"}));
    a.push_back(keys("RGB color selector", "Space cs", {kSpace, 'c', 's'},
                     {"colorselect"}));
#endif
    a.push_back(ex("Change directory\xE2\x80\xA6", {"cd"}, true));
    a.push_back(ex("Show working directory", {"pwd"}));
    return a;
}

void addExCommands(std::vector<Entry>& entries,
                   const std::vector<std::string>& commands,
                   const Describe& describe)
{
    auto covered = [&](const std::string& name, bool takesArgument)
    {
        return std::any_of(entries.begin(), entries.end(),
                           [&](const Entry& entry)
                           {
                               return entry.takesArgument == takesArgument &&
                                      std::find(entry.names.begin(),
                                                entry.names.end(),
                                                name) != entry.names.end();
                           });
    };

    const size_t firstNew = entries.size();
    for(const std::string& raw : commands)
    {
        const bool takesArgument = !raw.empty() && raw.back() == ' ';
        const std::string name = trimRight(raw);
        if(name.empty() || covered(name, takesArgument))
            continue;
        std::string description = describe ? describe(raw) : "";
        if(description.empty())
            description = ":" + name;
        if(takesArgument)
            description += "\xE2\x80\xA6";

        // Aliases (same description) share one entry.
        auto same = std::find_if(entries.begin() + firstNew, entries.end(),
                                 [&](const Entry& entry)
                                 {
                                     return entry.takesArgument ==
                                                takesArgument &&
                                            entry.title == description;
                                 });
        if(same == entries.end())
        {
            entries.push_back(ex(description, {name}, takesArgument));
        }
        else
        {
            same->names.push_back(name);
            same->shortcut += ", :" + name;
        }
    }
}

std::vector<Match> rank(const std::vector<Entry>& entries,
                        const std::string& query)
{
    std::vector<Match> out;
    if(editor::helper::isRegexQuery(query))
    {
        std::vector<std::pair<Match, int>> ranked;
        for(int i = 0; i < static_cast<int>(entries.size()); ++i)
        {
            Match match{i, {}};
            int score = editor::helper::fuzzyScoreWithPositions(
                query, entries[i].title, match.positions);
            for(const auto& name : entries[i].names)
            {
                std::vector<int> ignored;
                score = std::max(score, editor::helper::fuzzyScoreWithPositions(
                                            query, name, ignored));
            }
            if(score >= 0)
                ranked.push_back({std::move(match), score});
        }
        std::stable_sort(ranked.begin(), ranked.end(),
                         [](const auto& a, const auto& b)
                         { return a.second > b.second; });
        for(auto& entry : ranked)
            out.push_back(std::move(entry.first));
        return out;
    }
    const std::string q =
        lower(query.starts_with("fz:") ? query.substr(3) : query);
    const std::vector<std::string> queryWords = words(q);
    if(queryWords.empty())
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
        const std::string title = lower(entry.title);
        const std::vector<std::string> titleWords = words(title);
        Tier tier = NoMatch;
        int score = 0;
        auto offer = [&](Tier t, int s)
        {
            if(t < tier || (t == tier && s > score))
            {
                tier = t;
                score = s;
            }
        };

        for(const std::string& name : entry.names)
        {
            const std::string n = lower(name);
            if(n == q)
                offer(Exact, 0);
            else if(n.rfind(q, 0) == 0)
                offer(Prefix, 1000 - (int)n.size());
        }
        if(title == q)
            offer(Exact, 0);
        else if(title.rfind(q, 0) == 0)
            offer(Prefix, 1000 - (int)title.size());

        // Every query word starts a title word ("file br" -> "Open file
        // browser"), or at least appears in the title.
        bool allPrefixes = true;
        bool allContained = true;
        for(const std::string& word : queryWords)
        {
            const bool prefix = std::any_of(
                titleWords.begin(), titleWords.end(),
                [&](const std::string& titleWord)
                { return titleWord.rfind(word, 0) == 0; });
            allPrefixes = allPrefixes && prefix;
            allContained =
                allContained && title.find(word) != std::string::npos;
        }
        if(allPrefixes)
            offer(WordPrefixes, 1000 - (int)title.size());
        else if(allContained)
            offer(Words, 1000 - (int)title.size());

        int s = editor::helper::fuzzyScoreWithPositions("fz:" + q, title,
                                                        positions);
        if(s >= 0)
            offer(Fuzzy, s);
        for(const std::string& name : entry.names)
        {
            s = editor::helper::fuzzyScoreWithPositions("fz:" + q, lower(name),
                                                        positions);
            if(s >= 0)
                offer(Fuzzy, s);
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
        // Highlight the query's letters in the title when they are there.
        const std::string title = lower(entries[s.entry].title);
        if(editor::helper::fuzzyScoreWithPositions("fz:" + q, title,
                                                   positions) >= 0)
            match.positions = positions;
        out.push_back(std::move(match));
    }
    return out;
}
} // namespace command_palette
