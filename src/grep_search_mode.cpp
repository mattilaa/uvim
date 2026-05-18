#include "ascii.h"
#include "constants.h"
#include "editor.h"
#include "editor_utils.h"
#include "gitignore.h"
#include "header_help.h"
#include "mode_state_machine.h"
#include "process_pipe.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits.h>

namespace
{
std::vector<std::string> grepSearchHelpTokens()
{
    return {"[Enter: open]", "[Esc: cancel]", "[Ctrl+N: select]",
            "[" + ascii::utf8(ascii::UP_DOWN_ARROWS) + ": navigate]",
            "[Ctrl+I: gitignore]"};
}

int grepSearchHeaderRows(int screenCols)
{
    return 2 + HeaderHelp::lineCount(grepSearchHelpTokens(), screenCols);
}

int grepSearchVisibleRows(const Editor& editor)
{
    return std::max(1, editor.screenRows -
                           grepSearchHeaderRows(editor.screenCols));
}

std::string toLower(std::string_view input)
{
    std::string out(input);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
    return out;
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
        if(j == std::string::npos)
            j = s.size();
        if(j > i)
            out.push_back(s.substr(i, j - i));
        i = j + 1;
    }
    return out;
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
            suffix.erase(suffix.begin());
        else
            suffix.erase(0, slash + 1);
    }
    return prefix + suffix;
}

std::string singleLinePasteText(std::string text)
{
    text.erase(std::remove_if(text.begin(), text.end(),
                              [](char ch) { return ch == '\n' || ch == '\r'; }),
               text.end());
    return text;
}
} // namespace

// ============================================================================
// GrepSearchMode Implementation
// ============================================================================

void GrepSearchMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    initialize(*ed);
    ed->needsFullRedraw = true;

    // Set cursor to bar for input
    Terminal::setCursorBarBlinking();
}

void GrepSearchMode::on_exit(ModeContext& /* ctx */)
{
    // Restore block cursor
    Terminal::setCursorBlock();
}

std::optional<ModeState> GrepSearchMode::handle(ModeContext& ctx, int key)
{
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    // ========================================================================
    // Exit
    // ========================================================================

    if(c == keyCode(control::ControlKey::ESC))
    {
        return defaultExitMode(ed);
    }

    // ========================================================================
    // Selection
    // ========================================================================

    if(c == keyCode(control::ControlKey::ENTER))
    {
        if(!selectedMatches.empty() ? openSelected(*ed) : selectMatch(*ed))
        {
            return defaultExitMode(ed);
        }
        return std::nullopt;
    }

    // ========================================================================
    // Navigation through results
    // ========================================================================

    if(c == keyCode(control::ControlKey::CTRL_N))
    {
        toggleSelection();
    }
    else if(c == keyCode(control::ControlKey::CTRL_J) ||
            c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        resultDown(*ed);
    }
    else if(c == keyCode(control::ControlKey::CTRL_P) ||
            c == keyCode(control::ControlKey::CTRL_K) ||
            c == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        resultUp(*ed);
    }
    else if(c == keyCode(control::ControlKey::CTRL_D) ||
            c == keyCode(navigation::NavigationKey::PAGE_DOWN))
    {
        resultHalfPageDown(*ed);
    }
    else if(c == keyCode(control::ControlKey::CTRL_U) ||
            c == keyCode(navigation::NavigationKey::PAGE_UP))
    {
        resultHalfPageUp(*ed);
    }

    // ========================================================================
    // Input Editing
    // ========================================================================

    else if(c == keyCode(control::ControlKey::BACKSPACE) || c == 127 ||
            c == keyCode(control::ControlKey::CTRL_H))
    {
        searchBackspace(*ed);
    }
    else if(c == keyCode(control::ControlKey::CTRL_W))
    {
        // Delete word backward
        searchDeleteWord(*ed);
    }
    else if(c == keyCode(control::ControlKey::PASTE))
    {
        std::string text = singleLinePasteText(Terminal::takeLastPasteText());
        if(!text.empty())
        {
            query += text;
            performSearch(*ed);
            cursor = 0;
            offset = 0;
        }
    }
    // ========================================================================
    // Toggles
    // ========================================================================

    else if(c == keyCode(control::ControlKey::CTRL_I))
    {
        toggleGitignore(*ed);
    }
    else if(c == keyCode(control::ControlKey::TAB))
    {
        togglePreview();
    }

    // ========================================================================
    // Mode Switching
    // ========================================================================

    else if(c == keyCode(control::ControlKey::CTRL_B))
    {
        return BufferBrowserMode{};
    }

    // ========================================================================
    // Character Input
    // ========================================================================

    else if(c >= 32 && c < 127)
    {
        searchAddChar(*ed, static_cast<char>(c));
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}

void GrepSearchMode::draw(Editor& editor) const
{
    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

    output += Terminal::ESC_CURSOR_HOME;
    output += editor.theme.reset();
    output += Terminal::ESC_CLEAR_LINE;

    output += Terminal::ESC_BOLD;
    output += "  Grep: ";
    output += editor.theme.reset();
    output += editor.theme.uiPrompt();
    output += query;

    output += Terminal::ESC_BLINK;
    output += "_";
    output += Terminal::ESC_BLINK_OFF;
    output += editor.theme.baseFg();

    if(searching)
    {
        output += editor.theme.uiWarning();
        output += " (searching...)";
        output += editor.theme.baseFg();
    }

    HeaderHelp::append(output, editor.theme, editor.screenCols,
                       grepSearchHelpTokens());

    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.uiDim();
    if(!matches.empty())
    {
        output += "  " + std::to_string(matches.size());
        if(matches.size() >= 1000)
            output += "+ matches (limited)";
        else
            output += " matches";
    }
    else if(!query.empty() && !searching)
    {
        output += "  No matches";
    }
    if(editor.respectGitignore)
    {
        output += " [gitignore]";
    }
    if(!selectedMatches.empty())
    {
        output += " (" + std::to_string(selectedMatches.size()) + " selected)";
    }
    output += editor.theme.baseFg();

    int availableRows = grepSearchVisibleRows(editor);

    for(int i = 0; i < availableRows && i + offset < (int)matches.size(); i++)
    {
        output += Terminal::NEWLINE_CLEAR;

        int index = i + offset;
        const GrepMatch& match = matches[index];
        bool isSelected = selectedMatches.count(index) > 0;

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

        output += editor.theme.uiInfo();
        std::string displayName = match.filepath;
        std::error_code cwdEc;
        auto cwd = std::filesystem::current_path(cwdEc);
        if(!cwdEc)
        {
            const std::string cwdStr = cwd.string();
            if(displayName.find(cwdStr) == 0)
                displayName = displayName.substr(cwdStr.length() + 1);
        }

        const std::string lineNumber = std::to_string(match.lineNumber);
        const int rowPrefixWidth = 2;
        const int separatorsWidth = 3; // ":" + ": "
        const int minContentWidth =
            std::min(20, std::max(0, editor.screenCols / 3));
        int pathWidth = editor.screenCols - rowPrefixWidth - separatorsWidth -
                        (int)lineNumber.length() - minContentWidth;
        if(pathWidth < 8)
            pathWidth =
                std::max(1, editor.screenCols - rowPrefixWidth -
                                separatorsWidth - (int)lineNumber.length());

        displayName = truncatePathMiddle(displayName, pathWidth);
        output += displayName;
        output += editor.theme.baseFg();

        output += ":";
        output += editor.theme.uiWarning();
        output += lineNumber;
        output += editor.theme.baseFg();
        output += ": ";

        std::string content = match.lineContent;
        int maxContentLen = editor.screenCols - rowPrefixWidth -
                            text_utils::utf8DisplayWidth(displayName) -
                            separatorsWidth - (int)lineNumber.length();
        if(maxContentLen < 0)
            maxContentLen = 0;

        if(text_utils::utf8DisplayWidth(content) > maxContentLen)
        {
            if(maxContentLen <= 3)
                content = std::string(std::max(0, maxContentLen), '.');
            else
            {
                while(!content.empty() &&
                      text_utils::utf8DisplayWidth(content) > maxContentLen - 3)
                    content.pop_back();
                content += "...";
            }
        }

        if(!match.highlightRanges.empty() && index != cursor)
        {
            std::string lowerContent = toLower(content);
            std::string lowerQuery = toLower(query);

            size_t pos = lowerContent.find(lowerQuery);
            if(pos != std::string::npos)
            {
                output += content.substr(0, pos);
                output += editor.theme.matchHighlight();
                output += content.substr(pos, query.length());
                output += editor.theme.baseFg();
                output += content.substr(pos + query.length());
            }
            else
            {
                output += content;
            }
        }
        else
        {
            output += content;
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

void GrepSearchMode::loadFileIndex(Editor& editor)
{
    if(!editor.grepFileIndexInitialized)
    {
        editor.grepProjectFiles.clear();
        std::error_code cwdEc;
        auto cwd = std::filesystem::current_path(cwdEc);
        if(!cwdEc)
        {
            const std::string cwdStr = cwd.string();
            if(editor.useGitFileIndex)
            {
                std::string repoRoot = runCmd(
                    {"git", "-C", cwdStr, "rev-parse", "--show-toplevel"});

                if(!repoRoot.empty())
                {
                    const std::string raw =
                        runCmd({"git", "-C", cwdStr, "ls-files", "-z",
                                "--cached", "--others", "--exclude-standard"});
                    const auto relPaths = splitNul(raw);

                    for(const auto& relPath : relPaths)
                    {
                        if(relPath.empty())
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
                        entry.size = (uintmax_t)std::filesystem::file_size(
                            fullPath, szEc);
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
                        editor.grepProjectFiles.push_back(std::move(entry));
                    }
                }
            }

            if(editor.grepProjectFiles.empty())
            {
                GitIgnore gitignore;
                if(editor.respectGitignore)
                {
                    gitignore.loadRecursive(cwd);
                }
                editor::helper::collectProjectFileEntries(
                    cwdStr, 0, gitignore, editor.grepProjectFiles);
            }
        }
        editor.grepFileIndexInitialized = true;
    }
}

void GrepSearchMode::initialize(Editor& editor)
{
    loadFileIndex(editor);
    searchClear();
    searching = false;
}

void GrepSearchMode::refreshFileIndex(Editor& editor)
{
    editor.grepFileIndexInitialized = false;
    editor.grepProjectFiles.clear();
    loadFileIndex(editor);
    cursor = 0;
    offset = 0;
    selectedMatches.clear();
    if(query.empty())
    {
        matches.clear();
        searching = false;
        return;
    }
    performSearch(editor);
}

void GrepSearchMode::performSearch(Editor& editor)
{
    matches.clear();
    selectedMatches.clear();
    searching = true;

    if(query.empty())
    {
        searching = false;
        return;
    }

    for(const auto& file : editor.grepProjectFiles)
    {
        if(file.isDirectory)
            continue;

        searchInFile(file.path, query);

        if(matches.size() >= 1000)
            break;
    }

    searching = false;

    if(cursor >= (int)matches.size())
    {
        cursor = 0;
        offset = 0;
    }
}

void GrepSearchMode::searchInFile(const std::string& filepath,
                                  std::string_view needle)
{
    if(needle.empty())
        return;

    if(!isTextFile(filepath))
        return;

    std::ifstream file(filepath);
    if(!file)
        return;

    std::string line;
    int lineNumber = 0;

    std::string loweredNeedle;
    std::string_view searchNeedle = needle;
    if(!caseSensitive)
    {
        loweredNeedle = toLower(needle);
        searchNeedle = loweredNeedle;
    }

    while(std::getline(file, line))
    {
        lineNumber++;

        std::string_view haystack = line;
        std::string loweredLine;
        if(!caseSensitive)
        {
            loweredLine = toLower(line);
            haystack = loweredLine;
        }

        size_t pos = 0;
        while((pos = haystack.find(searchNeedle, pos)) != std::string::npos)
        {
            GrepMatch match;
            match.filepath = filepath;

            size_t lastSlash = filepath.find_last_of("/\\");
            match.filename = (lastSlash != std::string::npos)
                                 ? filepath.substr(lastSlash + 1)
                                 : filepath;

            match.lineNumber = lineNumber;
            match.lineContent = trimString(line);
            match.highlightRanges.push_back(
                std::make_pair((int)pos, (int)needle.length()));

            matches.push_back(match);
            pos += needle.length();

            if(matches.size() >= 1000)
                return;
        }
    }
}

bool GrepSearchMode::isTextFile(const std::string& filepath) const
{
    std::string ext;
    size_t dotPos =
        filepath.find_last_of(keyCode(command::CommandKey::KEY_DOT));
    if(dotPos != std::string::npos)
    {
        ext = filepath.substr(dotPos);
        bool isPythonExt =
            constants::is_filetype<constants::no_pattern,
                                   constants::python_suffixes>(filepath);
        bool isMlaExt =
            constants::is_filetype<constants::no_pattern,
                                   constants::mla_suffixes>(filepath);

        if(ext == ".txt" || ext == ".cpp" || ext == ".c" || ext == ".h" ||
           ext == ".hpp" || isPythonExt || ext == ".js" || ext == ".ts" ||
           ext == ".jsx" || ext == ".tsx" || ext == ".java" || ext == ".rs" ||
           ext == ".go" || ext == ".rb" || ext == ".php" || ext == ".sh" ||
           ext == ".bash" || ext == ".zsh" || ext == ".vim" || ext == ".lua" ||
           ext == ".md" || ext == ".markdown" || ext == ".rst" ||
           ext == ".tex" || ext == ".css" || ext == ".scss" || ext == ".html" ||
           ext == ".xml" || ext == ".json" || ext == ".yaml" || ext == ".yml" ||
           ext == ".toml" || ext == ".ini" || ext == ".conf" ||
           ext == ".config" || ext == ".log" || ext == ".cmake" ||
           ext == ".make" || ext == ".mk" || ext == ".am" || isMlaExt)
        {
            return true;
        }

        if(ext == ".exe" || ext == ".o" || ext == ".so" || ext == ".a" ||
           ext == ".dll" || ext == ".dylib" || ext == ".bin" || ext == ".dat" ||
           ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif" ||
           ext == ".bmp" || ext == ".ico" || ext == ".pdf" || ext == ".doc" ||
           ext == ".docx" || ext == ".xls" || ext == ".xlsx" || ext == ".ppt" ||
           ext == ".pptx" || ext == ".zip" || ext == ".tar" || ext == ".gz" ||
           ext == ".bz2" || ext == ".7z" || ext == ".rar" || ext == ".mp3" ||
           ext == ".mp4" || ext == ".avi" || ext == ".mov" || ext == ".wav" ||
           ext == ".flac" || ext == ".ogg" || ext == ".ttf" || ext == ".otf" ||
           ext == ".woff" || ext == ".woff2" || ext == ".eot")
        {
            return false;
        }
    }

    return !isBinaryFile(filepath);
}

bool GrepSearchMode::isBinaryFile(const std::string& filepath) const
{
    std::ifstream file(filepath, std::ios::binary);
    if(!file)
        return true;

    char buffer[512];
    file.read(buffer, sizeof(buffer));
    std::streamsize bytesRead = file.gcount();

    int nullCount = 0;
    int nonPrintable = 0;

    for(std::streamsize i = 0; i < bytesRead; i++)
    {
        unsigned char c = static_cast<unsigned char>(buffer[i]);

        if(c == 0)
        {
            nullCount++;
            if(nullCount > 1)
                return true;
        }

        if(c < 32 && c != '\n' && c != '\r' && c != '\t' && c != '\f')
        {
            nonPrintable++;
        }
    }

    double nonPrintableRatio =
        bytesRead > 0 ? (double)nonPrintable / bytesRead : 0;
    return nonPrintableRatio > 0.3;
}

std::string GrepSearchMode::trimString(const std::string& str) const
{
    size_t first = str.find_first_not_of(" \t\r\n");
    if(first == std::string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

bool GrepSearchMode::selectMatch(Editor& editor)
{
    if(cursor < 0 || cursor >= (int)matches.size())
        return false;

    const GrepMatch& match = matches[cursor];

    editor.openFile(std::string_view(match.filepath));

    *editor.cursorY = match.lineNumber - 1;
    if(*editor.cursorY >= (int)editor.lines->size())
        *editor.cursorY = editor.lines->size() - 1;
    if(*editor.cursorY < 0)
        *editor.cursorY = 0;

    *editor.cursorX = 0;

    editor.searchQuery = query;
    editor.findAllMatches();
    editor.centerScreen();

    return true;
}

void GrepSearchMode::resultUp(Editor& editor)
{
    if(cursor > 0)
    {
        cursor--;
        if(cursor < offset)
            offset = cursor;
    }
}

void GrepSearchMode::resultDown(Editor& editor)
{
    if(cursor < (int)matches.size() - 1)
    {
        cursor++;
        int visible = grepSearchVisibleRows(editor);
        if(cursor >= offset + visible)
            offset = cursor - visible + 1;
    }
}

void GrepSearchMode::resultHalfPageUp(Editor& editor)
{
    int half = grepSearchVisibleRows(editor) / 2;
    cursor -= half;
    if(cursor < 0)
        cursor = 0;
    if(cursor < offset)
        offset = cursor;
}

void GrepSearchMode::resultHalfPageDown(Editor& editor)
{
    int half = grepSearchVisibleRows(editor) / 2;
    cursor += half;
    if(cursor >= (int)matches.size())
        cursor = matches.size() - 1;
    int visible = grepSearchVisibleRows(editor);
    if(cursor >= offset + visible)
        offset = cursor - visible + 1;
}

void GrepSearchMode::searchAddChar(Editor& editor, char c)
{
    query += c;
    performSearch(editor);
    cursor = 0;
    offset = 0;
}

void GrepSearchMode::searchBackspace(Editor& editor)
{
    if(!query.empty())
    {
        query.pop_back();
        performSearch(editor);
        cursor = 0;
        offset = 0;
    }
}

void GrepSearchMode::searchDeleteWord(Editor& editor)
{
    while(!query.empty() && query.back() == keyCode(control::ControlKey::SPACE))
        query.pop_back();
    while(!query.empty() && query.back() != keyCode(control::ControlKey::SPACE))
        query.pop_back();
    performSearch(editor);
    cursor = 0;
    offset = 0;
}

void GrepSearchMode::searchClear()
{
    query.clear();
    matches.clear();
    selectedMatches.clear();
    cursor = 0;
    offset = 0;
}

void GrepSearchMode::toggleGitignore(Editor& editor)
{
    if(editor.gitignoreLockedOff)
        return;
    editor.respectGitignore = !editor.respectGitignore;
    editor.grepFileIndexInitialized = false;
    initialize(editor);
}

void GrepSearchMode::togglePreview()
{
    previewEnabled = !previewEnabled;
}

void GrepSearchMode::toggleSelection()
{
    if(cursor < 0 || cursor >= (int)matches.size())
        return;

    auto it = selectedMatches.find(cursor);
    if(it != selectedMatches.end())
        selectedMatches.erase(it);
    else
        selectedMatches.insert(cursor);
}

bool GrepSearchMode::openSelected(Editor& editor)
{
    if(selectedMatches.empty())
        return false;

    std::vector<int> indexes;
    indexes.reserve(selectedMatches.size());
    for(int index : selectedMatches)
    {
        if(index >= 0 && index < (int)matches.size())
            indexes.push_back(index);
    }
    if(indexes.empty())
        return false;

    std::sort(indexes.begin(), indexes.end());
    std::vector<std::string> openedPaths;
    openedPaths.reserve(indexes.size());
    for(int index : indexes)
    {
        const std::string& path = matches[index].filepath;
        if(std::find(openedPaths.begin(), openedPaths.end(), path) ==
           openedPaths.end())
            openedPaths.push_back(path);
    }

    GrepMatch finalMatch = matches[indexes.back()];
    for(auto it = openedPaths.begin(); it != openedPaths.end();)
    {
        if(*it == finalMatch.filepath)
            it = openedPaths.erase(it);
        else
            ++it;
    }
    openedPaths.push_back(finalMatch.filepath);

    for(size_t i = 0; i < openedPaths.size(); ++i)
    {
        bool notifyLsp = (i + 1 == openedPaths.size());
        editor.openFile(std::string_view(openedPaths[i]), notifyLsp);
    }

    *editor.cursorY = finalMatch.lineNumber - 1;
    if(*editor.cursorY >= (int)editor.lines->size())
        *editor.cursorY = editor.lines->size() - 1;
    if(*editor.cursorY < 0)
        *editor.cursorY = 0;
    *editor.cursorX = 0;
    editor.searchQuery = query;
    editor.findAllMatches();
    editor.centerScreen();

    selectedMatches.clear();
    return true;
}
