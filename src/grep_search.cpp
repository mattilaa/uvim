#include "editor_context.h"
#include "grep_search.h"
#include "terminal.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <sstream>

// ============================================================================
// Constructor
// ============================================================================

GrepSearch::GrepSearch(EditorContext& ctx) : m_ctx(ctx) {}

// ============================================================================
// Initialization
// ============================================================================

void GrepSearch::open()
{
    m_query.clear();
    m_matches.clear();
    m_cursor = 0;
    m_offset = 0;
    m_active = true;
    m_searching = false;
    m_ctx.requestFullRedraw();
}

void GrepSearch::close()
{
    m_active = false;
}

// ============================================================================
// Input Handling
// ============================================================================

void GrepSearch::addChar(char c)
{
    m_query += c;

    // Only search if query is at least 2 characters
    if(m_query.length() >= 2)
    {
        performSearch();
    }

    m_ctx.requestFullRedraw();
}

void GrepSearch::backspace()
{
    if(!m_query.empty())
    {
        m_query.pop_back();

        if(m_query.length() >= 2)
        {
            performSearch();
        }
        else
        {
            m_matches.clear();
        }
    }
    m_ctx.requestFullRedraw();
}

void GrepSearch::deleteWord()
{
    if(m_query.empty())
        return;

    // Delete trailing spaces
    while(!m_query.empty() && m_query.back() == ' ')
    {
        m_query.pop_back();
    }

    // Delete word characters
    while(!m_query.empty() && m_query.back() != ' ')
    {
        m_query.pop_back();
    }

    if(m_query.length() >= 2)
    {
        performSearch();
    }
    else
    {
        m_matches.clear();
    }

    m_cursor = 0;
    m_offset = 0;
    m_ctx.requestFullRedraw();
}

void GrepSearch::clear()
{
    m_query.clear();
    m_matches.clear();
    m_cursor = 0;
    m_offset = 0;
    m_ctx.requestFullRedraw();
}

// ============================================================================
// Navigation
// ============================================================================

void GrepSearch::moveUp()
{
    if(m_cursor > 0)
    {
        m_cursor--;
        adjustScroll();
    }
    m_ctx.requestFullRedraw();
}

void GrepSearch::moveDown()
{
    if(m_cursor < static_cast<int>(m_matches.size()) - 1)
    {
        m_cursor++;
        adjustScroll();
    }
    m_ctx.requestFullRedraw();
}

void GrepSearch::halfPageUp()
{
    int halfPage = m_ctx.screenRows() / 2;
    for(int i = 0; i < halfPage && m_cursor > 0; i++)
    {
        m_cursor--;
    }
    adjustScroll();
    m_ctx.requestFullRedraw();
}

void GrepSearch::halfPageDown()
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

bool GrepSearch::selectEntry()
{
    if(m_matches.empty() || m_cursor >= static_cast<int>(m_matches.size()))
    {
        return false;
    }

    const GrepMatch& match = m_matches[m_cursor];

    // Open the file
    m_ctx.openFile(match.filepath);

    // Jump to the line and column
    m_ctx.cursorY() = match.lineNumber - 1;
    m_ctx.cursorX() = match.columnNumber - 1;
    if(m_ctx.cursorY() < 0)
        m_ctx.cursorY() = 0;
    if(m_ctx.cursorX() < 0)
        m_ctx.cursorX() = 0;

    m_active = false;
    return true;
}

// ============================================================================
// Preview
// ============================================================================

void GrepSearch::togglePreview()
{
    m_showPreview = !m_showPreview;
    m_ctx.requestFullRedraw();
}

// ============================================================================
// Drawing
// ============================================================================

void GrepSearch::draw()
{
    std::string output;
    output.reserve(m_ctx.screenRows() * m_ctx.screenCols() * 2);

    // Draw query line at top
    Terminal::moveCursor(1, 1);
    output += "\x1b[K";               // Clear line
    output += "\x1b[1mgrep> \x1b[0m"; // Bold prompt
    output += m_query;
    output += "\x1b[7m \x1b[0m"; // Cursor

    if(m_searching)
    {
        output += " \x1b[33m(searching...)\x1b[0m";
    }

    // Draw matches
    int visibleRows = m_ctx.screenRows() - 1; // -1 for query line

    for(int row = 0; row < visibleRows; row++)
    {
        int matchIndex = m_offset + row;

        Terminal::moveCursor(row + 2, 1);
        output += "\x1b[K"; // Clear line

        if(matchIndex < static_cast<int>(m_matches.size()))
        {
            const GrepMatch& match = m_matches[matchIndex];

            // Highlight current selection
            if(matchIndex == m_cursor)
            {
                output += "\x1b[7m"; // Reverse video
            }

            // Format: filepath:line:col: content
            std::ostringstream oss;

            // Filepath (in color)
            output += "\x1b[35m"; // Magenta
            std::string filepath = match.filepath;
            int maxPathLen = 30;
            if(static_cast<int>(filepath.length()) > maxPathLen)
            {
                filepath =
                    "..." + filepath.substr(filepath.length() - maxPathLen + 3);
            }
            output += filepath;
            output += "\x1b[0m";

            if(matchIndex == m_cursor)
            {
                output += "\x1b[7m"; // Re-apply reverse
            }

            // Line number (in color)
            output += "\x1b[32m"; // Green
            oss << ":" << match.lineNumber;
            output += oss.str();
            output += "\x1b[0m";

            if(matchIndex == m_cursor)
            {
                output += "\x1b[7m"; // Re-apply reverse
            }

            output += ": ";

            // Line content (truncated)
            std::string content = match.lineContent;
            // Trim leading whitespace
            size_t start = content.find_first_not_of(" \t");
            if(start != std::string::npos)
            {
                content = content.substr(start);
            }

            int maxContentLen = m_ctx.screenCols() - maxPathLen - 15;
            if(static_cast<int>(content.length()) > maxContentLen)
            {
                content = content.substr(0, maxContentLen - 3) + "...";
            }
            output += content;

            // Reset colors
            output += "\x1b[0m";
        }
    }

    Terminal::write(output);
}

void GrepSearch::drawStatusLine()
{
    std::ostringstream oss;
    oss << " GREP: " << m_matches.size() << " matches";

    if(!m_query.empty())
    {
        oss << " for '" << m_query << "'";
    }

    if(m_searching)
    {
        oss << " (searching...)";
    }

    m_ctx.setStatusMessage(oss.str());
}

// ============================================================================
// Internal Methods
// ============================================================================

void GrepSearch::performSearch()
{
    m_matches.clear();
    m_cursor = 0;
    m_offset = 0;
    m_searching = true;

    // Build the grep command
    // Try ripgrep first, fall back to grep
    std::string cmd;

    // Escape the query for shell
    std::string escapedQuery;
    for(char c : m_query)
    {
        if(c == '\'' || c == '\\' || c == '"')
        {
            escapedQuery += '\\';
        }
        escapedQuery += c;
    }

    // Try ripgrep (rg) first
    cmd = "rg --line-number --column --no-heading --color=never '";
    cmd += escapedQuery;
    cmd += "' 2>/dev/null || grep -rn '";
    cmd += escapedQuery;
    cmd += "' . 2>/dev/null";

    // Run the command and capture output
    std::array<char, 4096> buffer;
    std::string result;

    FILE* pipe = popen(cmd.c_str(), "r");
    if(pipe)
    {
        while(fgets(buffer.data(), buffer.size(), pipe) != nullptr)
        {
            result += buffer.data();

            // Limit results
            if(m_matches.size() >= 1000)
            {
                break;
            }

            // Parse each line as we get it
            size_t lastNewline = result.rfind('\n');
            if(lastNewline != std::string::npos)
            {
                std::string completeLines = result.substr(0, lastNewline);
                result = result.substr(lastNewline + 1);
                parseGrepOutput(completeLines);
            }
        }
        pclose(pipe);

        // Parse any remaining output
        if(!result.empty())
        {
            parseGrepOutput(result);
        }
    }

    m_searching = false;
    m_ctx.requestFullRedraw();
}

void GrepSearch::parseGrepOutput(const std::string& output)
{
    std::istringstream stream(output);
    std::string line;

    while(std::getline(stream, line))
    {
        if(line.empty())
            continue;

        GrepMatch match;

        // Parse ripgrep format: filepath:line:col:content
        // Or grep format: filepath:line:content

        size_t firstColon = line.find(':');
        if(firstColon == std::string::npos)
            continue;

        // Handle Windows paths (C:\...)
        if(firstColon == 1 && line.length() > 2 && line[2] == '\\')
        {
            firstColon = line.find(':', 2);
            if(firstColon == std::string::npos)
                continue;
        }

        match.filepath = line.substr(0, firstColon);

        size_t secondColon = line.find(':', firstColon + 1);
        if(secondColon == std::string::npos)
            continue;

        std::string lineNumStr =
            line.substr(firstColon + 1, secondColon - firstColon - 1);
        try
        {
            match.lineNumber = std::stoi(lineNumStr);
        }
        catch(...)
        {
            continue;
        }

        // Check for column (ripgrep format)
        size_t thirdColon = line.find(':', secondColon + 1);
        if(thirdColon != std::string::npos)
        {
            std::string colStr =
                line.substr(secondColon + 1, thirdColon - secondColon - 1);
            try
            {
                match.columnNumber = std::stoi(colStr);
                match.lineContent = line.substr(thirdColon + 1);
            }
            catch(...)
            {
                match.columnNumber = 1;
                match.lineContent = line.substr(secondColon + 1);
            }
        }
        else
        {
            match.columnNumber = 1;
            match.lineContent = line.substr(secondColon + 1);
        }

        m_matches.push_back(match);
    }
}

void GrepSearch::adjustScroll()
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
