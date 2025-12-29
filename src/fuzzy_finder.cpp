#include "editor_context.h"
#include "fuzzy_finder.h"
#include "terminal.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <dirent.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

// ============================================================================
// Constructor
// ============================================================================

FuzzyFinder::FuzzyFinder(EditorContext& ctx) : m_ctx(ctx) {}

// ============================================================================
// Initialization
// ============================================================================

void FuzzyFinder::open()
{
    m_query.clear();
    m_cursor = 0;
    m_offset = 0;
    m_active = true;

    if(!m_filesCollected)
    {
        collectFiles();
        m_filesCollected = true;
    }

    updateMatches();
    m_ctx.requestFullRedraw();
}

void FuzzyFinder::close()
{
    m_active = false;
}

// ============================================================================
// Input Handling
// ============================================================================

void FuzzyFinder::addChar(char c)
{
    m_query += c;
    m_cursor = 0;
    m_offset = 0;
    updateMatches();
    m_ctx.requestFullRedraw();
}

void FuzzyFinder::backspace()
{
    if(!m_query.empty())
    {
        m_query.pop_back();
        m_cursor = 0;
        m_offset = 0;
        updateMatches();
    }
    m_ctx.requestFullRedraw();
}

void FuzzyFinder::deleteWord()
{
    if(m_query.empty())
        return;

    // Delete trailing spaces
    while(!m_query.empty() && m_query.back() == ' ')
    {
        m_query.pop_back();
    }

    // Delete word characters
    while(!m_query.empty() && m_query.back() != ' ' && m_query.back() != '/')
    {
        m_query.pop_back();
    }

    m_cursor = 0;
    m_offset = 0;
    updateMatches();
    m_ctx.requestFullRedraw();
}

void FuzzyFinder::clear()
{
    m_query.clear();
    m_cursor = 0;
    m_offset = 0;
    updateMatches();
    m_ctx.requestFullRedraw();
}

// ============================================================================
// Navigation
// ============================================================================

void FuzzyFinder::moveUp()
{
    if(m_cursor > 0)
    {
        m_cursor--;
        adjustScroll();
    }
    m_ctx.requestFullRedraw();
}

void FuzzyFinder::moveDown()
{
    if(m_cursor < static_cast<int>(m_matches.size()) - 1)
    {
        m_cursor++;
        adjustScroll();
    }
    m_ctx.requestFullRedraw();
}

void FuzzyFinder::halfPageUp()
{
    int halfPage = m_ctx.screenRows() / 2;
    for(int i = 0; i < halfPage && m_cursor > 0; i++)
    {
        m_cursor--;
    }
    adjustScroll();
    m_ctx.requestFullRedraw();
}

void FuzzyFinder::halfPageDown()
{
    int halfPage = m_ctx.screenRows() / 2;
    int maxCursor = static_cast<int>(m_matches.size()) - 1;
    for(int i = 0; i < halfPage && m_cursor < maxCursor; i++)
    {
        m_cursor++;
    }
    adjustScroll();
    m_ctx.requestFullRedraw();
}

// ============================================================================
// Selection
// ============================================================================

bool FuzzyFinder::selectEntry()
{
    if(m_matches.empty() || m_cursor >= static_cast<int>(m_matches.size()))
    {
        return false;
    }

    const FuzzyMatch& match = m_matches[m_cursor];
    m_ctx.openFile(match.path);
    m_active = false;
    return true;
}

// ============================================================================
// Preview
// ============================================================================

void FuzzyFinder::togglePreview()
{
    m_showPreview = !m_showPreview;
    m_ctx.requestFullRedraw();
}

// ============================================================================
// Drawing
// ============================================================================

void FuzzyFinder::draw()
{
    std::string output;
    output.reserve(m_ctx.screenRows() * m_ctx.screenCols() * 2);

    // Draw query line at top
    Terminal::moveCursor(1, 1);
    output += "\x1b[K";           // Clear line
    output += "\x1b[1m> \x1b[0m"; // Bold prompt
    output += m_query;
    output += "\x1b[7m \x1b[0m"; // Cursor

    // Draw matches
    int visibleRows = m_ctx.screenRows() - 1; // -1 for query line

    for(int row = 0; row < visibleRows; row++)
    {
        int matchIndex = m_offset + row;

        Terminal::moveCursor(row + 2, 1);
        output += "\x1b[K"; // Clear line

        if(matchIndex < static_cast<int>(m_matches.size()))
        {
            const FuzzyMatch& match = m_matches[matchIndex];

            // Highlight current selection
            if(matchIndex == m_cursor)
            {
                output += "\x1b[7m"; // Reverse video
            }

            // Display the path with highlighted match positions
            std::string displayPath = match.path;
            int maxLen = m_ctx.screenCols() - 2;
            if(static_cast<int>(displayPath.length()) > maxLen)
            {
                displayPath = "..." + displayPath.substr(displayPath.length() -
                                                         maxLen + 3);
            }

            output += " " + displayPath;

            // Reset colors
            output += "\x1b[0m";
        }
    }

    Terminal::write(output);
}

void FuzzyFinder::drawStatusLine()
{
    std::ostringstream oss;
    oss << " FUZZY FIND: " << m_matches.size() << "/" << m_allFiles.size()
        << " files";

    if(!m_query.empty())
    {
        oss << " matching '" << m_query << "'";
    }

    m_ctx.setStatusMessage(oss.str());
}

// ============================================================================
// Internal Methods
// ============================================================================

void FuzzyFinder::collectFiles()
{
    m_allFiles.clear();

    // Get current working directory
    char cwd[PATH_MAX];
    if(!getcwd(cwd, sizeof(cwd)))
    {
        return;
    }

    // Recursively collect files (limited depth to avoid too many files)
    std::vector<std::string> dirsToProcess;
    dirsToProcess.push_back(cwd);

    int maxFiles = 10000;
    int maxDepth = 10;

    while(!dirsToProcess.empty() &&
          static_cast<int>(m_allFiles.size()) < maxFiles)
    {
        std::string currentDir = dirsToProcess.back();
        dirsToProcess.pop_back();

        // Calculate depth
        int depth = 0;
        for(char c : currentDir)
        {
            if(c == '/')
                depth++;
        }
        int cwdDepth = 0;
        for(char c : std::string(cwd))
        {
            if(c == '/')
                cwdDepth++;
        }
        if(depth - cwdDepth > maxDepth)
            continue;

        DIR* dir = opendir(currentDir.c_str());
        if(!dir)
            continue;

        struct dirent* entry;
        while((entry = readdir(dir)) &&
              static_cast<int>(m_allFiles.size()) < maxFiles)
        {
            std::string name = entry->d_name;

            // Skip hidden files and special directories
            if(name[0] == '.')
                continue;
            if(name == "node_modules" || name == "build" || name == ".git")
                continue;

            std::string fullPath = currentDir + "/" + name;

            struct stat st;
            if(stat(fullPath.c_str(), &st) != 0)
                continue;

            if(S_ISDIR(st.st_mode))
            {
                dirsToProcess.push_back(fullPath);
            }
            else if(S_ISREG(st.st_mode))
            {
                // Store relative path
                std::string relativePath = fullPath.substr(strlen(cwd) + 1);
                m_allFiles.push_back(relativePath);
            }
        }

        closedir(dir);
    }

    // Sort files by name
    std::sort(m_allFiles.begin(), m_allFiles.end());
}

void FuzzyFinder::updateMatches()
{
    m_matches.clear();

    if(m_query.empty())
    {
        // Show all files when query is empty
        for(const auto& file : m_allFiles)
        {
            FuzzyMatch match;
            match.path = file;
            match.displayName = file;
            match.score = 0;
            m_matches.push_back(match);
        }
        return;
    }

    // Score and collect matches
    for(const auto& file : m_allFiles)
    {
        std::vector<size_t> positions;
        int score = fuzzyScore(file, m_query, positions);

        if(score > 0)
        {
            FuzzyMatch match;
            match.path = file;
            match.displayName = file;
            match.score = score;
            match.matchPositions = positions;
            m_matches.push_back(match);
        }
    }

    // Sort by score (highest first)
    std::sort(m_matches.begin(), m_matches.end(),
              [](const FuzzyMatch& a, const FuzzyMatch& b)
              { return a.score > b.score; });
}

int FuzzyFinder::fuzzyScore(const std::string& str, const std::string& pattern,
                            std::vector<size_t>& positions) const
{
    positions.clear();

    if(pattern.empty())
        return 1;
    if(str.empty())
        return 0;

    int score = 0;
    size_t patternIdx = 0;
    size_t lastMatchIdx = std::string::npos;

    for(size_t i = 0; i < str.length() && patternIdx < pattern.length(); i++)
    {
        char strChar = std::tolower(str[i]);
        char patChar = std::tolower(pattern[patternIdx]);

        if(strChar == patChar)
        {
            positions.push_back(i);

            // Bonus for consecutive matches
            if(lastMatchIdx != std::string::npos && i == lastMatchIdx + 1)
            {
                score += 10;
            }

            // Bonus for match at start or after separator
            if(i == 0 || str[i - 1] == '/' || str[i - 1] == '_' ||
               str[i - 1] == '-')
            {
                score += 5;
            }

            // Bonus for exact case match
            if(str[i] == pattern[patternIdx])
            {
                score += 2;
            }

            score += 1;
            lastMatchIdx = i;
            patternIdx++;
        }
    }

    // Return 0 if not all pattern characters matched
    if(patternIdx < pattern.length())
    {
        return 0;
    }

    // Bonus for shorter strings (prefer shorter matches)
    score += static_cast<int>(100 - std::min(str.length(), size_t(100)));

    return score;
}

void FuzzyFinder::adjustScroll()
{
    int visibleRows = m_ctx.screenRows() - 1;

    if(m_cursor < m_offset)
    {
        m_offset = m_cursor;
    }
    else if(m_cursor >= m_offset + visibleRows)
    {
        m_offset = m_cursor - visibleRows + 1;
    }
}
