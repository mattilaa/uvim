#include "buffer_browser.h"
#include "editor.h"
#include "terminal.h"
#include <algorithm>

void BufferBrowser::initialize(Editor& editor)
{
    bufferQuery.clear();
    bufferCursor = 0;
    bufferOffset = 0;
    updateMatches(editor);
}

void BufferBrowser::updateMatches(Editor& editor)
{
    bufferMatches.clear();

    for(size_t i = 0; i < editor.buffers.size(); i++)
    {
        BufferMatch m;
        m.bufferIndex = (int)i;

        std::string name = editor.buffers[i]->filename.empty()
                               ? "[No Name]"
                               : editor.buffers[i]->filename;
        std::string base = name;
        if(!editor.buffers[i]->filename.empty())
        {
            size_t lastSlash = editor.buffers[i]->filename.find_last_of("/\\");
            if(lastSlash != std::string::npos)
                base = editor.buffers[i]->filename.substr(lastSlash + 1);
        }

        m.display = std::to_string(i + 1);
        if((int)i == editor.currentBufferIndex)
            m.display += " *";
        else
            m.display += "  ";

        if(editor.buffers[i]->dirty)
            m.display += " [+] ";
        else
            m.display += "     ";

        m.display += base;
        if(!editor.buffers[i]->filename.empty() &&
           base != editor.buffers[i]->filename)
        {
            m.display += "  (" + editor.buffers[i]->filename + ")";
        }

        if(bufferQuery.empty())
        {
            m.score = 0;
            bufferMatches.push_back(std::move(m));
            continue;
        }

        std::vector<int> posDisplay;
        int s1 = editor.fuzzyScore(bufferQuery, m.display, posDisplay);

        std::vector<int> posName;
        int s2 = editor.buffers[i]->filename.empty()
                     ? -1
                     : editor.fuzzyScore(bufferQuery,
                                         editor.buffers[i]->filename, posName);
        std::vector<int> posBase;
        int s3 = editor.fuzzyScore(bufferQuery, base, posBase);

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

void BufferBrowser::draw(Editor& editor) const
{
    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

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
    output += "  [Enter: switch] [Esc: cancel] [Ctrl+J/K: navigate]";
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
        output += "  " + std::to_string(editor.buffers.size()) + " buffers";
    }
    output += Terminal::FG_DEFAULT;

    int availableRows = editor.screenRows - 3;

    for(int i = 0; i < availableRows &&
                    i + bufferOffset < (int)bufferMatches.size();
        i++)
    {
        output += Terminal::NEWLINE_CLEAR;
        int idx = i + bufferOffset;
        const BufferMatch& m = bufferMatches[idx];

        if(idx == bufferCursor)
            output += Terminal::STYLE_SELECTION;

        std::string line = "  " + m.display;
        if((int)line.length() > editor.screenCols)
            line = line.substr(0, editor.screenCols);

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

void BufferBrowser::selectMatch(Editor& editor)
{
    if(bufferCursor >= 0 && bufferCursor < (int)bufferMatches.size())
    {
        int idx = bufferMatches[bufferCursor].bufferIndex;
        if(idx >= 0 && idx < (int)editor.buffers.size())
        {
            editor.switchToBuffer(idx);
        }
    }
}

void BufferBrowser::up(int screenRows)
{
    if(bufferCursor > 0)
    {
        bufferCursor--;
        if(bufferCursor < bufferOffset)
            bufferOffset = bufferCursor;
    }
}

void BufferBrowser::down(int screenRows)
{
    if(bufferCursor < (int)bufferMatches.size() - 1)
    {
        bufferCursor++;
        int visible = screenRows - 4;
        if(bufferCursor >= bufferOffset + visible)
            bufferOffset = bufferCursor - visible + 1;
    }
}

void BufferBrowser::start()
{
    bufferCursor = 0;
    bufferOffset = 0;
}

void BufferBrowser::end(int screenRows)
{
    bufferCursor = bufferMatches.size() - 1;
    int visible = screenRows - 4;
    if(bufferCursor >= visible)
        bufferOffset = bufferCursor - visible + 1;
}

void BufferBrowser::halfPageUp(int screenRows)
{
    int half = (screenRows - 4) / 2;
    bufferCursor -= half;
    if(bufferCursor < 0)
        bufferCursor = 0;
    if(bufferCursor < bufferOffset)
        bufferOffset = bufferCursor;
}

void BufferBrowser::halfPageDown(int screenRows)
{
    int half = (screenRows - 4) / 2;
    bufferCursor += half;
    if(bufferCursor >= (int)bufferMatches.size())
        bufferCursor = bufferMatches.size() - 1;
    int visible = screenRows - 4;
    if(bufferCursor >= bufferOffset + visible)
        bufferOffset = bufferCursor - visible + 1;
}

void BufferBrowser::addChar(Editor& editor, char c)
{
    bufferQuery += c;
    updateMatches(editor);
    bufferCursor = 0;
    bufferOffset = 0;
}

void BufferBrowser::backspace(Editor& editor)
{
    if(!bufferQuery.empty())
    {
        bufferQuery.pop_back();
        updateMatches(editor);
        bufferCursor = 0;
        bufferOffset = 0;
    }
}

void BufferBrowser::clear(Editor& editor)
{
    bufferQuery.clear();
    updateMatches(editor);
    bufferCursor = 0;
    bufferOffset = 0;
}

bool BufferBrowser::selectEntry(Editor& editor)
{
    selectMatch(editor);
    return true;
}

void BufferBrowser::deleteSelected(Editor& editor)
{
    if(bufferCursor >= 0 && bufferCursor < (int)bufferMatches.size())
    {
        int idx = bufferMatches[bufferCursor].bufferIndex;
        if(idx != editor.currentBufferIndex && idx >= 0 &&
           idx < (int)editor.buffers.size())
        {
            editor.buffers.erase(editor.buffers.begin() + idx);
            if(editor.currentBufferIndex > idx)
                editor.currentBufferIndex--;
            editor.updateCurrentBufferPointers();
            updateMatches(editor);
        }
    }
}

bool BufferBrowser::switchToBufferByNumber(Editor& editor, int num)
{
    if(num >= 1 && num <= (int)editor.buffers.size())
    {
        editor.switchToBuffer(num - 1);
        return true;
    }
    return false;
}
