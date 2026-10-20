#include "function_browser_mode.h"

#include "editor.h"
#include "editor_utils.h"
#include "header_help.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#include "widgets/status_bar.h"

#include <algorithm>

namespace editor::statemachine
{
namespace
{
std::vector<std::string> helpTokens()
{
    return {"[Enter: jump]", "[Esc: cancel]", "[Ctrl+J/K: navigate]",
            "[PgUp/PgDn: page]", "[Ctrl+U: clear]"};
}

int visibleRows(const Editor& editor)
{
    return std::max(1,
                    editor.screenRows - 1 -
                        HeaderHelp::lineCount(helpTokens(), editor.screenCols));
}

std::string clipped(std::string_view text, int columns)
{
    int width = 0;
    int end = 0;
    while(end < static_cast<int>(text.size()))
    {
        const int next = text_utils::nextUtf8CharStart(text, end);
        const int charWidth =
            text_utils::utf8DisplayWidth(text.substr(end, next - end));
        if(width + charWidth > columns)
            break;
        width += charWidth;
        end = next;
    }
    return std::string(text.substr(0, end));
}
} // namespace

void FunctionBrowserMode::on_enter(ModeContext& ctx)
{
    Editor& editor = *ctx.editor;
    sourceIndex = editor.currentBufferIndex;
    sourceBuffer = editor.currentBuffer;
    sourceName = sourceBuffer && !sourceBuffer->filename.empty()
                     ? sourceBuffer->filename
                     : "[No Name]";
    query.clear();
    symbols.clear();
    cursor = offset = 0;
    const FileType type = structural_navigation::fileType(editor);
    if(!sourceBuffer)
        emptyMessage = "No buffer";
    else if(!structural_navigation::supports(type))
        emptyMessage =
            "Function signatures are not supported for this file type";
    else
    {
        symbols = structural_navigation::functionSymbols(*editor.lines, type);
        emptyMessage = "No function signatures in this buffer";
    }
    updateMatches();
    // Start on the function at or immediately before the editor cursor.
    for(int i = 0; i < static_cast<int>(symbols.size()); ++i)
    {
        if(symbols[i].position.line > ctx.cursorY())
            break;
        cursor = i;
    }
    ensureVisible(editor);
    ctx.requestFullRedraw();
}

void FunctionBrowserMode::on_exit(ModeContext& ctx)
{
    ctx.requestFullRedraw();
}

void FunctionBrowserMode::updateMatches()
{
    matches.clear();
    std::vector<int> scores(symbols.size(), 0);
    for(int i = 0; i < static_cast<int>(symbols.size()); ++i)
    {
        std::vector<int> positions;
        const int score = query.empty()
                              ? 0
                              : editor::helper::fuzzyScoreWithPositions(
                                    query, symbols[i].signature, positions);
        if(query.empty() || score > 0)
        {
            matches.push_back(i);
            scores[i] = score;
        }
    }
    if(!query.empty())
        std::stable_sort(matches.begin(), matches.end(),
                         [&](int a, int b) { return scores[a] > scores[b]; });
    cursor = offset = 0;
}

void FunctionBrowserMode::ensureVisible(const Editor& editor)
{
    if(matches.empty())
    {
        cursor = offset = 0;
        return;
    }
    cursor = std::clamp(cursor, 0, static_cast<int>(matches.size()) - 1);
    const int rows = visibleRows(editor);
    offset = std::clamp(offset, 0,
                        std::max(0, static_cast<int>(matches.size()) - rows));
    if(cursor < offset)
        offset = cursor;
    if(cursor >= offset + rows)
        offset = cursor - rows + 1;
}

std::optional<ModeState> FunctionBrowserMode::handle(ModeContext& ctx,
                                                     const ModeKeyEvent& event)
{
    Editor& editor = *ctx.editor;
    const int key = keyCode(event.key);
    if(key == keyCode(control::ControlKey::ESC))
        return defaultExitMode(&editor);
    if(key == keyCode(control::ControlKey::ENTER))
    {
        if(matches.empty())
            return std::nullopt;
        if(sourceIndex < 0 ||
           sourceIndex >= static_cast<int>(editor.buffers.size()) ||
           editor.buffers[sourceIndex].get() != sourceBuffer)
        {
            ctx.setStatusMessage("Function source buffer is no longer open");
            return defaultExitMode(&editor);
        }
        const auto& target = symbols[matches[cursor]].position;
        editor.pushJumpLocation();
        if(editor.currentBufferIndex != sourceIndex)
            editor.switchToBuffer(sourceIndex);
        *editor.cursorY = std::clamp(
            target.line, 0, static_cast<int>(editor.lines->size()) - 1);
        *editor.cursorX =
            std::min(target.column,
                     static_cast<int>((*editor.lines)[*editor.cursorY].size()));
        *editor.wantedX = *editor.cursorX;
        editor.adjustViewport();
        return NormalMode{};
    }
    if(key == keyCode(control::ControlKey::CTRL_J) ||
       key == keyCode(control::ControlKey::CTRL_N) ||
       key == keyCode(navigation::NavigationKey::ARROW_DOWN))
        ++cursor;
    else if(key == keyCode(control::ControlKey::CTRL_K) ||
            key == keyCode(navigation::NavigationKey::ARROW_UP))
        --cursor;
    else if(key == keyCode(navigation::NavigationKey::PAGE_DOWN))
        cursor += visibleRows(editor);
    else if(key == keyCode(navigation::NavigationKey::PAGE_UP))
        cursor -= visibleRows(editor);
    else if(key == keyCode(control::ControlKey::CTRL_U))
    {
        query.clear();
        updateMatches();
    }
    else if(key == keyCode(control::ControlKey::BACKSPACE) ||
            key == keyCode(control::ControlKey::DEL) ||
            key == keyCode(control::ControlKey::CTRL_H))
    {
        if(!query.empty())
            query.pop_back();
        updateMatches();
    }
    else if(key >= 32 && key < 127)
    {
        query += static_cast<char>(key);
        updateMatches();
    }
    ensureVisible(editor);
    ctx.requestFullRedraw();
    return std::nullopt;
}

void FunctionBrowserMode::draw(Editor& editor)
{
    ensureVisible(editor);
    std::string output = Terminal::ESC_CURSOR_HOME;
    output += editor.theme.reset();
    output += Terminal::ESC_CLEAR_LINE;
    output += editor.theme.uiPrompt();
    output += clipped("  Functions: " + query + "_", editor.screenCols);
    output += editor.theme.reset();
    HeaderHelp::append(output, editor.theme, editor.screenCols, helpTokens());
    const int rows = visibleRows(editor);
    for(int row = 0; row < rows; ++row)
    {
        output += Terminal::NEWLINE_CLEAR;
        const int index = offset + row;
        if(index < static_cast<int>(matches.size()))
        {
            const auto& symbol = symbols[matches[index]];
            if(index == cursor)
                output += editor.theme.selection();
            const std::string prefix =
                (index == cursor ? "> " : "  ") +
                std::to_string(symbol.position.line + 1) + "  ";
            output += clipped(prefix + symbol.signature, editor.screenCols);
        }
        else if(matches.empty() && row == 0)
        {
            output += editor.theme.uiDim();
            output += clipped("  " + (query.empty()
                                          ? emptyMessage
                                          : "No matching function signatures"),
                              editor.screenCols);
        }
        else
        {
            output += editor.theme.uiGutter();
            output += "~";
        }
        output += editor.theme.reset();
    }
    widgets::ViewStatusBarView status{.theme = editor.theme};
    status.modeLabel = name();
    status.context = sourceName;
    status.state = std::to_string(matches.size()) + " functions";
    if(sourceIndex >= 0 &&
       sourceIndex < static_cast<int>(editor.buffers.size()) &&
       editor.buffers[sourceIndex].get() == sourceBuffer && sourceBuffer->dirty)
        status.flags.push_back("modified");
    status.index = matches.empty() ? 0 : cursor + 1;
    status.count = static_cast<int>(matches.size());
    editor.appendViewStatusBar(output, std::move(status));
    Terminal::write(output);
    Terminal::flush();
}
} // namespace editor::statemachine
