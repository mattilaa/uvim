#include "editor.h"
#include "terminal.h"
#include <algorithm>

void Editor::initializeBufferBrowser()
{
    bufferQuery.clear();
    bufferCursor = 0;
    bufferOffset = 0;
    updateBufferMatches();
}

void Editor::updateBufferMatches()
{
    bufferMatches.clear();

    for(size_t i = 0; i < buffers.size(); i++)
    {
        BufferMatch m;
        m.bufferIndex = (int)i;

        std::string name =
            buffers[i]->filename.empty() ? "[No Name]" : buffers[i]->filename;
        std::string base = name;
        if(!buffers[i]->filename.empty())
        {
            size_t lastSlash = buffers[i]->filename.find_last_of("/\\");
            if(lastSlash != std::string::npos)
                base = buffers[i]->filename.substr(lastSlash + 1);
        }

        m.display = std::to_string(i + 1);
        if((int)i == currentBufferIndex)
            m.display += " *";
        else
            m.display += "  ";

        if(buffers[i]->dirty)
            m.display += " [+] ";
        else
            m.display += "     ";

        m.display += base;
        if(!buffers[i]->filename.empty() && base != buffers[i]->filename)
        {
            m.display += "  (" + buffers[i]->filename + ")";
        }

        if(bufferQuery.empty())
        {
            m.score = 0;
            bufferMatches.push_back(std::move(m));
            continue;
        }

        std::vector<int> posDisplay;
        int s1 = fuzzyScore(bufferQuery, m.display, posDisplay);

        std::vector<int> posName;
        int s2 = buffers[i]->filename.empty()
                     ? -1
                     : fuzzyScore(bufferQuery, buffers[i]->filename, posName);
        std::vector<int> posBase;
        int s3 = fuzzyScore(bufferQuery, base, posBase);

        int best = std::max({s1, s2, s3 * 2});

        if(best > 0)
        {
            m.score = best;
            if(best == s1)
                m.matchPositions = posDisplay;
            else if(best == s3 * 2)
                m.matchPositions = posBase;
            else
                m.matchPositions.clear();
            bufferMatches.push_back(std::move(m));
        }
    }

    if(!bufferQuery.empty())
    {
        std::sort(bufferMatches.begin(), bufferMatches.end(),
                  [](const BufferMatch& a, const BufferMatch& b)
                  { return a.score > b.score; });
    }
    else
    {
        std::sort(bufferMatches.begin(), bufferMatches.end(),
                  [](const BufferMatch& a, const BufferMatch& b)
                  { return a.bufferIndex < b.bufferIndex; });
    }

    if(bufferCursor >= (int)bufferMatches.size())
    {
        bufferCursor = 0;
        bufferOffset = 0;
    }
}

void Editor::drawBufferBrowser()
{
    std::string output;
    output.reserve(screenRows * screenCols * 2);

    output += Terminal::ESC_CURSOR_HOME;
    output += Terminal::ESC_CLEAR_LINE;

    output += Terminal::ESC_BOLD;
    output += "  Buffers: ";
    output += Terminal::ESC_RESET_ALL;
    output += Terminal::FG_GREEN;
    output += bufferQuery;
    output += Terminal::ESC_BLINK;
    output += "_";
    output += Terminal::ESC_BLINK_OFF;
    output += Terminal::FG_DEFAULT;

    output += Terminal::NEWLINE_CLEAR;
    output += Terminal::FG_BRIGHT_BLACK;
    output += "  [Enter: switch] [Esc: cancel] [↑↓: navigate]";
    output += Terminal::FG_DEFAULT;

    output += Terminal::NEWLINE_CLEAR;
    output += Terminal::FG_BRIGHT_BLACK;
    if(!bufferMatches.empty())
    {
        output += "  " + std::to_string(bufferMatches.size()) + " matches";
    }
    else if(!bufferQuery.empty())
    {
        output += "  No matches";
    }
    else
    {
        output += "  " + std::to_string(buffers.size()) + " buffers";
    }
    output += Terminal::FG_DEFAULT;

    int availableRows = screenRows - 3;

    for(int i = 0;
        i < availableRows && i + bufferOffset < (int)bufferMatches.size(); i++)
    {
        output += Terminal::NEWLINE_CLEAR;
        int idx = i + bufferOffset;
        const BufferMatch& m = bufferMatches[idx];

        if(idx == bufferCursor)
            output += Terminal::STYLE_SELECTION;

        std::string line = "  " + m.display;
        if((int)line.length() > screenCols)
            line = line.substr(0, screenCols);

        output += line;
        output += Terminal::ESC_RESET_ALL;
    }

    for(int i = (int)bufferMatches.size() - bufferOffset; i < availableRows;
        i++)
    {
        output += Terminal::NEWLINE_CLEAR;
        output += Terminal::FG_BLUE;
        output += "~";
        output += Terminal::FG_DEFAULT;
    }

    Terminal::write(output);
    Terminal::flush();
}

void Editor::selectBufferMatch()
{
    if(bufferCursor >= 0 && bufferCursor < (int)bufferMatches.size())
    {
        int idx = bufferMatches[bufferCursor].bufferIndex;
        if(idx >= 0 && idx < (int)buffers.size())
        {
            switchToBuffer(idx);
        }
        setMode(NORMAL);
    }
}

void Editor::handleBufferBrowserMode(int c)
{
    if(dispatchModeKey(c))
    {
        return;
    }

    switch(c)
    {
    case Terminal::ENTER:
        selectBufferMatch();
        break;

    case Terminal::ESC:
        setMode(NORMAL);
        needsFullRedraw = true;
        break;

    case Terminal::ARROW_DOWN:
    case Terminal::CTRL_N:
    case Terminal::CTRL_J:
        if(bufferCursor < (int)bufferMatches.size() - 1)
        {
            bufferCursor++;
            if(bufferCursor >= bufferOffset + screenRows - 3)
                bufferOffset = bufferCursor - screenRows + 4;
        }
        break;

    case Terminal::ARROW_UP:
    case Terminal::CTRL_P:
    case Terminal::CTRL_K:
        if(bufferCursor > 0)
        {
            bufferCursor--;
            if(bufferCursor < bufferOffset)
                bufferOffset = bufferCursor;
        }
        break;

    case Terminal::BACKSPACE:
    case Terminal::DEL:
        if(!bufferQuery.empty())
        {
            bufferQuery.pop_back();
            updateBufferMatches();
            bufferCursor = 0;
            bufferOffset = 0;
        }
        break;

    case Terminal::CTRL_U:
        bufferQuery.clear();
        updateBufferMatches();
        bufferCursor = 0;
        bufferOffset = 0;
        break;

    default:
        if(c >= 32 && c < 127)
        {
            bufferQuery += static_cast<char>(c);
            updateBufferMatches();
            bufferCursor = 0;
            bufferOffset = 0;
        }
        break;
    }
}
