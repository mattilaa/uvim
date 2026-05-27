#include "regex_search_mode.h"
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
#include <chrono>
#include <filesystem>
#include <fstream>

namespace editor::statemachine
{
namespace
{
std::vector<std::string> regexSearchHelpTokens(bool allFiles)
{
    return {"[Enter: open]", "[Esc: cancel]",
            std::string("[Ctrl+S: ") + (allFiles ? "buffer]" : "all files]"),
            "[Ctrl+J/K: navigate]", "[Ctrl+W: delete word]"};
}

int regexSearchHeaderRows(int screenCols, bool allFiles)
{
    return 2 +
           HeaderHelp::lineCount(regexSearchHelpTokens(allFiles), screenCols);
}

int regexSearchVisibleRows(const Editor& editor, bool allFiles)
{
    return std::max(1, editor.screenRows -
                           regexSearchHeaderRows(editor.screenCols, allFiles));
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

std::string displayPath(const std::string& path)
{
    std::error_code cwdEc;
    auto cwd = std::filesystem::current_path(cwdEc);
    if(cwdEc)
        return path;

    const std::string cwdStr = cwd.string();
    if(path.rfind(cwdStr + "/", 0) == 0)
        return path.substr(cwdStr.size() + 1);
    return path;
}

std::string truncateDisplay(std::string text, int width)
{
    if(width <= 0)
        return "";
    if(text_utils::utf8DisplayWidth(text) <= width)
        return text;
    if(width <= 3)
        return std::string(width, '.');

    while(!text.empty() && text_utils::utf8DisplayWidth(text) > width - 3)
        text.pop_back();
    text += "...";
    return text;
}
} // namespace

void RegexSearchMode::on_enter(ModeContext& ctx)
{
    initialize(*ctx.editor);
    ctx.requestFullRedraw();
    Terminal::setCursorBarBlinking();
}

void RegexSearchMode::on_exit(ModeContext& /* ctx */)
{
    Terminal::setCursorBlock();
}

std::optional<ModeState> RegexSearchMode::handle(ModeContext& ctx,
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
        if(select(*ed))
            return defaultExitMode(ed);
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::CTRL_S))
    {
        toggleScope(*ed);
    }
    else if(c == keyCode(control::ControlKey::CTRL_J) ||
            c == keyCode(control::ControlKey::CTRL_N) ||
            c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        moveDown(*ed);
    }
    else if(c == keyCode(control::ControlKey::CTRL_K) ||
            c == keyCode(control::ControlKey::CTRL_P) ||
            c == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        moveUp(*ed);
    }
    else if(c == keyCode(control::ControlKey::CTRL_D) ||
            c == keyCode(navigation::NavigationKey::PAGE_DOWN))
    {
        halfPageDown(*ed);
    }
    else if(c == keyCode(control::ControlKey::CTRL_U) ||
            c == keyCode(navigation::NavigationKey::PAGE_UP))
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
    else if(c >= 32 && c < 127)
    {
        addChar(*ed, static_cast<char>(c));
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}

void RegexSearchMode::draw(Editor& editor) const
{
    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

    output += Terminal::ESC_CURSOR_HOME;
    output += editor.theme.reset();
    output += Terminal::ESC_CLEAR_LINE;

    output += Terminal::ESC_BOLD;
    output += allFiles ? "  Regex Files: " : "  Regex Buffer: ";
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
                       regexSearchHelpTokens(allFiles));

    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.uiDim();
    if(!regexError.empty())
    {
        output += "  Invalid regex: " + regexError;
    }
    else if(!matches.empty())
    {
        output += "  " + std::to_string(matches.size());
        output += matches.size() >= 1000 ? "+ matches (limited)" : " matches";
    }
    else if(!query.empty())
    {
        output += "  No matches";
    }
    else
    {
        output += allFiles ? "  Type a regex to search project files"
                           : "  Type a regex to search the current buffer";
    }
    if(allFiles && editor.respectGitignore)
        output += " [gitignore]";
    output += editor.theme.baseFg();

    int availableRows = regexSearchVisibleRows(editor, allFiles);
    for(int i = 0; i < availableRows && i + offset < (int)matches.size(); ++i)
    {
        output += Terminal::NEWLINE_CLEAR;
        int index = i + offset;
        const auto& match = matches[index];

        if(index == cursor)
            output += editor.theme.selection();

        output += "  ";
        output += editor.theme.uiInfo();
        std::string path = displayPath(match.filepath);
        const std::string lineNumber = std::to_string(match.lineNumber);
        int reserved = 2 + 3 + (int)lineNumber.size() +
                       std::max(16, editor.screenCols / 3);
        int pathWidth = std::max(1, editor.screenCols - reserved);
        path = truncateDisplay(path, pathWidth);
        output += path;
        output += editor.theme.baseFg();
        output += ":";
        output += editor.theme.uiWarning();
        output += lineNumber;
        output += editor.theme.baseFg();
        output += ": ";

        int contentWidth = editor.screenCols - 2 -
                           text_utils::utf8DisplayWidth(path) - 3 -
                           (int)lineNumber.size();
        std::string content = truncateDisplay(match.lineContent, contentWidth);
        if(!match.highlightRanges.empty())
        {
            int start = match.highlightRanges.front().first;
            int len = match.highlightRanges.front().second;
            if(start >= 0 && len > 0 &&
               start + len <= static_cast<int>(content.size()))
            {
                output += content.substr(0, start);
                output += editor.theme.matchHighlight();
                output += content.substr(start, len);
                output += editor.theme.baseFg();
                output += content.substr(start + len);
            }
            else
                output += content;
        }
        else
            output += content;

        output += editor.theme.reset();
    }

    for(int i = (int)matches.size() - offset; i < availableRows; ++i)
    {
        output += Terminal::NEWLINE_CLEAR;
        output += editor.theme.uiGutter();
        output += "~";
        output += editor.theme.baseFg();
    }

    Terminal::write(output);
    Terminal::flush();
}

void RegexSearchMode::loadFileIndex(Editor& editor)
{
    if(editor.grepFileIndexInitialized)
        return;

    editor.grepProjectFiles.clear();
    std::error_code cwdEc;
    auto cwd = std::filesystem::current_path(cwdEc);
    if(cwdEc)
    {
        editor.grepFileIndexInitialized = true;
        return;
    }

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
            for(const auto& relPath : splitNul(raw))
            {
                if(relPath.empty())
                    continue;
                const std::string fullPath = cwdStr + "/" + relPath;
                std::error_code stEc;
                auto status = std::filesystem::status(fullPath, stEc);
                if(stEc || std::filesystem::is_directory(status))
                    continue;

                FileEntry entry;
                entry.name = text_utils::basename(relPath);
                entry.path = relPath;
                entry.isDirectory = false;
                editor.grepProjectFiles.push_back(std::move(entry));
            }
        }
    }

    if(editor.grepProjectFiles.empty())
    {
        GitIgnore gitignore;
        if(editor.respectGitignore)
            gitignore.loadRecursive(cwd);
        editor::helper::collectProjectFileEntries(cwdStr, 0, gitignore,
                                                  editor.grepProjectFiles);
    }

    editor.grepFileIndexInitialized = true;
}

void RegexSearchMode::initialize(Editor& editor)
{
    loadFileIndex(editor);
    clearQuery();
    searching = false;
}

void RegexSearchMode::refreshFileIndex(Editor& editor)
{
    editor.grepFileIndexInitialized = false;
    editor.grepProjectFiles.clear();
    loadFileIndex(editor);
    performSearch(editor);
}

void RegexSearchMode::performSearch(Editor& editor)
{
    matches.clear();
    regexError.clear();
    cursor = 0;
    offset = 0;

    if(query.empty())
        return;

    std::regex pattern;
    try
    {
        pattern = std::regex(query, std::regex_constants::icase);
    }
    catch(const std::regex_error& e)
    {
        regexError = e.what();
        return;
    }

    searching = true;
    if(allFiles)
    {
        loadFileIndex(editor);
        for(const auto& file : editor.grepProjectFiles)
        {
            if(file.isDirectory)
                continue;
            searchInFile(file.path, pattern);
            if(matches.size() >= 1000)
                break;
        }
    }
    else
    {
        searchCurrentBuffer(editor, pattern);
    }
    searching = false;
}

void RegexSearchMode::searchCurrentBuffer(Editor& editor,
                                          const std::regex& pattern)
{
    if(!editor.lines)
        return;

    std::string path = editor.filename ? *editor.filename : "[No Name]";
    for(int i = 0; i < static_cast<int>(editor.lines->size()); ++i)
        addLineMatches(path, i + 1, (*editor.lines)[i], pattern);
}

void RegexSearchMode::searchInFile(const std::string& filepath,
                                   const std::regex& pattern)
{
    if(!isTextFile(filepath))
        return;

    std::ifstream file(filepath);
    if(!file)
        return;

    std::string line;
    int lineNumber = 0;
    while(std::getline(file, line))
    {
        ++lineNumber;
        addLineMatches(filepath, lineNumber, line, pattern);
        if(matches.size() >= 1000)
            return;
    }
}

bool RegexSearchMode::isTextFile(const std::string& filepath) const
{
    std::string ext;
    size_t dotPos = filepath.find_last_of('.');
    if(text_utils::is_found(dotPos))
    {
        ext = filepath.substr(dotPos);
        const bool isPython =
            constants::is_filetype<constants::no_pattern,
                                   constants::python_suffixes>(filepath);
        const bool isMla =
            constants::is_filetype<constants::no_pattern,
                                   constants::mla_suffixes>(filepath);

        if(ext == ".txt" || ext == ".cpp" || ext == ".c" || ext == ".h" ||
           ext == ".hpp" || isPython || ext == ".js" || ext == ".ts" ||
           ext == ".jsx" || ext == ".tsx" || ext == ".java" || ext == ".rs" ||
           ext == ".go" || ext == ".rb" || ext == ".php" || ext == ".sh" ||
           ext == ".bash" || ext == ".zsh" || ext == ".vim" || ext == ".lua" ||
           ext == ".md" || ext == ".markdown" || ext == ".rst" ||
           ext == ".tex" || ext == ".css" || ext == ".scss" || ext == ".html" ||
           ext == ".xml" || ext == ".json" || ext == ".yaml" || ext == ".yml" ||
           ext == ".toml" || ext == ".ini" || ext == ".conf" ||
           ext == ".config" || ext == ".log" || ext == ".cmake" ||
           ext == ".make" || ext == ".mk" || ext == ".am" || isMla)
            return true;

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
            return false;
    }

    return !isBinaryFile(filepath);
}

bool RegexSearchMode::isBinaryFile(const std::string& filepath) const
{
    std::ifstream file(filepath, std::ios::binary);
    if(!file)
        return true;

    char buffer[512];
    file.read(buffer, sizeof(buffer));
    std::streamsize bytesRead = file.gcount();

    int nullCount = 0;
    int nonPrintable = 0;
    for(std::streamsize i = 0; i < bytesRead; ++i)
    {
        unsigned char c = static_cast<unsigned char>(buffer[i]);
        if(c == 0 && ++nullCount > 1)
            return true;
        if(c < 32 && c != '\n' && c != '\r' && c != '\t' && c != '\f')
            ++nonPrintable;
    }

    double ratio = bytesRead > 0 ? (double)nonPrintable / bytesRead : 0;
    return ratio > 0.3;
}

void RegexSearchMode::addLineMatches(const std::string& filepath,
                                     int lineNumber, const std::string& line,
                                     const std::regex& pattern)
{
    auto start = line.cbegin();
    while(start != line.cend())
    {
        std::smatch match;
        if(!std::regex_search(start, line.cend(), match, pattern))
            break;

        int col = static_cast<int>(std::distance(line.cbegin(), start)) +
                  static_cast<int>(match.position());
        int len = static_cast<int>(match.length());
        if(len > 0)
        {
            RegexSearchMatch entry;
            entry.filename = text_utils::basename(filepath);
            entry.filepath = filepath;
            entry.lineNumber = lineNumber;
            entry.lineContent = line;
            entry.matchText = match.str();
            entry.highlightRanges.push_back({col, len});
            matches.push_back(std::move(entry));
        }

        if(len == 0)
        {
            if(col >= static_cast<int>(line.size()))
                break;
            start = line.cbegin() + col + 1;
        }
        else
        {
            start = line.cbegin() + col + len;
        }

        if(matches.size() >= 1000)
            return;
    }
}

void RegexSearchMode::moveDown(Editor& editor)
{
    if(cursor < (int)matches.size() - 1)
    {
        ++cursor;
        int visible = regexSearchVisibleRows(editor, allFiles);
        if(cursor >= offset + visible)
            offset = cursor - visible + 1;
    }
}

void RegexSearchMode::moveUp(Editor& /* editor */)
{
    if(cursor > 0)
    {
        --cursor;
        if(cursor < offset)
            offset = cursor;
    }
}

void RegexSearchMode::halfPageDown(Editor& editor)
{
    int half = std::max(1, regexSearchVisibleRows(editor, allFiles) / 2);
    cursor = std::min(cursor + half, std::max(0, (int)matches.size() - 1));
    int visible = regexSearchVisibleRows(editor, allFiles);
    if(cursor >= offset + visible)
        offset = cursor - visible + 1;
}

void RegexSearchMode::halfPageUp(Editor& editor)
{
    int half = std::max(1, regexSearchVisibleRows(editor, allFiles) / 2);
    cursor = std::max(0, cursor - half);
    if(cursor < offset)
        offset = cursor;
}

void RegexSearchMode::addChar(Editor& editor, char c)
{
    query += c;
    performSearch(editor);
}

void RegexSearchMode::backspace(Editor& editor)
{
    if(query.empty())
        return;
    query.pop_back();
    performSearch(editor);
}

void RegexSearchMode::deleteWord(Editor& editor)
{
    while(!query.empty() && query.back() == keyCode(control::ControlKey::SPACE))
        query.pop_back();
    while(!query.empty() && query.back() != keyCode(control::ControlKey::SPACE))
        query.pop_back();
    performSearch(editor);
}

void RegexSearchMode::clearQuery()
{
    query.clear();
    regexError.clear();
    matches.clear();
    cursor = 0;
    offset = 0;
}

void RegexSearchMode::toggleScope(Editor& editor)
{
    allFiles = !allFiles;
    performSearch(editor);
}

bool RegexSearchMode::select(Editor& editor)
{
    if(cursor < 0 || cursor >= (int)matches.size())
        return false;

    const auto match = matches[cursor];
    if(allFiles)
        editor.openFile(std::string_view(match.filepath));

    if(!editor.lines || editor.lines->empty())
        return true;

    *editor.cursorY =
        std::clamp(match.lineNumber - 1, 0, (int)editor.lines->size() - 1);
    int col =
        match.highlightRanges.empty() ? 0 : match.highlightRanges[0].first;
    *editor.cursorX =
        std::clamp(col, 0, (int)(*editor.lines)[*editor.cursorY].size());

    editor.searchQuery = query;
    editor.findAllMatches();
    editor.centerScreen();
    return true;
}
} // namespace editor::statemachine
