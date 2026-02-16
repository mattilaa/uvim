#include "editor.h"
#include "gitignore.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <iomanip>
#include <limits.h>
#include <sstream>
#include <sys/stat.h>
#include <unordered_set>
#include <unistd.h>

// ============================================================================
// FuzzyFindMode Implementation
// ============================================================================

static std::string formatFileSizeShort(size_t size)
{
    const char* units[] = {"B", "K", "M", "G", "T"};
    int unitIndex = 0;
    double displaySize = size;

    while(displaySize >= 1024 && unitIndex < 4)
    {
        displaySize /= 1024;
        unitIndex++;
    }

    std::ostringstream ss;
    if(unitIndex == 0)
    {
        ss << std::setw(5) << size << units[unitIndex];
    }
    else
    {
        ss << std::fixed << std::setprecision(1) << std::setw(5) << displaySize
           << units[unitIndex];
    }

    return ss.str();
}

static std::string makeDisplayPath(const std::string& fullPath)
{
    char cwd[PATH_MAX];
    std::string displayPath = fullPath;
    if(getcwd(cwd, sizeof(cwd)))
    {
        std::string cwdStr(cwd);
        if(displayPath.find(cwdStr) == 0)
            displayPath = displayPath.substr(cwdStr.length() + 1);
    }
    return displayPath;
}

void FuzzyFindMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    initializeFiles(*ed);
    query.clear();
    cursor = 0;
    offset = 0;
    updateMatches(*ed);
    ed->needsFullRedraw = true;

    // Set cursor to bar for input
    Terminal::setCursorBarBlinking();
}

void FuzzyFindMode::on_exit(ModeContext& /* ctx */)
{
    // Restore block cursor
    Terminal::setCursorBlock();
}

std::optional<ModeState> FuzzyFindMode::handle(ModeContext& ctx,
                                               int key)
{
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    if(c == keyCode(control::ControlKey::ESC))
    {
        ed->noteDoubleEscStatusClear();
        return defaultExitMode(ed);
    }

    if(c == keyCode(control::ControlKey::ENTER))
    {
        if(select(*ed))
        {
            return defaultExitMode(ed);
        }
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::CTRL_N) || c == keyCode(control::ControlKey::CTRL_J) ||
       c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        moveDown(*ed);
    }
    else if(c == keyCode(typed::TypedKey::KEY_N))
    {
        moveDown(*ed);
    }
    else if(c == keyCode(control::ControlKey::CTRL_P) || c == keyCode(control::ControlKey::CTRL_K) ||
            c == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        moveUp(*ed);
    }
    else if(c == keyCode(typed::TypedKey::KEY_CAP_N))
    {
        moveUp(*ed);
    }
    else if(c == keyCode(control::ControlKey::CTRL_D) || c == keyCode(navigation::NavigationKey::PAGE_DOWN))
    {
        halfPageDown(*ed);
    }
    else if(c == keyCode(navigation::NavigationKey::PAGE_UP))
    {
        halfPageUp(*ed);
    }
    else if(c == keyCode(control::ControlKey::BACKSPACE) || c == 127 || c == keyCode(control::ControlKey::CTRL_H))
    {
        backspace(*ed);
    }
    else if(c == keyCode(control::ControlKey::CTRL_W))
    {
        deleteWord(*ed);
    }
    else if(c == keyCode(control::ControlKey::CTRL_U))
    {
        clearQuery(*ed);
    }
    else if(c == keyCode(control::ControlKey::CTRL_I))
    {
        toggleGitignore(*ed);
    }
    else if(c == keyCode(control::ControlKey::CTRL_B))
    {
        return BufferBrowserMode{};
    }
    else if(c == keyCode(control::ControlKey::CTRL_S))
    {
        return GrepSearchMode{};
    }
    else if(c >= 32 && c < 127)
    {
        addChar(*ed, static_cast<char>(c));
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}

void FuzzyFindMode::draw(Editor& editor) const
{
    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

    output += Terminal::ESC_HIDE_CURSOR;
    output += Terminal::ESC_CURSOR_HOME;
    output += editor.theme.reset();
    output += Terminal::ESC_CLEAR_LINE;

    output += Terminal::ESC_BOLD;
    output += "  Find File: ";
    output += editor.theme.reset();
    output += editor.theme.uiPrompt();
    output += query;

    output += Terminal::ESC_BLINK;
    output += "_";
    output += Terminal::ESC_BLINK_OFF;
    output += editor.theme.baseFg();

    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.uiDim();
    output += "  [Enter: open] [Esc: cancel] [Ctrl+J/K: navigate] [Ctrl+I: "
              "gitignore]";
    output += editor.theme.baseFg();

    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.uiDim();

    if(!matches.empty())
    {
        output += "  " + std::to_string(matches.size()) + " matches";
    }
    else if(!query.empty())
    {
        output += "  No matches";
    }
    else
    {
        output +=
            "  " + std::to_string(editor.allProjectFiles.size()) + " files";
    }
    if(editor.respectGitignore)
    {
        output += " [gitignore]";
    }
    output += editor.theme.baseFg();

    int availableRows = editor.screenRows - 3;

    for(int i = 0; i < availableRows && i + offset < (int)matches.size(); i++)
    {
        output += Terminal::NEWLINE_CLEAR;

        int index = i + offset;
        const FuzzyMatch& match = matches[index];

        if(index == cursor)
        {
            output += editor.theme.selection();
        }

        output += "  ";

        std::string displayPath = makeDisplayPath(match.file.path);

        if(!query.empty() && !match.matchPositions.empty())
        {
            size_t lastPos = 0;
            for(int pos : match.matchPositions)
            {
                if(pos >= 0 && pos < (int)displayPath.length())
                {
                    if((size_t)pos > lastPos)
                    {
                        output += displayPath.substr(lastPos, pos - lastPos);
                    }

                    if(index != cursor)
                    {
                        output += editor.theme.matchHighlight();
                    }
                    output += displayPath[pos];
                    if(index != cursor)
                    {
                        output += editor.theme.baseFg();
                    }

                    lastPos = (size_t)pos + 1;
                }
            }
            if(lastPos < displayPath.length())
            {
                output += displayPath.substr(lastPos);
            }
        }
        else
        {
            output += displayPath;
        }

        if(editor.screenCols > 60)
        {
            std::string sizeStr = formatFileSizeShort(match.file.size);
            int padding = editor.screenCols - 2 - (int)displayPath.length() -
                          (int)sizeStr.length() - 2;
            if(padding > 0)
            {
                output.append(padding, keyCode(control::ControlKey::SPACE));
            }
            output += editor.theme.uiDim();
            output += sizeStr;
            output += editor.theme.baseFg();
        }

        output += editor.theme.reset();
    }

    for(int i = (int)matches.size() - offset; i < availableRows; i++)
    {
        output += Terminal::NEWLINE_CLEAR;
        output += editor.theme.uiGutter();
        output += "~";
        output += editor.theme.baseFg();
    }

    const bool syncOutput = Terminal::useSynchronizedOutput();
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_BEGIN);
    Terminal::write(output);
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_END);
    Terminal::flush();
}

void FuzzyFindMode::initializeFiles(Editor& editor)
{
    if(editor.fuzzyInitialized)
        return;

    editor.allProjectFiles.clear();

    char cwd[PATH_MAX];
    if(getcwd(cwd, sizeof(cwd)))
    {
        GitIgnore gitignore;
        if(editor.respectGitignore)
        {
            gitignore.loadRecursive(cwd);
        }
        editor.collectProjectFiles(std::string(cwd), 0, gitignore);
    }

    editor.fuzzyInitialized = true;
}

void FuzzyFindMode::updateMatches(Editor& editor)
{
    matches.clear();

    if(query.empty())
    {
        for(const auto& file : editor.allProjectFiles)
        {
            if(!file.isDirectory)
            {
                FuzzyMatch match;
                match.file = file;
                match.score = 0;
                matches.push_back(match);
            }
        }

        std::sort(matches.begin(), matches.end(),
                  [](const FuzzyMatch& a, const FuzzyMatch& b)
                  { return a.file.path < b.file.path; });
    }
    else
    {
        for(const auto& file : editor.allProjectFiles)
        {
            if(file.isDirectory)
                continue;

            std::string displayPath = makeDisplayPath(file.path);

            std::vector<int> positions;
            int pathScore = editor.fuzzyScore(query, displayPath, positions);

            std::vector<int> namePositions;
            int nameScore = editor.fuzzyScore(query, file.name, namePositions);

            int finalScore = std::max(pathScore, nameScore * 2);

            if(finalScore > 0)
            {
                FuzzyMatch match;
                match.file = file;
                match.score = finalScore;
                if(nameScore * 2 > pathScore)
                {
                    size_t basePos = displayPath.rfind(file.name);
                    if(basePos != std::string::npos)
                    {
                        match.matchPositions = namePositions;
                        for(int& p : match.matchPositions)
                            p += static_cast<int>(basePos);
                    }
                    else
                    {
                        match.matchPositions = positions;
                    }
                }
                else
                {
                    match.matchPositions = positions;
                }
                matches.push_back(match);
            }
        }

        std::sort(matches.begin(), matches.end(),
                  [](const FuzzyMatch& a, const FuzzyMatch& b)
                  { return a.score > b.score; });
    }

    if(cursor >= (int)matches.size())
    {
        cursor = 0;
        offset = 0;
    }
}

void FuzzyFindMode::moveDown(Editor& editor)
{
    if(matches.empty())
        return;

    if(cursor < (int)matches.size() - 1)
    {
        cursor++;
        int visible = editor.screenRows - 3;
        if(cursor >= offset + visible)
            offset = cursor - visible + 1;
    }
}

void FuzzyFindMode::moveUp(Editor& /* editor */)
{
    if(matches.empty())
        return;

    if(cursor > 0)
    {
        cursor--;
        if(cursor < offset)
            offset = cursor;
    }
}

void FuzzyFindMode::halfPageDown(Editor& editor)
{
    if(matches.empty())
        return;

    int half = (editor.screenRows - 3) / 2;
    cursor += half;
    if(cursor >= (int)matches.size())
        cursor = (int)matches.size() - 1;
    int visible = editor.screenRows - 3;
    if(cursor >= offset + visible)
        offset = cursor - visible + 1;
}

void FuzzyFindMode::halfPageUp(Editor& editor)
{
    if(matches.empty())
        return;

    int half = (editor.screenRows - 3) / 2;
    cursor -= half;
    if(cursor < 0)
        cursor = 0;
    if(cursor < offset)
        offset = cursor;
}

void FuzzyFindMode::addChar(Editor& editor, char c)
{
    query += c;
    updateMatches(editor);
    cursor = 0;
    offset = 0;
}

void FuzzyFindMode::backspace(Editor& editor)
{
    if(!query.empty())
    {
        query.pop_back();
        updateMatches(editor);
        cursor = 0;
        offset = 0;
    }
}

void FuzzyFindMode::deleteWord(Editor& editor)
{
    while(!query.empty() && query.back() == keyCode(control::ControlKey::SPACE))
        query.pop_back();
    while(!query.empty() && query.back() != keyCode(control::ControlKey::SPACE))
        query.pop_back();
    updateMatches(editor);
    cursor = 0;
    offset = 0;
}

void FuzzyFindMode::clearQuery(Editor& editor)
{
    query.clear();
    updateMatches(editor);
    cursor = 0;
    offset = 0;
}

void FuzzyFindMode::toggleGitignore(Editor& editor)
{
    editor.respectGitignore = !editor.respectGitignore;
    editor.fuzzyInitialized = false;
    initializeFiles(editor);
    query.clear();
    cursor = 0;
    offset = 0;
    updateMatches(editor);
}

bool FuzzyFindMode::select(Editor& editor)
{
    if(cursor < 0 || cursor >= (int)matches.size())
        return false;

    const FuzzyMatch& match = matches[cursor];
    editor.openFile(std::string_view(match.file.path));
    return true;
}

void Editor::collectProjectFiles(const std::string& dir, int depth,
                                 const GitIgnore& gitignore)
{
    if(depth > 10)
        return; // Limit recursion depth

    DIR* d = opendir(dir.c_str());
    if(!d)
        return;

    struct dirent* entry;
    while((entry = readdir(d)))
    {
        std::string name = entry->d_name;

        // Skip hidden files and special directories
        if(name == "." || name == "..")
            continue;

        std::string fullPath = dir + "/" + name;

        struct stat st;
        if(stat(fullPath.c_str(), &st) != 0)
            continue;

        bool isDir = S_ISDIR(st.st_mode);

        // Check gitignore
        if(gitignore.isIgnored(fullPath, isDir))
            continue;

        // Skip hidden files (starting with .)
        if(name[0] == keyCode(command::CommandKey::KEY_DOT))
            continue;

        FileEntry fileEntry;
        fileEntry.name = name;
        fileEntry.path = fullPath;
        fileEntry.isDirectory = isDir;
        fileEntry.size = st.st_size;
        fileEntry.modTime = st.st_mtime;

        allProjectFiles.push_back(fileEntry);

        if(isDir)
        {
            collectProjectFiles(fullPath, depth + 1, gitignore);
        }
    }

    closedir(d);
}

int Editor::fuzzyScore(const std::string& needle, const std::string& haystack,
                       std::vector<int>& matchPositions)
{
    auto asciiLower = [](char ch) -> char
    {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    };
    auto isTokenChar = [](char ch) -> bool
    {
        unsigned char u = static_cast<unsigned char>(ch);
        return std::isalnum(u) != 0 || ch == keyCode(command::CommandKey::KEY_UNDERSCORE);
    };
    auto boundedDamerauLevenshtein = [&](const std::string& a,
                                         const std::string& b,
                                         int maxDist) -> int
    {
        const int n = static_cast<int>(a.size());
        const int m = static_cast<int>(b.size());
        if(std::abs(n - m) > maxDist)
            return maxDist + 1;
        if(n == 0)
            return m;
        if(m == 0)
            return n;

        std::vector<int> prev2(m + 1, 0);
        std::vector<int> prev(m + 1, 0);
        std::vector<int> curr(m + 1, 0);
        for(int j = 0; j <= m; ++j)
            prev[j] = j;

        for(int i = 1; i <= n; ++i)
        {
            curr[0] = i;
            int rowMin = curr[0];
            for(int j = 1; j <= m; ++j)
            {
                int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
                int del = prev[j] + 1;
                int ins = curr[j - 1] + 1;
                int sub = prev[j - 1] + cost;
                int best = std::min({del, ins, sub});
                if(i > 1 && j > 1 && a[i - 1] == b[j - 2] &&
                   a[i - 2] == b[j - 1])
                {
                    best = std::min(best, prev2[j - 2] + 1);
                }
                curr[j] = best;
                rowMin = std::min(rowMin, best);
            }
            if(rowMin > maxDist)
                return maxDist + 1;
            prev2.swap(prev);
            prev.swap(curr);
        }
        return prev[m];
    };
    auto tokenProximityBonus = [&]() -> int
    {
        if(needle.empty())
            return 0;

        std::string needleLower = needle;
        std::transform(needleLower.begin(), needleLower.end(),
                       needleLower.begin(), asciiLower);

        int bestDist = 3;
        for(size_t i = 0; i < haystack.size();)
        {
            while(i < haystack.size() && !isTokenChar(haystack[i]))
                ++i;
            size_t start = i;
            while(i < haystack.size() && isTokenChar(haystack[i]))
                ++i;
            if(start == i)
                continue;

            std::string token = haystack.substr(start, i - start);
            std::transform(token.begin(), token.end(), token.begin(),
                           asciiLower);

            int distFull = boundedDamerauLevenshtein(needleLower, token, 2);
            bestDist = std::min(bestDist, distFull);

            // Compare against token prefixes near query length so short typos
            // can match long identifiers (e.g. "setsut" -> "setStatusMessage").
            if(token.size() > needleLower.size())
            {
                size_t minLen = needleLower.size();
                size_t maxLen = std::min(token.size(), needleLower.size() + 2);
                for(size_t len = minLen; len <= maxLen; ++len)
                {
                    int distPrefix = boundedDamerauLevenshtein(
                        needleLower, token.substr(0, len), 2);
                    bestDist = std::min(bestDist, distPrefix);
                    if(bestDist == 0)
                        break;
                }
            }
            if(bestDist == 0)
                break;
        }

        if(bestDist == 0)
            return 220;
        if(bestDist == 1)
            return 150;
        if(bestDist == 2)
            return 90;
        return 0;
    };

    auto scoreExact = [&](const std::string& localNeedle,
                          std::vector<int>& outPositions) -> int
    {
        outPositions.clear();

        if(localNeedle.empty())
            return 0;
        if(localNeedle.length() > haystack.length())
            return -1;

        int score = 0;
        const int consecutiveBonus = 10;
        const int separatorBonus = 30;
        const int camelBonus = 30;
        const int firstLetterBonus = 15;

        size_t needleIdx = 0;
        int prevMatchIdx = -1;

        for(size_t i = 0; i < haystack.length() && needleIdx < localNeedle.length();
            i++)
        {
            char needleChar =
                static_cast<char>(std::tolower(localNeedle[needleIdx]));
            char haystackChar = static_cast<char>(std::tolower(haystack[i]));

            if(needleChar == haystackChar)
            {
                outPositions.push_back(static_cast<int>(i));
                score += 100;

                if(prevMatchIdx >= 0 && i == (size_t)prevMatchIdx + 1)
                    score += consecutiveBonus;

                if(i > 0)
                {
                    char prevChar = haystack[i - 1];
                    if(prevChar == keyCode(command::CommandKey::KEY_SLASH) ||
                       prevChar == keyCode(command::CommandKey::KEY_MINUS) ||
                       prevChar ==
                           keyCode(command::CommandKey::KEY_UNDERSCORE) ||
                       prevChar == keyCode(command::CommandKey::KEY_DOT))
                    {
                        score += separatorBonus;
                    }
                }

                if(i > 0 && std::islower(haystack[i - 1]) &&
                   std::isupper(haystack[i]))
                {
                    score += camelBonus;
                }

                if(i == 0)
                    score += firstLetterBonus;

                if(localNeedle[needleIdx] == haystack[i])
                    score += 5;

                prevMatchIdx = static_cast<int>(i);
                needleIdx++;
            }
            else if(prevMatchIdx >= 0)
            {
                score -= static_cast<int>(i - static_cast<size_t>(prevMatchIdx));
            }
        }

        if(needleIdx != localNeedle.length())
            return -1;

        score -= static_cast<int>(haystack.length());
        return score;
    };

    std::vector<int> exactPositions;
    int exactScore = scoreExact(needle, exactPositions);
    if(exactScore >= 0)
    {
        matchPositions = std::move(exactPositions);
        return exactScore + tokenProximityBonus();
    }

    if(!fuzzyTypoTolerance || needle.size() < 2)
    {
        matchPositions.clear();
        return -1;
    }

    int bestScore = -1;
    std::vector<int> bestPositions;
    auto considerVariant = [&](const std::string& variant, int typoPenalty)
    {
        std::vector<int> variantPositions;
        int variantScore = scoreExact(variant, variantPositions);
        if(variantScore < 0)
            return;
        variantScore -= typoPenalty;
        if(bestScore < 0 || variantScore > bestScore)
        {
            bestScore = variantScore;
            bestPositions = std::move(variantPositions);
        }
    };

    struct VariantNode
    {
        std::string pattern;
        int penalty = 0;
        int depth = 0;
    };

    std::vector<VariantNode> frontier;
    frontier.push_back({needle, 0, 0});
    std::unordered_set<std::string> seen;
    seen.insert(needle);

    // Allow small typo chains (up to 2 edits): omission + transposition, etc.
    constexpr int kMaxDepth = 2;
    constexpr int kRemovePenalty = 90;
    constexpr int kSwapPenalty = 65;
    constexpr int kMaxPenalty = 240;

    for(int depth = 0; depth < kMaxDepth; ++depth)
    {
        std::vector<VariantNode> next;
        for(const auto& node : frontier)
        {
            const std::string& p = node.pattern;

            // Single-character omission typo (extra typed character).
            for(size_t removeIdx = 0; removeIdx < p.size(); ++removeIdx)
            {
                std::string variant = p;
                variant.erase(removeIdx, 1);
                if(variant.empty())
                    continue;
                int penalty = node.penalty + kRemovePenalty;
                if(penalty > kMaxPenalty)
                    continue;
                considerVariant(variant, penalty);
                if(seen.insert(variant).second)
                    next.push_back({std::move(variant), penalty, node.depth + 1});
            }

            // Adjacent transposition typo (e.g. "gti" -> "git").
            for(size_t i = 0; i + 1 < p.size(); ++i)
            {
                std::string variant = p;
                std::swap(variant[i], variant[i + 1]);
                int penalty = node.penalty + kSwapPenalty;
                if(penalty > kMaxPenalty)
                    continue;
                considerVariant(variant, penalty);
                if(seen.insert(variant).second)
                    next.push_back({std::move(variant), penalty, node.depth + 1});
            }
        }
        frontier = std::move(next);
        if(frontier.empty())
            break;
    }

    if(bestScore >= 0)
    {
        matchPositions = std::move(bestPositions);
        return bestScore + tokenProximityBonus();
    }

    matchPositions.clear();
    return -1;
}
