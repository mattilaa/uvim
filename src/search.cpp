#include "editor.h"
#include <regex>

void Editor::startSearchForward()
{
    setMode(SEARCH_FORWARD);
    clearSearch();
}

void Editor::startSearchBackward()
{
    setMode(SEARCH_BACKWARD);
    clearSearch();
}

void Editor::findAllMatches()
{
    searchMatches.clear();
    searchRegexError = false;
    searchMatchesPartial = false;
    if(searchQuery.empty())
        return;

    std::regex pattern;
    try
    {
        pattern = std::regex(searchQuery, std::regex_constants::icase);
    }
    catch(const std::regex_error&)
    {
        setStatusMessage("Invalid regex: " + searchQuery);
        searchRegexError = true;
        return;
    }

    for(int row = 0; row < lines->size(); row++)
    {
        const std::string& line = (*lines)[row];
        auto start = line.cbegin();

        while(start != line.cend())
        {
            std::smatch match;
            if(!std::regex_search(start, line.cend(), match, pattern))
                break;

            int col =
                (int)std::distance(line.cbegin(), start) + match.position();
            int len = match.length();
            if(len > 0)
            {
                SearchMatch entry;
                entry.row = row;
                entry.col = col;
                entry.len = len;
                searchMatches.push_back(entry);
            }

            if(len == 0)
            {
                if(col >= (int)line.size())
                    break;
                start = line.cbegin() + col + 1;
            }
            else
            {
                start = line.cbegin() + col + len;
            }
        }
    }
}

void Editor::jumpToMatch(int index)
{
    if(index < 0 || index >= searchMatches.size())
        return;

    const auto& match = searchMatches[index];
    *cursorY = match.row;
    *cursorX = match.col;
    currentMatchIndex = index;
    adjustViewport();
}

void Editor::performSearch()
{
    findAllMatches();

    if(searchRegexError)
        return;

    if(searchMatches.empty())
    {
        setStatusMessage("Pattern not found: " + searchQuery);
        return;
    }

    // Find closest match from saved cursor position
    int bestIndex = 0;
    int minDist = INT_MAX;

    for(int i = 0; i < searchMatches.size(); i++)
    {
        const auto& match = searchMatches[i];
        int dist;

        if(searchForward)
        {
            // Forward: prefer matches after cursor
            if(match.row > savedCursorY ||
               (match.row == savedCursorY && match.col >= savedCursorX))
            {
                dist = (match.row - savedCursorY) * 10000 +
                       (match.col - savedCursorX);
            }
            else
            {
                // Wrap around
                dist = 1000000 + match.row * 10000 + match.col;
            }
        }
        else
        {
            // Backward: prefer matches before cursor
            if(match.row < savedCursorY ||
               (match.row == savedCursorY && match.col <= savedCursorX))
            {
                dist = (savedCursorY - match.row) * 10000 +
                       (savedCursorX - match.col);
            }
            else
            {
                // Wrap around
                dist = 1000000 + (lines->size() - match.row) * 10000 +
                       ((*lines)[match.row].length() - match.col);
            }
        }

        if(dist < minDist)
        {
            minDist = dist;
            bestIndex = i;
        }
    }

    jumpToMatch(bestIndex);
    needsFullRedraw = true;
}

void Editor::searchNext()
{
    if(searchMatchesPartial && !searchQuery.empty())
    {
        savedCursorY = *cursorY;
        savedCursorX = *cursorX + 1;
        searchForward = true;
        performSearch();
        return;
    }

    if(searchMatches.empty())
    {
        if(!searchQuery.empty())
        {
            findAllMatches();
            if(searchRegexError)
                return;
            if(searchMatches.empty())
            {
                setStatusMessage("Pattern not found: " + searchQuery);
                return;
            }
        }
        else
        {
            return;
        }
    }

    currentMatchIndex = (currentMatchIndex + 1) % searchMatches.size();
    jumpToMatch(currentMatchIndex);
    needsFullRedraw = true;
}

void Editor::searchPrevious()
{
    if(searchMatchesPartial && !searchQuery.empty())
    {
        savedCursorY = *cursorY;
        savedCursorX = *cursorX - 1;
        searchForward = false;
        performSearch();
        return;
    }

    if(searchMatches.empty())
    {
        if(!searchQuery.empty())
        {
            findAllMatches();
            if(searchRegexError)
                return;
            if(searchMatches.empty())
            {
                setStatusMessage("Pattern not found: " + searchQuery);
                return;
            }
        }
        else
        {
            return;
        }
    }

    currentMatchIndex =
        (currentMatchIndex - 1 + searchMatches.size()) % searchMatches.size();
    jumpToMatch(currentMatchIndex);
    setStatusMessage("Match " + std::to_string(currentMatchIndex + 1) + " of " +
                     std::to_string(searchMatches.size()));
    needsFullRedraw = true;
}

void Editor::clearSearch()
{
    searchQuery.clear();
    searchMatches.clear();
    searchMatchesPartial = false;
    currentMatchIndex = -1;
    savedCursorX = *cursorX;
    savedCursorY = *cursorY;
}

void Editor::cancelSearch()
{
    searchMatches.clear();
    searchMatchesPartial = false;
    currentMatchIndex = -1;
    *cursorX = savedCursorX;
    *cursorY = savedCursorY;
    setMode(NORMAL);
}

bool Editor::isInSearchMatch(int row, int col)
{
    for(const auto& match : searchMatches)
    {
        if(match.row == row && col >= match.col && col < match.col + match.len)
        {
            return true;
        }
    }
    return false;
}
