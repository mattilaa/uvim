#include "color_constant.h"
#include "editor.h"
#include "editor_utils.h"
#include "header_help.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>

namespace editor::statemachine
{
namespace
{
std::vector<std::string> bufferBrowserHelpTokens()
{
    return {"[Enter: switch]", "[Ctrl+X: close]",
            "[Ctrl+Shift+X: close matches]", "[Esc: cancel]",
            "[Ctrl+J/K: navigate]"};
}

int bufferBrowserVisibleRows(const Editor& editor)
{
    const int headerRows =
        2 + HeaderHelp::lineCount(bufferBrowserHelpTokens(), editor.screenCols);
    return std::max(1, editor.screenRows - headerRows);
}

int decimalWidth(size_t value)
{
    int width = 1;
    while(value >= 10)
    {
        value /= 10;
        ++width;
    }
    return width;
}

std::string utf8PrefixByWidth(std::string_view text, int maxWidth)
{
    if(maxWidth <= 0)
        return "";
    int width = 0;
    int pos = 0;
    while(pos < static_cast<int>(text.size()))
    {
        int next = text_utils::nextUtf8CharStart(text, pos);
        int charWidth =
            text_utils::utf8DisplayWidth(text.substr(pos, next - pos));
        if(width + charWidth > maxWidth)
            break;
        width += charWidth;
        pos = next;
    }
    return std::string(text.substr(0, pos));
}

std::string utf8SuffixByWidth(std::string_view text, int maxWidth)
{
    if(maxWidth <= 0)
        return "";
    int width = 0;
    int pos = static_cast<int>(text.size());
    while(pos > 0)
    {
        int prev = text_utils::prevUtf8CharStart(text, pos);
        int charWidth =
            text_utils::utf8DisplayWidth(text.substr(prev, pos - prev));
        if(width + charWidth > maxWidth)
            break;
        width += charWidth;
        pos = prev;
    }
    return std::string(text.substr(pos));
}

std::string truncatePathFromFront(std::string_view path, int maxWidth)
{
    if(maxWidth <= 0)
        return "";
    if(text_utils::utf8DisplayWidth(path) <= maxWidth)
        return std::string(path);
    if(maxWidth == 1)
        return "…";
    return "…" + utf8SuffixByWidth(path, maxWidth - 1);
}
} // namespace

void BufferBrowserMode::on_enter(ModeContext& ctx)
{
    bufferQuery.clear();
    bufferCursor = 0;
    bufferOffset = 0;
    updateMatches(*ctx.editor);

    auto current = std::find_if(
        bufferMatches.begin(), bufferMatches.end(), [&](const BufferMatch& m)
        { return m.bufferIndex == ctx.editor->currentBufferIndex; });
    if(current != bufferMatches.end())
    {
        bufferCursor = static_cast<int>(current - bufferMatches.begin());
        const int visible = bufferBrowserVisibleRows(*ctx.editor);
        if(bufferCursor >= visible)
            bufferOffset = bufferCursor - visible + 1;
    }
    ctx.editor->needsFullRedraw = true;
}

void BufferBrowserMode::on_exit(ModeContext& /* ctx */) {}

std::optional<ModeState> BufferBrowserMode::handle(ModeContext& ctx,
                                                   const ModeKeyEvent& event)
{
    const int key = event.key;
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    if(c == keyCode(control::ControlKey::ESC))
    {
        ed->noteDoubleEscStatusClear();
        return defaultExitMode(ed);
    }

    if(c == keyCode(control::ControlKey::ENTER))
    {
        selectMatch(*ed);
        return defaultExitMode(ed);
    }
    if(c == keyCode(control::ControlKey::CTRL_X))
    {
        closeSelectedBuffer(*ed);
        if(!ed->hasBuffer())
            return defaultExitMode(ed);
    }
    if(c == keyCode(control::ControlKey::SHIFT_CTRL_X))
    {
        closeMatchedBuffers(*ed);
        if(!ed->hasBuffer())
            return defaultExitMode(ed);
    }

    if(c == keyCode(control::ControlKey::CTRL_J) ||
       c == keyCode(navigation::NavigationKey::ARROW_DOWN) ||
       c == keyCode(control::ControlKey::CTRL_N))
    {
        if(bufferCursor < (int)bufferMatches.size() - 1)
        {
            bufferCursor++;
            int visible = bufferBrowserVisibleRows(*ed);
            if(bufferCursor >= bufferOffset + visible)
                bufferOffset = bufferCursor - visible + 1;
        }
    }
    else if(c == keyCode(control::ControlKey::CTRL_K) ||
            c == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        if(bufferCursor > 0)
        {
            bufferCursor--;
            if(bufferCursor < bufferOffset)
                bufferOffset = bufferCursor;
        }
    }
    else if(c == keyCode(control::ControlKey::CTRL_D))
    {
        int half = bufferBrowserVisibleRows(*ed) / 2;
        bufferCursor += half;
        if(bufferCursor >= (int)bufferMatches.size())
            bufferCursor = bufferMatches.size() - 1;
        int visible = bufferBrowserVisibleRows(*ed);
        if(bufferCursor >= bufferOffset + visible)
            bufferOffset = bufferCursor - visible + 1;
    }
    else if(c == keyCode(navigation::NavigationKey::PAGE_UP))
    {
        int half = bufferBrowserVisibleRows(*ed) / 2;
        bufferCursor -= half;
        if(bufferCursor < 0)
            bufferCursor = 0;
        if(bufferCursor < bufferOffset)
            bufferOffset = bufferCursor;
    }
    else if(c == keyCode(control::ControlKey::BACKSPACE) ||
            c == keyCode(control::ControlKey::DEL) ||
            c == keyCode(control::ControlKey::CTRL_H))
    {
        if(!bufferQuery.empty())
        {
            bufferQuery.pop_back();
            updateMatches(*ed);
            bufferCursor = 0;
            bufferOffset = 0;
        }
    }
    else if(c == keyCode(control::ControlKey::CTRL_U))
    {
        bufferQuery.clear();
        updateMatches(*ed);
        bufferCursor = 0;
        bufferOffset = 0;
    }
    else if(c == keyCode(control::ControlKey::CTRL_P))
    {
#ifdef UVIM_ENABLE_SEARCH_TOOLS
        return FuzzyFindMode{};
#else
        ctx.setStatusMessage("search tools are not compiled in");
        return std::nullopt;
#endif
    }
    else if(c == keyCode(control::ControlKey::CTRL_A) ||
            c == keyCode(command::CommandKey::KEY_SLASH))
    {
#ifdef UVIM_ENABLE_SEARCH_TOOLS
        return GrepSearchMode{};
#else
        ctx.setStatusMessage("search tools are not compiled in");
        return std::nullopt;
#endif
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
    output += editor.theme.reset();
    output += Terminal::ESC_CLEAR_LINE;

    output += Terminal::ESC_BOLD;
    output += "  Buffers: ";
    output += editor.theme.reset();
    output += editor.theme.uiPrompt();
    output += bufferQuery;
    output += Terminal::ESC_BLINK;
    output += "_";
    output += Terminal::ESC_BLINK_OFF;
    output += editor.theme.baseFg();

    HeaderHelp::append(output, editor.theme, editor.screenCols,
                       bufferBrowserHelpTokens());

    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.uiDim();
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
    output += editor.theme.baseFg();

    int availableRows = bufferBrowserVisibleRows(editor);
    const int numberWidth = decimalWidth(editor.buffers.size());

    for(int i = 0;
        i < availableRows && i + bufferOffset < (int)bufferMatches.size(); i++)
    {
        output += Terminal::NEWLINE_CLEAR;
        int idx = i + bufferOffset;
        const BufferMatch& m = bufferMatches[idx];
        if(m.bufferIndex < 0 ||
           m.bufferIndex >= static_cast<int>(editor.buffers.size()))
            continue;

        const Buffer& buffer = *editor.buffers[m.bufferIndex];
        const bool isCurrent = m.bufferIndex == editor.currentBufferIndex;

        if(idx == bufferCursor && isCurrent)
        {
            output += color::rgbBg(56, 120, 72);
            output += editor.theme.baseFg();
        }
        else if(idx == bufferCursor)
        {
            output += editor.theme.selection();
        }
        else if(isCurrent)
        {
            output += color::rgbBg(24, 64, 36);
            output += editor.theme.baseFg();
        }

        const std::string number = std::to_string(m.bufferIndex + 1);
        std::string prefix = "  ";
        prefix.append(std::max(0, numberWidth - static_cast<int>(number.size())),
                      ' ');
        prefix += number;
        prefix += " ";

        const std::string filename =
            buffer.filename.empty()
                ? "[No Name]"
                : std::string(text_utils::basename(buffer.filename));
        std::string status;
        if(isCurrent)
            status += " *";
        if(buffer.dirty)
            status += " [+]";

        int remaining = editor.screenCols - text_utils::displayWidth(prefix);
        std::string nameAndStatus = filename + status;
        if(text_utils::utf8DisplayWidth(nameAndStatus) > remaining)
            nameAndStatus = utf8PrefixByWidth(nameAndStatus, remaining);

        output += editor.theme.uiDim();
        output += prefix;
        output += editor.theme.uiInfo();
        output += nameAndStatus;

        const bool hasPath = !buffer.filename.empty() &&
                             filename != buffer.filename;
        remaining -= text_utils::utf8DisplayWidth(nameAndStatus);
        if(hasPath && remaining > 2)
        {
            output += "  ";
            output += editor.theme.uiDim();
            output += truncatePathFromFront(buffer.filename, remaining - 2);
        }
        output += editor.theme.reset();
    }

    for(int i = (int)bufferMatches.size() - bufferOffset; i < availableRows;
        i++)
    {
        output += Terminal::NEWLINE_CLEAR;
        output += editor.theme.uiGutter();
        output += "~";
        output += editor.theme.baseFg();
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
        std::string base = editor.buffers[i]->filename.empty()
                               ? name
                               : std::string(text_utils::basename(
                                     editor.buffers[i]->filename));

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
        int s1 = editor::helper::fuzzyScoreWithPositions(bufferQuery, m.display,
                                                         posDisplay);

        std::vector<int> posName;
        int s2 = editor.buffers[i]->filename.empty()
                     ? -1
                     : editor::helper::fuzzyScoreWithPositions(
                           bufferQuery, editor.buffers[i]->filename, posName);
        std::vector<int> posBase;
        int s3 =
            editor::helper::fuzzyScoreWithPositions(bufferQuery, base, posBase);

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
            if(idx != editor.currentBufferIndex)
                editor.pushJumpLocation();
            editor.switchToBuffer(idx);
        }
    }
}

void BufferBrowserMode::closeSelectedBuffer(Editor& editor)
{
    if(bufferCursor < 0 || bufferCursor >= (int)bufferMatches.size())
        return;

    int idx = bufferMatches[bufferCursor].bufferIndex;
    if(idx < 0 || idx >= (int)editor.buffers.size())
        return;

    if(editor.buffers[idx]->dirty)
    {
        editor.setStatusMessage(
            "No write since last change (add ! to override)");
        return;
    }

    if(editor.buffers.size() == 1)
    {
        editor.buffers.erase(editor.buffers.begin());
        editor.currentBufferIndex = -1;
        editor.clearCurrentBufferPointers();
        editor.splitActive = false;
        editor.splitPanes.clear();
        editor.splitTabBarOffset.clear();
        editor.splitNodes.clear();
        editor.splitRoot = -1;
        editor.splitPaneLayouts.clear();
        editor.currentMode = WELCOME;
        editor.needsFullRedraw = true;
        bufferMatches.clear();
        bufferCursor = 0;
        bufferOffset = 0;
        return;
    }

    if(idx == editor.currentBufferIndex)
    {
        editor.switchToBuffer(idx);
        editor.closeCurrentBuffer();
    }
    else
    {
        int previousCurrent = editor.currentBufferIndex;
        editor.buffers.erase(editor.buffers.begin() + idx);
        if(previousCurrent > idx)
            previousCurrent--;
        editor.currentBufferIndex = previousCurrent;

        if(editor.splitActive)
        {
            for(auto& pane : editor.splitPanes)
            {
                int& paneIndex = pane.bufferIndex;
                if(paneIndex == idx)
                    paneIndex = editor.currentBufferIndex;
                else if(paneIndex > idx)
                    paneIndex--;
            }
            editor.currentBufferIndex =
                editor.splitPanes[editor.activePane].bufferIndex;
        }

        editor.updateCurrentBufferPointers();
        editor.restoreBufferState();
        editor.needsFullRedraw = true;
    }

    updateMatches(editor);
    if(bufferCursor >= (int)bufferMatches.size())
        bufferCursor = std::max(0, (int)bufferMatches.size() - 1);
    if(bufferOffset > bufferCursor)
        bufferOffset = bufferCursor;
}

void BufferBrowserMode::closeMatchedBuffers(Editor& editor)
{
    if(bufferQuery.empty() || bufferMatches.empty())
    {
        editor.setStatusMessage("No searched buffers to close");
        return;
    }

    std::vector<int> indices;
    indices.reserve(bufferMatches.size());
    for(const auto& match : bufferMatches)
    {
        int idx = match.bufferIndex;
        if(idx >= 0 && idx < (int)editor.buffers.size() &&
           !editor.buffers[idx]->dirty)
        {
            indices.push_back(idx);
        }
    }

    if(indices.empty())
    {
        editor.setStatusMessage("No searched buffers closed");
        return;
    }

    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

    for(auto it = indices.rbegin(); it != indices.rend(); ++it)
    {
        int idx = *it;
        int previousCurrent = editor.currentBufferIndex;
        editor.buffers.erase(editor.buffers.begin() + idx);

        if(editor.buffers.empty())
        {
            editor.currentBufferIndex = -1;
            editor.clearCurrentBufferPointers();
            editor.splitActive = false;
            editor.splitPanes.clear();
            editor.splitTabBarOffset.clear();
            editor.splitNodes.clear();
            editor.splitRoot = -1;
            editor.splitPaneLayouts.clear();
            editor.currentMode = WELCOME;
            editor.needsFullRedraw = true;
            bufferMatches.clear();
            bufferCursor = 0;
            bufferOffset = 0;
            return;
        }

        if(previousCurrent == idx)
        {
            editor.currentBufferIndex =
                std::min(idx, (int)editor.buffers.size() - 1);
        }
        else
        {
            editor.currentBufferIndex =
                previousCurrent > idx ? previousCurrent - 1 : previousCurrent;
        }

        if(editor.splitActive)
        {
            for(auto& pane : editor.splitPanes)
            {
                int& paneIndex = pane.bufferIndex;
                if(paneIndex == idx)
                    paneIndex = editor.currentBufferIndex;
                else if(paneIndex > idx)
                    paneIndex--;
            }
            editor.currentBufferIndex =
                editor.splitPanes[editor.activePane].bufferIndex;
        }
    }

    editor.updateCurrentBufferPointers();
    editor.restoreBufferState();
    editor.needsFullRedraw = true;
    updateMatches(editor);
    if(bufferCursor >= (int)bufferMatches.size())
        bufferCursor = std::max(0, (int)bufferMatches.size() - 1);
    if(bufferOffset > bufferCursor)
        bufferOffset = bufferCursor;
}
} // namespace editor::statemachine
