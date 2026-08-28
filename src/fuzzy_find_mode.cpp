#include "color_constant.h"
#include "editor.h"
#include "editor_utils.h"
#include "gitignore.h"
#include "header_help.h"
#include "mode_state_machine.h"
#include "process_pipe.h"
#include "search_match_colors.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <limits.h>
#include <sstream>
#include <unordered_set>

// ============================================================================
// FuzzyFindMode Implementation
// ============================================================================

namespace editor::statemachine
{
namespace
{
std::vector<std::string> fuzzyFindHelpTokens(bool filenameFirst,
                                             int contrast)
{
    return {"[Enter: open]",
            "[Esc: cancel]",
            "[Ctrl+N: select]",
            "[Ctrl+J/K: navigate]",
            "[Ctrl+I: gitignore]",
            "[Ctrl+T: contrast " + std::to_string(contrast) + "]",
            filenameFirst ? "[Ctrl+O: filename first]"
                          : "[Ctrl+O: path first]"};
}

int fuzzyFindHeaderRows(int screenCols, bool filenameFirst, int contrast)
{
    return 2 + HeaderHelp::lineCount(
                   fuzzyFindHelpTokens(filenameFirst, contrast),
                   screenCols);
}

int fuzzyFindVisibleRows(const Editor& editor, bool filenameFirst)
{
    return std::max(
        1, editor.screenRows -
               fuzzyFindHeaderRows(editor.screenCols, filenameFirst,
                                   editor.searchMatchContrast));
}

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
        if(text_utils::is_not_found(j))
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
        if(text_utils::is_not_found(end))
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
        if(text_utils::is_not_found(slash) || slash + 1 >= suffix.size())
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

std::string fuzzyResultBackground(const FuzzyMatch& match,
                                  std::string_view query,
                                  bool explicitlySelected, int contrast)
{
    size_t contentLength = std::max<size_t>(1, match.file.path.size());
    const size_t nameStart = match.file.path.find_last_of('/');
    const size_t adjustedNameStart =
        text_utils::is_found(nameStart) ? nameStart + 1 : 0;
    if(!match.matchPositions.empty() &&
       std::all_of(match.matchPositions.begin(), match.matchPositions.end(),
                   [adjustedNameStart](int pos)
                   { return pos >= static_cast<int>(adjustedNameStart); }))
    {
        contentLength = std::max<size_t>(1, match.file.name.size());
    }

    const double coverage =
        static_cast<double>(query.size()) / static_cast<double>(contentLength);
    int adjacentPairs = 0;
    for(size_t i = 1; i < match.matchPositions.size(); ++i)
    {
        if(match.matchPositions[i] == match.matchPositions[i - 1] + 1)
            ++adjacentPairs;
    }
    const double cohesion = match.matchPositions.size() > 1
                                ? static_cast<double>(adjacentPairs) /
                                      (match.matchPositions.size() - 1)
                                : 0.0;
    return SearchMatchColors::matchBackground(
        SearchMatchColors::relevanceStrength(coverage, cohesion), contrast,
        explicitlySelected);
}

std::string singleLinePasteText(std::string text)
{
    text.erase(std::remove_if(text.begin(), text.end(),
                              [](char ch) { return ch == '\n' || ch == '\r'; }),
               text.end());
    return text;
}

std::vector<int> positionsForFileNameInPath(const std::string& path,
                                            const std::vector<int>& positions)
{
    size_t nameStart = path.find_last_of('/');
    nameStart = text_utils::is_found(nameStart) ? nameStart + 1 : 0;
    std::vector<int> adjusted;
    adjusted.reserve(positions.size());
    for(int pos : positions)
        adjusted.push_back(static_cast<int>(nameStart) + pos);
    return adjusted;
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
    prewarmAroundCursor(*ed);
    ed->needsFullRedraw = true;

    // Set cursor to bar for input
    Terminal::setCursorBarBlinking();
}

void FuzzyFindMode::on_exit(ModeContext& ctx)
{
    if(projectFilesInitialized)
    {
        ctx.editor->fuzzyProjectFiles = std::move(projectFiles);
        ctx.editor->fuzzyFileIndexInitialized = true;
    }

    // Restore block cursor
    Terminal::setCursorBlock();
}

std::optional<ModeState> FuzzyFindMode::handle(ModeContext& ctx,
                                               const ModeKeyEvent& event)
{
    const int key = event.key;
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
    else if(c == keyCode(control::ControlKey::CTRL_O))
    {
        toggleFilenameFirst(*ed);
    }
    else if(c == keyCode(control::ControlKey::CTRL_I))
    {
        toggleGitignore(*ed);
    }
    else if(c == keyCode(control::ControlKey::CTRL_T))
    {
        ed->searchMatchContrast = ed->searchMatchContrast >= 70 ? 35 : 100;
    }
    else if(c == keyCode(control::ControlKey::CTRL_B))
    {
        return BufferBrowserMode{};
    }
    else if(c == keyCode(control::ControlKey::CTRL_A))
    {
        return GrepSearchMode{};
    }
    else if(c == keyCode(control::ControlKey::PASTE))
    {
        std::string text = singleLinePasteText(Terminal::takeLastPasteText());
        if(!text.empty())
        {
            query += text;
            updateMatches(*ed);
            cursor = 0;
            offset = 0;
            prewarmAroundCursor(*ed);
        }
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

    HeaderHelp::append(output, editor.theme, editor.screenCols,
                       fuzzyFindHelpTokens(filenameFirst,
                                           editor.searchMatchContrast));

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
    output += " ";
    output += filenameFirst ? "[filename first]" : "[path first]";
    output += editor.theme.baseFg();

    int availableRows = fuzzyFindVisibleRows(editor, filenameFirst);
    for(int i = 0; i < availableRows && i + offset < (int)matches.size(); i++)
    {
        output += Terminal::NEWLINE_CLEAR;

        int index = i + offset;
        const FuzzyMatch& match = matches[index];
        bool isSelected = selectedFiles.count(match.file.path) > 0;

        if(index == cursor)
        {
            output += SearchMatchColors::selectedRowBackground();
            output += editor.theme.baseFg();
        }
        else if(!query.empty())
        {
            output += fuzzyResultBackground(match, query, isSelected,
                                            editor.searchMatchContrast);
            output += editor.theme.baseFg();
        }
        else if(isSelected)
        {
            output += SearchMatchColors::markedRowBackground();
            output += editor.theme.baseFg();
        }

        output += "  ";
        output += editor.theme.uiInfo();

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
        if(editor.screenCols > 60 && match.file.metadataLoaded)
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
                        output += editor.theme.uiInfo();
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

        int renderedWidth = 2 + text_utils::utf8DisplayWidth(displayPath);
        if(!sizeStr.empty())
        {
            int padding = editor.screenCols - 2 -
                          text_utils::utf8DisplayWidth(displayPath) -
                          (int)sizeStr.length() - 2;
            if(padding > 0)
            {
                output.append(padding, keyCode(control::ControlKey::SPACE));
                renderedWidth += padding;
            }
            output += editor.theme.uiDim();
            output += sizeStr;
            renderedWidth += static_cast<int>(sizeStr.length());
        }

        if(renderedWidth < editor.screenCols)
            output.append(editor.screenCols - renderedWidth, ' ');

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

    std::error_code cwdEc;
    auto cwd = std::filesystem::current_path(cwdEc);
    const std::string cwdStr = cwdEc ? std::string{} : cwd.string();
    const bool cacheMatches =
        editor.fuzzyFileIndexInitialized &&
        editor.fuzzyFileIndexCwd == cwdStr &&
        editor.fuzzyFileIndexRespectGitignore == editor.respectGitignore &&
        editor.fuzzyFileIndexUseGit == editor.useGitFileIndex;
    if(cacheMatches)
    {
        projectFiles = std::move(editor.fuzzyProjectFiles);
        editor.fuzzyFileIndexInitialized = false;
        projectFilesInitialized = true;
        return;
    }

    editor.fuzzyProjectFiles.clear();
    editor.fuzzyFileIndexInitialized = false;
    editor.fuzzyFileIndexCwd = cwdStr;
    editor.fuzzyFileIndexRespectGitignore = editor.respectGitignore;
    editor.fuzzyFileIndexUseGit = editor.useGitFileIndex;
    projectFiles.clear();

    if(!cwdEc)
    {
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
#ifdef _WIN32
                const auto deletedPathsList = splitNul(runCmd(
                    {"git", "-C", cwdStr, "ls-files", "-z", "--deleted"}));
                const std::unordered_set<std::string> deletedPaths(
                    deletedPathsList.begin(), deletedPathsList.end());
#endif

                for(const auto& relPath : relPaths)
                {
                    if(relPath.empty())
                        continue;
                    if(hasHiddenPathComponent(relPath))
                        continue;
#ifdef _WIN32
                    if(deletedPaths.count(relPath) != 0)
                        continue;
#endif

                    const std::string fullPath = cwdStr + "/" + relPath;
#ifndef _WIN32
                    std::error_code stEc;
                    auto status = std::filesystem::status(fullPath, stEc);
                    if(stEc)
                        continue;
                    if(std::filesystem::is_directory(status))
                        continue;
#endif

                    FileEntry entry;
                    std::filesystem::path fullFs(fullPath);
                    entry.name = fullFs.filename().string();
                    entry.path = relPath;
                    entry.isDirectory = false;
#ifndef _WIN32
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
                    entry.metadataLoaded = true;
#endif
                    projectFiles.push_back(std::move(entry));
                }
                if(!projectFiles.empty())
                {
                    std::sort(projectFiles.begin(), projectFiles.end(),
                              [](const FileEntry& a, const FileEntry& b)
                              { return a.path < b.path; });
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

    std::sort(projectFiles.begin(), projectFiles.end(),
              [](const FileEntry& a, const FileEntry& b)
              { return a.path < b.path; });
    projectFilesInitialized = true;
}

void FuzzyFindMode::updateMatches(Editor& /* editor */)
{
    matches.clear();
    matches.reserve(projectFiles.size());
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

            const bool nameMatched = nameScore > 0;
            const bool pathMatched = pathScore > 0;
            int finalScore = -1;
            bool useNamePositions = false;

            if(filenameFirst)
            {
                if(nameMatched)
                {
                    finalScore = 100000 + nameScore * 4;
                    useNamePositions = true;
                }
                else if(pathMatched)
                {
                    finalScore = pathScore;
                }
            }
            else
            {
                finalScore = std::max(pathScore, nameScore * 2);
                useNamePositions = nameScore * 2 > pathScore;
            }

            if(finalScore > 0)
            {
                FuzzyMatch match;
                match.file = file;
                match.score = finalScore;
                match.matchPositions = useNamePositions
                                           ? positionsForFileNameInPath(
                                                 file.path, namePositions)
                                           : positions;
                matches.push_back(match);
            }
        }

        std::sort(matches.begin(), matches.end(),
                  [](const FuzzyMatch& a, const FuzzyMatch& b)
                  {
                      if(a.score != b.score)
                          return a.score > b.score;
                      return a.file.path < b.file.path;
                  });
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
        int visible = fuzzyFindVisibleRows(editor, filenameFirst);
        if(cursor >= offset + visible)
            offset = cursor - visible + 1;
    }
    prewarmAroundCursor(editor);
}

void FuzzyFindMode::moveUp(Editor& editor)
{
    if(matches.empty())
        return;

    if(cursor > 0)
    {
        cursor--;
        if(cursor < offset)
            offset = cursor;
    }
    prewarmAroundCursor(editor);
}

void FuzzyFindMode::halfPageDown(Editor& editor)
{
    if(matches.empty())
        return;

    int half = fuzzyFindVisibleRows(editor, filenameFirst) / 2;
    cursor += half;
    if(cursor >= (int)matches.size())
        cursor = (int)matches.size() - 1;
    int visible = fuzzyFindVisibleRows(editor, filenameFirst);
    if(cursor >= offset + visible)
        offset = cursor - visible + 1;
    prewarmAroundCursor(editor);
}

void FuzzyFindMode::halfPageUp(Editor& editor)
{
    if(matches.empty())
        return;

    int half = fuzzyFindVisibleRows(editor, filenameFirst) / 2;
    cursor -= half;
    if(cursor < 0)
        cursor = 0;
    if(cursor < offset)
        offset = cursor;
    prewarmAroundCursor(editor);
}

void FuzzyFindMode::addChar(Editor& editor, char c)
{
    query += c;
    updateMatches(editor);
    cursor = 0;
    offset = 0;
    prewarmAroundCursor(editor);
}

void FuzzyFindMode::backspace(Editor& editor)
{
    if(!query.empty())
    {
        query.pop_back();
        updateMatches(editor);
        cursor = 0;
        offset = 0;
        prewarmAroundCursor(editor);
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
    prewarmAroundCursor(editor);
}

void FuzzyFindMode::clearQuery(Editor& editor)
{
    query.clear();
    updateMatches(editor);
    cursor = 0;
    offset = 0;
    prewarmAroundCursor(editor);
}

void FuzzyFindMode::toggleFilenameFirst(Editor& editor)
{
    filenameFirst = !filenameFirst;
    updateMatches(editor);
    cursor = 0;
    offset = 0;
    prewarmAroundCursor(editor);
    editor.setStatusMessage(filenameFirst ? "Fuzzy: filename first"
                                          : "Fuzzy: path first");
}

void FuzzyFindMode::toggleGitignore(Editor& editor)
{
    if(editor.gitignoreLockedOff)
        return;
    editor.respectGitignore = !editor.respectGitignore;
    editor.fuzzyProjectFiles.clear();
    editor.fuzzyFileIndexInitialized = false;
    projectFilesInitialized = false;
    initializeFiles(editor);
    query.clear();
    cursor = 0;
    offset = 0;
    updateMatches(editor);
    prewarmAroundCursor(editor);
}

void FuzzyFindMode::refreshFileIndex(Editor& editor)
{
    editor.fuzzyProjectFiles.clear();
    editor.fuzzyFileIndexInitialized = false;
    projectFilesInitialized = false;
    initializeFiles(editor);
    cursor = 0;
    offset = 0;
    selectedFiles.clear();
    updateMatches(editor);
    prewarmAroundCursor(editor);
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

void FuzzyFindMode::prewarmAroundCursor(Editor& editor) const
{
    if(cursor < 0 || cursor >= (int)matches.size())
        return;

    std::vector<std::string> paths;
    paths.reserve(21);
    paths.push_back(matches[cursor].file.path);
    for(int distance = 1; distance <= 10; ++distance)
    {
        const int next = cursor + distance;
        if(next < (int)matches.size())
            paths.push_back(matches[next].file.path);
        const int prev = cursor - distance;
        if(prev >= 0)
            paths.push_back(matches[prev].file.path);
    }
    editor.prewarmColdOpenFiles(paths);
}

bool FuzzyFindMode::select(Editor& editor)
{
    if(cursor < 0 || cursor >= (int)matches.size())
        return false;

    prewarmAroundCursor(editor);
    const FuzzyMatch& match = matches[cursor];
    editor.openFile(std::string_view(match.file.path));
#ifdef _WIN32
    Terminal::discardPendingInput();
#endif
    return true;
}

bool FuzzyFindMode::openSelected(Editor& editor)
{
    if(selectedFiles.empty())
        return false;

    std::vector<std::string> paths(selectedFiles.begin(), selectedFiles.end());
    std::sort(paths.begin(), paths.end());
    editor.prewarmColdOpenFiles(paths);
    for(size_t i = 0; i < paths.size(); ++i)
    {
        bool notifyLsp = (i + 1 == paths.size());
        editor.openFile(std::string_view(paths[i]), notifyLsp);
    }
#ifdef _WIN32
    Terminal::discardPendingInput();
#endif
    selectedFiles.clear();
    return true;
}
} // namespace editor::statemachine
