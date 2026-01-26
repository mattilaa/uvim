#include "editor.h"
#include "constants.h"
#include "gitignore.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits.h>
#include <unistd.h>

namespace
{
std::string toLower(std::string_view input)
{
    std::string out(input);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
    return out;
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

std::optional<ModeState> GrepSearchMode::handle(ModeContext& ctx,
                                                const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    // ========================================================================
    // Exit
    // ========================================================================

    if(c == Terminal::ESC)
    {
        return defaultExitMode(ed);
    }

    // ========================================================================
    // Selection
    // ========================================================================

    if(c == Terminal::ENTER)
    {
        if(selectMatch(*ed))
        {
            return defaultExitMode(ed);
        }
        return std::nullopt;
    }

    // ========================================================================
    // Navigation through results
    // ========================================================================

    if(c == Terminal::CTRL_N || c == Terminal::CTRL_J ||
       c == Terminal::ARROW_DOWN)
    {
        resultDown(*ed);
    }
    else if(c == Terminal::CTRL_P || c == Terminal::CTRL_K ||
            c == Terminal::ARROW_UP)
    {
        resultUp(*ed);
    }
    else if(c == Terminal::CTRL_D || c == Terminal::PAGE_DOWN)
    {
        resultHalfPageDown(*ed);
    }
    else if(c == Terminal::CTRL_U || c == Terminal::PAGE_UP)
    {
        resultHalfPageUp(*ed);
    }

    // ========================================================================
    // Input Editing
    // ========================================================================

    else if(c == Terminal::BACKSPACE || c == 127 || c == Terminal::CTRL_H)
    {
        searchBackspace(*ed);
    }
    else if(c == Terminal::CTRL_W)
    {
        // Delete word backward
        searchDeleteWord(*ed);
    }
    // ========================================================================
    // Toggles
    // ========================================================================

    else if(c == Terminal::CTRL_I)
    {
        toggleGitignore(*ed);
    }
    else if(c == Terminal::TAB)
    {
        togglePreview();
    }

    // ========================================================================
    // Mode Switching
    // ========================================================================

    else if(c == Terminal::CTRL_B)
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

    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.uiDim();
    output +=
        "  [Enter: open] [Esc: cancel] [↑↓: navigate] [Ctrl+I: gitignore]";
    output += editor.theme.baseFg();

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
    output += editor.theme.baseFg();

    int availableRows = editor.screenRows - 3;

    for(int i = 0; i < availableRows && i + offset < (int)matches.size(); i++)
    {
        output += Terminal::NEWLINE_CLEAR;

        int index = i + offset;
        const GrepMatch& match = matches[index];

        if(index == cursor)
        {
            output += editor.theme.selection();
        }

        output += "  ";

        output += editor.theme.uiInfo();
        std::string displayName = match.filename;
        if(displayName.length() > 20)
        {
            displayName = displayName.substr(0, 17) + "...";
        }
        output += displayName;
        output += editor.theme.baseFg();

        output += ":";
        output += editor.theme.uiWarning();
        output += std::to_string(match.lineNumber);
        output += editor.theme.baseFg();
        output += ": ";

        std::string content = match.lineContent;
        int maxContentLen = editor.screenCols - displayName.length() - 10;
        if(maxContentLen < 20)
            maxContentLen = 20;

        if((int)content.length() > maxContentLen)
        {
            content = content.substr(0, maxContentLen - 3) + "...";
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

void GrepSearchMode::initialize(Editor& editor)
{
    if(!editor.fuzzyInitialized)
    {
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

    searchClear();
    searching = false;
}

void GrepSearchMode::performSearch(Editor& editor)
{
    matches.clear();
    searching = true;

    if(query.empty())
    {
        searching = false;
        return;
    }

    for(const auto& file : editor.allProjectFiles)
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
    size_t dotPos = filepath.find_last_of('.');
    if(dotPos != std::string::npos)
    {
        ext = filepath.substr(dotPos);
        bool isPythonExt =
            constants::is_filetype<constants::no_pattern,
                                   constants::python_suffixes>(filepath);
        bool isMlaExt = constants::is_filetype<constants::no_pattern,
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
        int visible = editor.screenRows - 4;
        if(cursor >= offset + visible)
            offset = cursor - visible + 1;
    }
}

void GrepSearchMode::resultHalfPageUp(Editor& editor)
{
    int half = (editor.screenRows - 4) / 2;
    cursor -= half;
    if(cursor < 0)
        cursor = 0;
    if(cursor < offset)
        offset = cursor;
}

void GrepSearchMode::resultHalfPageDown(Editor& editor)
{
    int half = (editor.screenRows - 4) / 2;
    cursor += half;
    if(cursor >= (int)matches.size())
        cursor = matches.size() - 1;
    int visible = editor.screenRows - 4;
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
    while(!query.empty() && query.back() == ' ')
        query.pop_back();
    while(!query.empty() && query.back() != ' ')
        query.pop_back();
    performSearch(editor);
    cursor = 0;
    offset = 0;
}

void GrepSearchMode::searchClear()
{
    query.clear();
    matches.clear();
    cursor = 0;
    offset = 0;
}

void GrepSearchMode::toggleGitignore(Editor& editor)
{
    editor.respectGitignore = !editor.respectGitignore;
    editor.fuzzyInitialized = false;
    initialize(editor);
}

void GrepSearchMode::togglePreview()
{
    previewEnabled = !previewEnabled;
}
