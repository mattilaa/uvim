#include "editor.h"
#include "gitignore.h"
#include "terminal.h"
#include <algorithm>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

void Editor::initializeGrepSearch()
{
    if(!fuzzyInitialized)
    {
        allProjectFiles.clear();
        char cwd[PATH_MAX];
        if(getcwd(cwd, sizeof(cwd)))
        {
            GitIgnore gitignore;
            gitignore.loadRecursive(cwd);
            collectProjectFiles(std::string(cwd), 0, gitignore);
        }
        fuzzyInitialized = true;
    }

    grepQuery.clear();
    grepMatches.clear();
    grepCursor = 0;
    grepOffset = 0;
    grepSearching = false;
}

std::string Editor::trimString(const std::string& str)
{
    size_t first = str.find_first_not_of(" \t\r\n");
    if(first == std::string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

bool Editor::isTextFile(const std::string& filepath)
{
    std::string ext;
    size_t dotPos = filepath.find_last_of('.');
    if(dotPos != std::string::npos)
    {
        ext = filepath.substr(dotPos);
        if(ext == ".txt" || ext == ".cpp" || ext == ".c" || ext == ".h" ||
           ext == ".hpp" || ext == ".py" || ext == ".js" || ext == ".ts" ||
           ext == ".jsx" || ext == ".tsx" || ext == ".java" || ext == ".rs" ||
           ext == ".go" || ext == ".rb" || ext == ".php" || ext == ".sh" ||
           ext == ".bash" || ext == ".zsh" || ext == ".vim" || ext == ".lua" ||
           ext == ".md" || ext == ".markdown" || ext == ".rst" ||
           ext == ".tex" || ext == ".css" || ext == ".scss" || ext == ".html" ||
           ext == ".xml" || ext == ".json" || ext == ".yaml" || ext == ".yml" ||
           ext == ".toml" || ext == ".ini" || ext == ".conf" ||
           ext == ".config" || ext == ".log" || ext == ".cmake" ||
           ext == ".make" || ext == ".mk" || ext == ".am" || ext == ".mla")
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

bool Editor::isBinaryFile(const std::string& filepath)
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

void Editor::searchInFile(const std::string& filepath, const std::string& query)
{
    if(!isTextFile(filepath))
        return;

    std::ifstream file(filepath);
    if(!file)
        return;

    std::string lowerQuery = toLowerCase(query);
    std::string line;
    int lineNumber = 0;

    while(std::getline(file, line))
    {
        lineNumber++;

        std::string lowerLine = toLowerCase(line);
        size_t pos = 0;

        while((pos = lowerLine.find(lowerQuery, pos)) != std::string::npos)
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
                std::make_pair((int)pos, (int)query.length()));

            grepMatches.push_back(match);
            pos += query.length();

            if(grepMatches.size() >= 1000)
                return;
        }
    }
}

void Editor::performGrepSearch()
{
    grepMatches.clear();
    grepSearching = true;

    if(grepQuery.empty())
    {
        grepSearching = false;
        return;
    }

    for(const auto& file : allProjectFiles)
    {
        if(file.isDirectory)
            continue;

        searchInFile(file.path, grepQuery);

        if(grepMatches.size() >= 1000)
            break;
    }

    grepSearching = false;

    if(grepCursor >= (int)grepMatches.size())
    {
        grepCursor = 0;
        grepOffset = 0;
    }
}

void Editor::searchFileContent(const std::string& filepath)
{
    if(!isTextFile(filepath))
        return;

    std::ifstream file(filepath);
    if(!file)
        return;

    std::string line;
    int lineNum = 0;

    // Get relative path for display
    char cwd[PATH_MAX];
    std::string displayPath = filepath;
    if(getcwd(cwd, sizeof(cwd)))
    {
        std::string cwdStr(cwd);
        if(displayPath.find(cwdStr) == 0)
        {
            displayPath = displayPath.substr(cwdStr.length() + 1);
        }
    }

    // Extract filename
    size_t lastSlash = displayPath.find_last_of("/");
    std::string filename = (lastSlash != std::string::npos)
                               ? displayPath.substr(lastSlash + 1)
                               : displayPath;

    while(std::getline(file, line))
    {
        lineNum++;

        std::string searchLine = line;
        std::string searchQuery = grepQuery;

        if(!grepCaseSensitive)
        {
            std::transform(searchLine.begin(), searchLine.end(),
                           searchLine.begin(), ::tolower);
            std::transform(searchQuery.begin(), searchQuery.end(),
                           searchQuery.begin(), ::tolower);
        }

        if(searchLine.find(searchQuery) != std::string::npos)
        {
            GrepMatch match;
            match.filename = filename;
            match.filepath = filepath;
            match.lineNumber = lineNum;
            match.lineContent = trimString(line);
            highlightGrepMatches(line, grepQuery, match.highlightRanges);

            // Limit line content to reasonable length
            if(match.lineContent.length() > 200)
            {
                match.lineContent = match.lineContent.substr(0, 197) + "...";
            }

            grepMatches.push_back(match);

            // Limit total matches to prevent memory issues
            if(grepMatches.size() > 10000)
            {
                return;
            }
        }
    }
}

void Editor::drawGrepSearch()
{
    std::string output;
    output.reserve(screenRows * screenCols * 2);

    output += Terminal::ESC_CURSOR_HOME;
    output += Terminal::ESC_CLEAR_LINE;

    output += Terminal::ESC_BOLD;
    output += "  Grep: ";
    output += Terminal::ESC_RESET_ALL;
    output += Terminal::FG_GREEN;
    output += grepQuery;

    output += Terminal::ESC_BLINK;
    output += "_";
    output += Terminal::ESC_BLINK_OFF;
    output += Terminal::FG_DEFAULT;

    if(grepSearching)
    {
        output += Terminal::FG_YELLOW;
        output += " (searching...)";
        output += Terminal::FG_DEFAULT;
    }

    output += Terminal::NEWLINE_CLEAR;
    output += Terminal::FG_BRIGHT_BLACK;
    output +=
        "  [Enter: open] [Esc: cancel] [↑↓: navigate] [Ctrl+I: gitignore]";
    output += Terminal::FG_DEFAULT;

    output += Terminal::NEWLINE_CLEAR;
    output += Terminal::FG_BRIGHT_BLACK;
    if(!grepMatches.empty())
    {
        output += "  " + std::to_string(grepMatches.size());
        if(grepMatches.size() >= 1000)
            output += "+ matches (limited)";
        else
            output += " matches";
    }
    else if(!grepQuery.empty() && !grepSearching)
    {
        output += "  No matches";
    }
    if(respectGitignore)
    {
        output += " [gitignore]";
    }
    output += Terminal::FG_DEFAULT;

    int availableRows = screenRows - 3;

    for(int i = 0;
        i < availableRows && i + grepOffset < (int)grepMatches.size(); i++)
    {
        output += Terminal::NEWLINE_CLEAR;

        int index = i + grepOffset;
        const GrepMatch& match = grepMatches[index];

        if(index == grepCursor)
        {
            output += Terminal::STYLE_SELECTION;
        }

        output += "  ";

        output += Terminal::FG_CYAN;
        std::string displayName = match.filename;
        if(displayName.length() > 20)
        {
            displayName = displayName.substr(0, 17) + "...";
        }
        output += displayName;
        output += Terminal::FG_DEFAULT;

        output += ":";
        output += Terminal::FG_YELLOW;
        output += std::to_string(match.lineNumber);
        output += Terminal::FG_DEFAULT;
        output += ": ";

        std::string content = match.lineContent;
        int maxContentLen = screenCols - displayName.length() - 10;
        if(maxContentLen < 20)
            maxContentLen = 20;

        if((int)content.length() > maxContentLen)
        {
            content = content.substr(0, maxContentLen - 3) + "...";
        }

        if(!match.highlightRanges.empty() && index != grepCursor)
        {
            std::string lowerContent = toLowerCase(content);
            std::string lowerQuery = toLowerCase(grepQuery);

            size_t pos = lowerContent.find(lowerQuery);
            if(pos != std::string::npos)
            {
                output += content.substr(0, pos);
                output += Terminal::STYLE_GREEN_BOLD;
                output += content.substr(pos, grepQuery.length());
                output += Terminal::STYLE_RESET_GREEN_BOLD;
                output += content.substr(pos + grepQuery.length());
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

        output += Terminal::ESC_RESET_ALL;
    }

    for(int i = (int)grepMatches.size() - grepOffset; i < availableRows; i++)
    {
        output += Terminal::NEWLINE_CLEAR;
        output += Terminal::FG_BLUE;
        output += "~";
        output += Terminal::FG_DEFAULT;
    }

    Terminal::write(output);
    Terminal::flush();
}

void Editor::selectGrepMatch()
{
    if(grepCursor < 0 || grepCursor >= (int)grepMatches.size())
        return;

    const GrepMatch& match = grepMatches[grepCursor];

    openFile(match.filepath);

    *cursorY = match.lineNumber - 1;
    if(*cursorY >= (int)lines->size())
        *cursorY = lines->size() - 1;
    if(*cursorY < 0)
        *cursorY = 0;

    *cursorX = 0;

    searchQuery = grepQuery;
    findAllMatches();

    setMode(NORMAL);
    centerScreen();
}

void Editor::handleGrepSearchMode(int c)
{
    switch(c)
    {
    case Terminal::ENTER:
        selectGrepMatch();
        break;

    case Terminal::ESC:
        setMode(NORMAL);
        needsFullRedraw = true;
        break;

    case Terminal::ARROW_DOWN:
    case Terminal::CTRL_N:
    case Terminal::CTRL_J:
        if(grepCursor < (int)grepMatches.size() - 1)
        {
            grepCursor++;
            if(grepCursor >= grepOffset + screenRows - 3)
            {
                grepOffset = grepCursor - screenRows + 4;
            }
        }
        break;

    case Terminal::ARROW_UP:
    case Terminal::CTRL_P:
    case Terminal::CTRL_K:
        if(grepCursor > 0)
        {
            grepCursor--;
            if(grepCursor < grepOffset)
            {
                grepOffset = grepCursor;
            }
        }
        break;

    case Terminal::BACKSPACE:
    case Terminal::DEL:
        if(!grepQuery.empty())
        {
            grepQuery.pop_back();
            performGrepSearch();
            grepCursor = 0;
            grepOffset = 0;
        }
        break;

    case Terminal::CTRL_U:
        grepQuery.clear();
        grepMatches.clear();
        grepCursor = 0;
        grepOffset = 0;
        break;

    case Terminal::CTRL_I:
        toggleGitignore();
        fuzzyInitialized = false;
        initializeGrepSearch();
        if(!grepQuery.empty())
        {
            performGrepSearch();
        }
        break;

    case Terminal::PAGE_DOWN:
        if(grepMatches.size() > 0)
        {
            int pageSize = screenRows - 3;
            grepCursor =
                std::min((int)grepMatches.size() - 1, grepCursor + pageSize);
            if(grepCursor >= grepOffset + pageSize)
            {
                grepOffset = grepCursor - pageSize + 1;
            }
        }
        break;

    case Terminal::PAGE_UP:
        if(grepMatches.size() > 0)
        {
            int pageSize = screenRows - 3;
            grepCursor = std::max(0, grepCursor - pageSize);
            if(grepCursor < grepOffset)
            {
                grepOffset = grepCursor;
            }
        }
        break;

    default:
        if(c >= 32 && c < 127)
        {
            grepQuery += static_cast<char>(c);
            performGrepSearch();
            grepCursor = 0;
            grepOffset = 0;
        }
        break;
    }
}

void Editor::highlightGrepMatches(const std::string& line,
                                  const std::string& query,
                                  std::vector<std::pair<int, int>>& ranges)
{
    ranges.clear();
    if(query.empty())
        return;

    std::string searchLine = line;
    std::string searchQuery = query;

    if(!grepCaseSensitive)
    {
        std::transform(searchLine.begin(), searchLine.end(), searchLine.begin(),
                       ::tolower);
        std::transform(searchQuery.begin(), searchQuery.end(),
                       searchQuery.begin(), ::tolower);
    }

    size_t pos = 0;
    while((pos = searchLine.find(searchQuery, pos)) != std::string::npos)
    {
        ranges.push_back({pos, pos + searchQuery.length()});
        pos += searchQuery.length();
    }
}
