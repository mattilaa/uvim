#include "editor.h"
#include "gitignore.h"
#include "terminal.h"
#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

void Editor::initializeFuzzyFind()
{
    if(!fuzzyInitialized)
    {
        allProjectFiles.clear();

        // Get current working directory as project root
        char cwd[PATH_MAX];
        if(getcwd(cwd, sizeof(cwd)))
        {
            // Load gitignore patterns
            GitIgnore gitignore;
            gitignore.loadRecursive(cwd);
            collectProjectFiles(std::string(cwd), 0, gitignore);
        }

        fuzzyInitialized = true;
    }

    fuzzyQuery.clear();
    fuzzyCursor = 0;
    fuzzyOffset = 0;
    fuzzyMatches.clear();

    // Initially show all files
    for(const auto& file : allProjectFiles)
    {
        if(!file.isDirectory)
        {
            FuzzyMatch match;
            match.file = file;
            match.score = 0;
            fuzzyMatches.push_back(match);
        }
    }

    // Sort by path initially
    std::sort(fuzzyMatches.begin(), fuzzyMatches.end(),
              [](const FuzzyMatch& a, const FuzzyMatch& b)
              { return a.file.path < b.file.path; });
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
        if(name[0] == '.')
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
    matchPositions.clear();

    if(needle.empty())
        return 0;
    if(needle.length() > haystack.length())
        return -1;

    int score = 0;
    int consecutiveBonus = 10;
    int separatorBonus = 30;
    int camelBonus = 30;
    int firstLetterBonus = 15;

    size_t needleIdx = 0;
    int prevMatchIdx = -1;

    for(size_t i = 0; i < haystack.length() && needleIdx < needle.length(); i++)
    {
        char needleChar = std::tolower(needle[needleIdx]);
        char haystackChar = std::tolower(haystack[i]);

        if(needleChar == haystackChar)
        {
            matchPositions.push_back(i);

            score += 100;

            if(prevMatchIdx >= 0 && i == prevMatchIdx + 1)
            {
                score += consecutiveBonus;
            }

            if(i > 0)
            {
                char prevChar = haystack[i - 1];
                if(prevChar == '/' || prevChar == '-' || prevChar == '_' ||
                   prevChar == '.')
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
            {
                score += firstLetterBonus;
            }

            if(needle[needleIdx] == haystack[i])
            {
                score += 5;
            }

            prevMatchIdx = i;
            needleIdx++;
        }
        else
        {
            if(prevMatchIdx >= 0)
            {
                score -= (i - prevMatchIdx);
            }
        }
    }

    if(needleIdx != needle.length())
    {
        return -1;
    }

    score -= haystack.length();

    return score;
}

void Editor::updateFuzzyMatches()
{
    fuzzyMatches.clear();

    if(fuzzyQuery.empty())
    {
        for(const auto& file : allProjectFiles)
        {
            if(!file.isDirectory)
            {
                FuzzyMatch match;
                match.file = file;
                match.score = 0;
                fuzzyMatches.push_back(match);
            }
        }
    }
    else
    {
        for(const auto& file : allProjectFiles)
        {
            if(file.isDirectory)
                continue;

            std::vector<int> positions;
            int pathScore = fuzzyScore(fuzzyQuery, file.path, positions);

            std::vector<int> namePositions;
            int nameScore = fuzzyScore(fuzzyQuery, file.name, namePositions);

            int finalScore = std::max(pathScore, nameScore * 2);

            if(finalScore > 0)
            {
                FuzzyMatch match;
                match.file = file;
                match.score = finalScore;
                match.matchPositions =
                    (nameScore * 2 > pathScore) ? namePositions : positions;
                fuzzyMatches.push_back(match);
            }
        }

        std::sort(fuzzyMatches.begin(), fuzzyMatches.end(),
                  [](const FuzzyMatch& a, const FuzzyMatch& b)
                  { return a.score > b.score; });
    }

    if(fuzzyCursor >= fuzzyMatches.size())
    {
        fuzzyCursor = 0;
        fuzzyOffset = 0;
    }
}

void Editor::drawFuzzyFind()
{
    std::string output;
    output.reserve(screenRows * screenCols * 2);

    output += Terminal::ESC_CURSOR_HOME;
    output += Terminal::ESC_CLEAR_LINE;

    output += Terminal::ESC_BOLD;
    output += "  Find File: ";
    output += Terminal::ESC_RESET_ALL;
    output += Terminal::FG_GREEN;
    output += fuzzyQuery;

    output += Terminal::ESC_BLINK;
    output += "_";
    output += Terminal::ESC_BLINK_OFF;
    output += Terminal::FG_DEFAULT;

    output += Terminal::NEWLINE_CLEAR;
    output += Terminal::FG_BRIGHT_BLACK;
    output +=
        "  [Enter: open] [Esc: cancel] [↑↓: navigate] [Ctrl+I: gitignore]";
    output += Terminal::FG_DEFAULT;
    output += Terminal::NEWLINE_CLEAR;
    output += Terminal::FG_BRIGHT_BLACK;

    if(!fuzzyMatches.empty())
    {
        output += "  " + std::to_string(fuzzyMatches.size()) + " matches";
    }
    else if(!fuzzyQuery.empty())
    {
        output += "  No matches";
    }
    else
    {
        output += "  " + std::to_string(allProjectFiles.size()) + " files";
    }
    if(respectGitignore)
    {
        output += " [gitignore]";
    }
    output += Terminal::FG_DEFAULT;

    int availableRows = screenRows - 3;

    for(int i = 0; i < availableRows && i + fuzzyOffset < fuzzyMatches.size();
        i++)
    {
        output += Terminal::NEWLINE_CLEAR;

        int index = i + fuzzyOffset;
        const FuzzyMatch& match = fuzzyMatches[index];

        if(index == fuzzyCursor)
        {
            output += Terminal::STYLE_SELECTION;
        }

        output += "  ";

        char cwd[PATH_MAX];
        std::string displayPath = match.file.path;
        if(getcwd(cwd, sizeof(cwd)))
        {
            std::string cwdStr(cwd);
            if(displayPath.find(cwdStr) == 0)
            {
                displayPath = displayPath.substr(cwdStr.length() + 1);
            }
        }

        if(!fuzzyQuery.empty() && !match.matchPositions.empty())
        {
            size_t lastPos = 0;
            for(int pos : match.matchPositions)
            {
                if(pos >= 0 && pos < displayPath.length())
                {
                    if(pos > lastPos)
                    {
                        output += displayPath.substr(lastPos, pos - lastPos);
                    }

                    if(index != fuzzyCursor)
                    {
                        output += Terminal::STYLE_GREEN_BOLD;
                    }
                    output += displayPath[pos];
                    if(index != fuzzyCursor)
                    {
                        output += Terminal::STYLE_RESET_GREEN_BOLD;
                    }

                    lastPos = pos + 1;
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

        if(screenCols > 60)
        {
            std::string sizeStr = formatFileSize(match.file.size);
            int padding =
                screenCols - 2 - displayPath.length() - sizeStr.length() - 2;
            if(padding > 0)
            {
                output.append(padding, ' ');
            }
            output += Terminal::FG_BRIGHT_BLACK;
            output += sizeStr;
            output += Terminal::FG_DEFAULT;
        }

        output += Terminal::ESC_RESET_ALL;
    }

    for(int i = fuzzyMatches.size() - fuzzyOffset; i < availableRows; i++)
    {
        output += Terminal::NEWLINE_CLEAR;
        output += Terminal::FG_BLUE;
        output += "~";
        output += Terminal::FG_DEFAULT;
    }

    Terminal::write(output);
    Terminal::flush();
}

void Editor::selectFuzzyMatch()
{
    if(fuzzyCursor < fuzzyMatches.size())
    {
        const FuzzyMatch& match = fuzzyMatches[fuzzyCursor];
        openFile(match.file.path);
        setMode(NORMAL);
    }
}

void Editor::handleFuzzyFindMode(int c)
{
    if(dispatchModeKey(c))
    {
        return;
    }

    switch(c)
    {
    case Terminal::ENTER:
        selectFuzzyMatch();
        break;

    case Terminal::ESC:
        setMode(NORMAL);
        needsFullRedraw = true;
        break;

    case Terminal::ARROW_DOWN:
    case Terminal::CTRL_N:
    case Terminal::CTRL_J:
        if(fuzzyCursor < fuzzyMatches.size() - 1)
        {
            fuzzyCursor++;
            if(fuzzyCursor >= fuzzyOffset + screenRows - 3)
            {
                fuzzyOffset = fuzzyCursor - screenRows + 4;
            }
        }
        break;

    case Terminal::ARROW_UP:
    case Terminal::CTRL_P:
    case Terminal::CTRL_K:
        if(fuzzyCursor > 0)
        {
            fuzzyCursor--;
            if(fuzzyCursor < fuzzyOffset)
            {
                fuzzyOffset = fuzzyCursor;
            }
        }
        break;

    case Terminal::BACKSPACE:
    case Terminal::DEL:
        if(!fuzzyQuery.empty())
        {
            fuzzyQuery.pop_back();
            updateFuzzyMatches();
        }
        break;

    case Terminal::CTRL_U:
        fuzzyQuery.clear();
        updateFuzzyMatches();
        break;

    case Terminal::CTRL_I:
        toggleGitignore();
        fuzzyInitialized = false;
        initializeFuzzyFind();
        break;

    default:
        if(c >= 32 && c < 127)
        {
            fuzzyQuery += static_cast<char>(c);
            updateFuzzyMatches();
            fuzzyCursor = 0;
            fuzzyOffset = 0;
        }
        break;
    }
}
