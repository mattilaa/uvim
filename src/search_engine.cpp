#include "search_engine.h"
#include <algorithm>
#include <cctype>

SearchEngine::SearchEngine(EditorContext& ctx) : ctx(ctx) {}

void SearchEngine::startSearchForward()
{
    ctx.currentMode = Mode::SEARCH_FORWARD;
    ctx.searchQuery.clear();
    ctx.searchForward = true;
    ctx.statusMessage = "/";
}

void SearchEngine::startSearchBackward()
{
    ctx.currentMode = Mode::SEARCH_BACKWARD;
    ctx.searchQuery.clear();
    ctx.searchForward = false;
    ctx.statusMessage = "?";
}

void SearchEngine::findAllMatches()
{
    ctx.searchMatches.clear();
    ctx.currentMatchIndex = -1;

    if(ctx.searchQuery.empty())
        return;

    // Case-insensitive search
    std::string lowerQuery = ctx.searchQuery;
    for(char& c : lowerQuery)
    {
        c = std::tolower(static_cast<unsigned char>(c));
    }

    for(int lineNum = 0; lineNum < (int)ctx.lines->size(); lineNum++)
    {
        const std::string& line = (*ctx.lines)[lineNum];
        std::string lowerLine = line;
        for(char& c : lowerLine)
        {
            c = std::tolower(static_cast<unsigned char>(c));
        }

        size_t pos = 0;
        while((pos = lowerLine.find(lowerQuery, pos)) != std::string::npos)
        {
            SearchMatch match;
            match.line = lineNum;
            match.startCol = pos;
            match.endCol = pos + ctx.searchQuery.length() - 1;
            ctx.searchMatches.push_back(match);
            pos++;
        }
    }
}

void SearchEngine::jumpToMatch(int index)
{
    if(index < 0 || index >= (int)ctx.searchMatches.size())
        return;

    ctx.currentMatchIndex = index;
    const SearchMatch& match = ctx.searchMatches[index];
    *ctx.cursorY = match.line;
    *ctx.cursorX = match.startCol;
    *ctx.wantedX = *ctx.cursorX;

    // Adjust viewport
    if(*ctx.cursorY < *ctx.offsetY)
    {
        *ctx.offsetY = *ctx.cursorY;
    }
    if(*ctx.cursorY >= *ctx.offsetY + ctx.screenRows)
    {
        *ctx.offsetY = *ctx.cursorY - ctx.screenRows + 1;
    }

    ctx.needsFullRedraw = true;
}

void SearchEngine::performSearch()
{
    if(ctx.searchQuery.empty())
    {
        ctx.currentMode = Mode::NORMAL;
        return;
    }

    // Save search state to buffer
    ctx.currentBuffer->lastSearchQuery = ctx.searchQuery;
    ctx.currentBuffer->lastSearchForward = ctx.searchForward;

    findAllMatches();

    if(ctx.searchMatches.empty())
    {
        ctx.statusMessage = "Pattern not found: " + ctx.searchQuery;
        ctx.currentMode = Mode::NORMAL;
        return;
    }

    // Find the nearest match in search direction
    int nearestIndex = -1;

    if(ctx.searchForward)
    {
        // Find first match at or after cursor
        for(int i = 0; i < (int)ctx.searchMatches.size(); i++)
        {
            const SearchMatch& m = ctx.searchMatches[i];
            if(m.line > *ctx.cursorY ||
               (m.line == *ctx.cursorY && m.startCol >= *ctx.cursorX))
            {
                nearestIndex = i;
                break;
            }
        }
        // Wrap around
        if(nearestIndex == -1)
        {
            nearestIndex = 0;
        }
    }
    else
    {
        // Find first match before cursor
        for(int i = (int)ctx.searchMatches.size() - 1; i >= 0; i--)
        {
            const SearchMatch& m = ctx.searchMatches[i];
            if(m.line < *ctx.cursorY ||
               (m.line == *ctx.cursorY && m.startCol <= *ctx.cursorX))
            {
                nearestIndex = i;
                break;
            }
        }
        // Wrap around
        if(nearestIndex == -1)
        {
            nearestIndex = ctx.searchMatches.size() - 1;
        }
    }

    jumpToMatch(nearestIndex);

    ctx.statusMessage = "Match " + std::to_string(ctx.currentMatchIndex + 1) +
                        " of " + std::to_string(ctx.searchMatches.size());
    ctx.currentMode = Mode::NORMAL;
}

void SearchEngine::searchNext()
{
    if(ctx.searchMatches.empty())
    {
        if(!ctx.searchQuery.empty())
        {
            findAllMatches();
            if(ctx.searchMatches.empty())
            {
                ctx.statusMessage = "Pattern not found: " + ctx.searchQuery;
                return;
            }
        }
        else
        {
            ctx.statusMessage = "No previous search";
            return;
        }
    }

    int nextIndex;
    if(ctx.searchForward)
    {
        nextIndex = (ctx.currentMatchIndex + 1) % ctx.searchMatches.size();
    }
    else
    {
        nextIndex = (ctx.currentMatchIndex - 1 + ctx.searchMatches.size()) %
                    ctx.searchMatches.size();
    }

    jumpToMatch(nextIndex);
    ctx.statusMessage = "Match " + std::to_string(ctx.currentMatchIndex + 1) +
                        " of " + std::to_string(ctx.searchMatches.size());
}

void SearchEngine::searchPrevious()
{
    if(ctx.searchMatches.empty())
    {
        if(!ctx.searchQuery.empty())
        {
            findAllMatches();
            if(ctx.searchMatches.empty())
            {
                ctx.statusMessage = "Pattern not found: " + ctx.searchQuery;
                return;
            }
        }
        else
        {
            ctx.statusMessage = "No previous search";
            return;
        }
    }

    int prevIndex;
    if(ctx.searchForward)
    {
        prevIndex = (ctx.currentMatchIndex - 1 + ctx.searchMatches.size()) %
                    ctx.searchMatches.size();
    }
    else
    {
        prevIndex = (ctx.currentMatchIndex + 1) % ctx.searchMatches.size();
    }

    jumpToMatch(prevIndex);
    ctx.statusMessage = "Match " + std::to_string(ctx.currentMatchIndex + 1) +
                        " of " + std::to_string(ctx.searchMatches.size());
}

void SearchEngine::clearSearch()
{
    ctx.searchQuery.clear();
    ctx.searchMatches.clear();
    ctx.currentMatchIndex = -1;
    ctx.needsFullRedraw = true;
}

void SearchEngine::cancelSearch()
{
    ctx.searchQuery.clear();
    ctx.currentMode = Mode::NORMAL;
    ctx.statusMessage.clear();
}

bool SearchEngine::isInSearchMatch(int row, int col) const
{
    for(const auto& match : ctx.searchMatches)
    {
        if(match.line == row && col >= match.startCol && col <= match.endCol)
        {
            return true;
        }
    }
    return false;
}
