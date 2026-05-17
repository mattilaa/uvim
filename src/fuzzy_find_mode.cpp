#include "editor.h"
#include "editor_utils.h"
#include "gitignore.h"
#include "mode_state_machine.h"
#include "process_pipe.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <limits.h>
#include <sstream>

// ============================================================================
// FuzzyFindMode Implementation
// ============================================================================

namespace
{
std::string runCmd(const std::vector<std::string>& args)
{
    ProcessPipe pipe(args);
    if(!pipe)
        return {};
    return pipe.readAll();
}

std::vector<std::string> splitNul(const std::string& s)
{
    std::vector<std::string> out;
    size_t i = 0;
    while(i < s.size())
    {
        size_t j = s.find('\0', i);
        if(j == std::string::npos)
            j = s.size();
        if(j > i)
            out.push_back(s.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

bool hasHiddenPathComponent(std::string_view path)
{
    size_t start = 0;
    while(start < path.size())
    {
        size_t end = path.find('/', start);
        if(end == std::string_view::npos)
            end = path.size();
        if(end > start && path[start] == '.')
            return true;
        start = end + 1;
    }
    return false;
}

std::string truncatePathMiddle(std::string path, int width)
{
    if(width <= 0)
        return "";
    if(text_utils::utf8DisplayWidth(path) <= width)
        return path;
    if(width <= 3)
        return std::string(width, '.');

    const std::string prefix = "...";
    const int suffixWidth = width - (int)prefix.size();
    std::string suffix = path;
    while(!suffix.empty() && text_utils::utf8DisplayWidth(suffix) > suffixWidth)
    {
        size_t slash = suffix.find('/');
        if(slash == std::string::npos || slash + 1 >= suffix.size())
        {
            suffix.erase(suffix.begin());
        }
        else
        {
            suffix.erase(0, slash + 1);
        }
    }
    return prefix + suffix;
}
} // namespace

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
    selectedFiles.clear();
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

std::optional<ModeState> FuzzyFindMode::handle(ModeContext& ctx, int key)
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
        if(!selectedFiles.empty() ? openSelected(*ed) : select(*ed))
        {
            return defaultExitMode(ed);
        }
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::CTRL_N))
    {
        toggleSelection();
    }
    else if(c == keyCode(control::ControlKey::CTRL_J) ||
            c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        moveDown(*ed);
    }
    else if(c == keyCode(control::ControlKey::CTRL_P) ||
            c == keyCode(control::ControlKey::CTRL_K) ||
            c == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        moveUp(*ed);
    }
    else if(c == keyCode(control::ControlKey::CTRL_D) ||
            c == keyCode(navigation::NavigationKey::PAGE_DOWN))
    {
        halfPageDown(*ed);
    }
    else if(c == keyCode(navigation::NavigationKey::PAGE_UP))
    {
        halfPageUp(*ed);
    }
    else if(c == keyCode(control::ControlKey::BACKSPACE) || c == 127 ||
            c == keyCode(control::ControlKey::CTRL_H))
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
    output += "  [Enter: open] [Esc: cancel] [Ctrl+N: select] "
              "[Ctrl+J/K: navigate] [Ctrl+I: gitignore]";
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
        output += "  " + std::to_string(projectFiles.size()) + " files";
    }
    output += " ";
    if(editor.respectGitignore)
    {
        output += "[gitignore]";
    }
    else
    {
        output += editor.theme.uiDim();
        output += "[gitignore off]";
        output += editor.theme.baseFg();
    }
    output += " ";
    if(editor.useGitFileIndex)
    {
        output += "[git index]";
    }
    else
    {
        output += editor.theme.uiDim();
        output += "[git index off]";
        output += editor.theme.baseFg();
    }
    if(!selectedFiles.empty())
    {
        output += " (" + std::to_string(selectedFiles.size()) + " selected)";
    }
    output += editor.theme.baseFg();

    int availableRows = editor.screenRows - 3;

    for(int i = 0; i < availableRows && i + offset < (int)matches.size(); i++)
    {
        output += Terminal::NEWLINE_CLEAR;

        int index = i + offset;
        const FuzzyMatch& match = matches[index];
        bool isSelected = selectedFiles.count(match.file.path) > 0;

        if(index == cursor && isSelected)
        {
            output += "\x1b[48;2;56;120;72m";
            output += editor.theme.baseFg();
        }
        else if(index == cursor)
        {
            output += editor.theme.selection();
        }
        else if(isSelected)
        {
            output += "\x1b[48;2;24;64;36m";
            output += editor.theme.baseFg();
        }

        output += "  ";

        std::string displayPath = match.file.path;
        std::error_code cwdEc;
        auto cwd = std::filesystem::current_path(cwdEc);
        if(!cwdEc)
        {
            std::string cwdStr = cwd.string();
            if(displayPath.find(cwdStr) == 0)
            {
                displayPath = displayPath.substr(cwdStr.length() + 1);
            }
        }

        std::string sizeStr;
        int pathWidth = std::max(1, editor.screenCols - 2);
        if(editor.screenCols > 60)
        {
            sizeStr = formatFileSizeShort(match.file.size);
            pathWidth =
                std::max(1, editor.screenCols - 2 - (int)sizeStr.length() - 2);
        }

        const bool truncated =
            text_utils::utf8DisplayWidth(displayPath) > pathWidth;
        displayPath = truncatePathMiddle(displayPath, pathWidth);

        if(!truncated && !query.empty() && !match.matchPositions.empty())
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

        if(!sizeStr.empty())
        {
            int padding = editor.screenCols - 2 -
                          text_utils::utf8DisplayWidth(displayPath) -
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
    if(projectFilesInitialized)
        return;

    projectFiles.clear();

    std::error_code cwdEc;
    auto cwd = std::filesystem::current_path(cwdEc);
    if(!cwdEc)
    {
        const std::string cwdStr = cwd.string();
        if(editor.useGitFileIndex)
        {
            std::string repoRoot =
                runCmd({"git", "-C", cwdStr, "rev-parse", "--show-toplevel"});

            if(!repoRoot.empty())
            {
                const std::string raw =
                    runCmd({"git", "-C", cwdStr, "ls-files", "-z", "--cached",
                            "--others", "--exclude-standard"});
                const auto relPaths = splitNul(raw);

                for(const auto& relPath : relPaths)
                {
                    if(relPath.empty())
                        continue;
                    if(hasHiddenPathComponent(relPath))
                        continue;

                    const std::string fullPath = cwdStr + "/" + relPath;
                    std::error_code stEc;
                    auto status = std::filesystem::status(fullPath, stEc);
                    if(stEc)
                        continue;
                    if(std::filesystem::is_directory(status))
                        continue;

                    FileEntry entry;
                    std::filesystem::path fullFs(fullPath);
                    entry.name = fullFs.filename().string();
                    entry.path = relPath;
                    entry.isDirectory = false;
                    std::error_code szEc;
                    entry.size =
                        (uintmax_t)std::filesystem::file_size(fullPath, szEc);
                    if(szEc)
                        entry.size = 0;
                    std::error_code mtEc;
                    auto ftime =
                        std::filesystem::last_write_time(fullPath, mtEc);
                    if(!mtEc)
                    {
                        using namespace std::chrono;
                        auto sctp = time_point_cast<system_clock::duration>(
                            ftime - decltype(ftime)::clock::now() +
                            system_clock::now());
                        entry.modTime = system_clock::to_time_t(sctp);
                    }
                    projectFiles.push_back(std::move(entry));
                }
                if(!projectFiles.empty())
                {
                    projectFilesInitialized = true;
                    return;
                }
            }
        }

        GitIgnore gitignore;
        if(editor.respectGitignore)
        {
            gitignore.loadRecursive(cwd);
        }
        editor::helper::collectProjectFileEntries(cwdStr, 0, gitignore,
                                                  projectFiles);
    }

    projectFilesInitialized = true;
}

void FuzzyFindMode::updateMatches(Editor& editor)
{
    matches.clear();
    selectedFiles.clear();

    if(query.empty())
    {
        for(const auto& file : projectFiles)
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
        for(const auto& file : projectFiles)
        {
            if(file.isDirectory)
                continue;

            std::vector<int> positions;
            int pathScore = editor::helper::fuzzyScoreWithPositions(
                query, file.path, positions);

            std::vector<int> namePositions;
            int nameScore = editor::helper::fuzzyScoreWithPositions(
                query, file.name, namePositions);

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
    if(editor.gitignoreLockedOff)
        return;
    editor.respectGitignore = !editor.respectGitignore;
    projectFilesInitialized = false;
    initializeFiles(editor);
    query.clear();
    cursor = 0;
    offset = 0;
    updateMatches(editor);
}

void FuzzyFindMode::refreshFileIndex(Editor& editor)
{
    projectFilesInitialized = false;
    initializeFiles(editor);
    cursor = 0;
    offset = 0;
    selectedFiles.clear();
    updateMatches(editor);
}

void FuzzyFindMode::toggleSelection()
{
    if(cursor < 0 || cursor >= (int)matches.size())
        return;

    const std::string& path = matches[cursor].file.path;
    auto it = selectedFiles.find(path);
    if(it != selectedFiles.end())
        selectedFiles.erase(it);
    else
        selectedFiles.insert(path);
}

bool FuzzyFindMode::select(Editor& editor)
{
    if(cursor < 0 || cursor >= (int)matches.size())
        return false;

    const FuzzyMatch& match = matches[cursor];
    editor.openFile(std::string_view(match.file.path));
    return true;
}

bool FuzzyFindMode::openSelected(Editor& editor)
{
    if(selectedFiles.empty())
        return false;

    std::vector<std::string> paths(selectedFiles.begin(), selectedFiles.end());
    std::sort(paths.begin(), paths.end());
    for(size_t i = 0; i < paths.size(); ++i)
    {
        bool notifyLsp = (i + 1 == paths.size());
        editor.openFile(std::string_view(paths[i]), notifyLsp);
    }
    selectedFiles.clear();
    return true;
}
