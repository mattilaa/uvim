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
                                               const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    if(c == Terminal::ESC)
    {
        return NormalMode{};
    }

    if(c == Terminal::ENTER)
    {
        if(select(*ed))
        {
            return NormalMode{};
        }
        return std::nullopt;
    }

    if(c == Terminal::CTRL_N || c == Terminal::CTRL_J ||
       c == Terminal::ARROW_DOWN)
    {
        moveDown(*ed);
    }
    else if(c == Terminal::CTRL_P || c == Terminal::CTRL_K ||
            c == Terminal::ARROW_UP)
    {
        moveUp(*ed);
    }
    else if(c == Terminal::CTRL_D || c == Terminal::PAGE_DOWN)
    {
        halfPageDown(*ed);
    }
    else if(c == Terminal::PAGE_UP)
    {
        halfPageUp(*ed);
    }
    else if(c == Terminal::BACKSPACE || c == 127 || c == Terminal::CTRL_H)
    {
        backspace(*ed);
    }
    else if(c == Terminal::CTRL_W)
    {
        deleteWord(*ed);
    }
    else if(c == Terminal::CTRL_U)
    {
        clearQuery(*ed);
    }
    else if(c == Terminal::CTRL_I)
    {
        toggleGitignore(*ed);
    }
    else if(c == Terminal::CTRL_B)
    {
        return BufferBrowserMode{};
    }
    else if(c == Terminal::CTRL_S)
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
                output.append(padding, ' ');
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

    Terminal::write(output);
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

            std::vector<int> positions;
            int pathScore = editor.fuzzyScore(query, file.path, positions);

            std::vector<int> namePositions;
            int nameScore = editor.fuzzyScore(query, file.name, namePositions);

            int finalScore = std::max(pathScore, nameScore * 2);

            if(finalScore > 0)
            {
                FuzzyMatch match;
                match.file = file;
                match.score = finalScore;
                match.matchPositions =
                    (nameScore * 2 > pathScore) ? namePositions : positions;
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
    while(!query.empty() && query.back() == ' ')
        query.pop_back();
    while(!query.empty() && query.back() != ' ')
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

            if(prevMatchIdx >= 0 && i == (size_t)prevMatchIdx + 1)
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

            prevMatchIdx = static_cast<int>(i);
            needleIdx++;
        }
        else
        {
            if(prevMatchIdx >= 0)
            {
                score -= (int)(i - static_cast<size_t>(prevMatchIdx));
            }
        }
    }

    if(needleIdx != needle.length())
    {
        return -1;
    }

    score -= static_cast<int>(haystack.length());

    return score;
}
