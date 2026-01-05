#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include <algorithm>

// ============================================================================
// BufferBrowserMode Implementation
// ============================================================================

void BufferBrowserMode::on_enter(ModeContext& ctx)
{
    bufferQuery.clear();
    bufferCursor = 0;
    bufferOffset = 0;
    updateMatches(*ctx.editor);
    ctx.editor->needsFullRedraw = true;
}

void BufferBrowserMode::on_exit(ModeContext& /* ctx */)
{
}

std::optional<ModeState> BufferBrowserMode::handle(ModeContext& ctx,
                                                   const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    if(c == Terminal::ESC)
    {
        return NormalMode{};
    }

    if(c == Terminal::ENTER)
    {
        selectMatch(*ed);
        return NormalMode{};
    }

    if(c == Terminal::CTRL_J || c == Terminal::ARROW_DOWN ||
       c == Terminal::CTRL_N)
    {
        if(bufferCursor < (int)bufferMatches.size() - 1)
        {
            bufferCursor++;
            int visible = ed->screenRows - 4;
            if(bufferCursor >= bufferOffset + visible)
                bufferOffset = bufferCursor - visible + 1;
        }
    }
    else if(c == Terminal::CTRL_K || c == Terminal::ARROW_UP)
    {
        if(bufferCursor > 0)
        {
            bufferCursor--;
            if(bufferCursor < bufferOffset)
                bufferOffset = bufferCursor;
        }
    }
    else if(c == Terminal::CTRL_D)
    {
        int half = (ed->screenRows - 4) / 2;
        bufferCursor += half;
        if(bufferCursor >= (int)bufferMatches.size())
            bufferCursor = bufferMatches.size() - 1;
        int visible = ed->screenRows - 4;
        if(bufferCursor >= bufferOffset + visible)
            bufferOffset = bufferCursor - visible + 1;
    }
    else if(c == Terminal::PAGE_UP)
    {
        int half = (ed->screenRows - 4) / 2;
        bufferCursor -= half;
        if(bufferCursor < 0)
            bufferCursor = 0;
        if(bufferCursor < bufferOffset)
            bufferOffset = bufferCursor;
    }
    else if(c == Terminal::BACKSPACE || c == Terminal::DEL ||
            c == Terminal::CTRL_H)
    {
        if(!bufferQuery.empty())
        {
            bufferQuery.pop_back();
            updateMatches(*ed);
            bufferCursor = 0;
            bufferOffset = 0;
        }
    }
    else if(c == Terminal::CTRL_U)
    {
        bufferQuery.clear();
        updateMatches(*ed);
        bufferCursor = 0;
        bufferOffset = 0;
    }
    else if(c == Terminal::CTRL_P)
    {
        return FuzzyFindMode{};
    }
    else if(c == Terminal::CTRL_S || c == '/')
    {
        return GrepSearchMode{};
    }
    else if(c >= 32 && c < 127)
    {
        bufferQuery += static_cast<char>(c);
        updateMatches(*ed);
        bufferCursor = 0;
        bufferOffset = 0;
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}

void BufferBrowserMode::draw(Editor& editor) const
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

    for(int i = 0;
        i < availableRows && i + bufferOffset < (int)bufferMatches.size(); i++)
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

void BufferBrowserMode::updateMatches(Editor& editor)
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

void BufferBrowserMode::selectMatch(Editor& editor)
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
