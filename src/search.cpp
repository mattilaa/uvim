#include "editor_lsp_query.h"
#include <algorithm>

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
    if(searchQuery.empty())
        return;

    std::string lowerQuery = toLowerCase(searchQuery);

    for(int row = 0; row < lines->size(); row++)
    {
        std::string lowerLine = toLowerCase((*lines)[row]);
        size_t pos = 0;

        while((pos = lowerLine.find(lowerQuery, pos)) != std::string::npos)
        {
            SearchMatch match;
            match.row = row;
            match.col = pos;
            match.len = searchQuery.length();
            searchMatches.push_back(match);
            pos += searchQuery.length();
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
    setStatusMessage("Match " + std::to_string(currentMatchIndex + 1) + " of " +
                     std::to_string(searchMatches.size()));
}

void Editor::searchNext()
{
    if(searchMatches.empty())
    {
        if(!searchQuery.empty())
        {
            findAllMatches();
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
    setStatusMessage("Match " + std::to_string(currentMatchIndex + 1) + " of " +
                     std::to_string(searchMatches.size()));
}

void Editor::searchPrevious()
{
    if(searchMatches.empty())
    {
        if(!searchQuery.empty())
        {
            findAllMatches();
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
}

void Editor::clearSearch()
{
    searchQuery.clear();
    searchMatches.clear();
    currentMatchIndex = -1;
    savedCursorX = *cursorX;
    savedCursorY = *cursorY;
}

void Editor::cancelSearch()
{
    searchMatches.clear();
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
