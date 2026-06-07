#include "ansi_tools_mode.h"

#include "ascii.h"
#include "editor.h"
#include "key_enums.h"
#include "mode_state_machine.h"
#include "normal_mode.h"
#include "terminal.h"
#include "text_utils.h"
#include "welcome_mode.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <string_view>

namespace editor::statemachine
{
namespace
{
struct AnsiToolEntry
{
    std::string_view literal;
    std::string_view description;
};

constexpr std::array<AnsiToolEntry, 17> kAnsiTools = {{
    {"\\x1b[0m", "Reset all text attributes and colors"},
    {"\\x1b[39m", "Reset foreground color to default"},
    {"\\x1b[49m", "Reset background color to default"},
    {"\\x1b[22m", "Reset bold and dim intensity"},
    {"\\x1b[23m", "Turn italic off"},
    {"\\x1b[24m", "Turn underline off"},
    {"\\x1b[27m", "Turn reverse video off"},
    {"\\x1b[2J", "Clear entire screen"},
    {"\\x1b[H", "Move cursor to home position"},
    {"\\x1b[2J\\x1b[H", "Clear screen and move cursor home"},
    {"\\x1b[K", "Clear line from cursor to end"},
    {"\\x1b[1K", "Clear line from start to cursor"},
    {"\\x1b[2K", "Clear entire current line"},
    {"\\x1b[s", "Save cursor position"},
    {"\\x1b[u", "Restore cursor position"},
    {"\\x1b[?25l", "Hide terminal cursor"},
    {"\\x1b[?25h", "Show terminal cursor"},
}};

int toolCount()
{
    return (int)kAnsiTools.size();
}

int popupWidth(int screenCols)
{
    return std::min(100, std::max(1, screenCols - 2));
}

int popupHeight(int screenRows)
{
    return std::min(22, std::max(1, screenRows - 2));
}

void appendPadded(std::string& out, std::string_view text, int width)
{
    int used = 0;
    std::size_t pos = 0;
    while(pos < text.size() && used < width)
    {
        const int next = text_utils::nextUtf8CharStart(text, (int)pos);
        const std::string_view ch = text.substr(pos, next - pos);
        const int chWidth = text_utils::utf8DisplayWidth(ch);
        if(used + chWidth > width)
            break;
        out.append(ch.data(), ch.size());
        used += chWidth;
        pos = (std::size_t)next;
    }
    const int pad = width - used;
    if(pad > 0)
        out.append(pad, ' ');
}

ModeState exitState(ModeContext& ctx)
{
    return ctx.hasBuffer() ? ModeState{NormalMode{}} : ModeState{WelcomeMode{}};
}

void appendEntry(std::string& out, const Theme& theme,
                 const AnsiToolEntry& entry, bool selected, int width)
{
    constexpr int codeWidth = 22;
    out += selected ? theme.selection() : theme.baseFg();
    out += selected ? "> " : "  ";
    appendPadded(out, entry.literal, codeWidth);
    out += " ";
    appendPadded(out, entry.description, std::max(0, width - codeWidth - 3));
    out += theme.reset();
}
} // namespace

void AnsiToolsMode::on_enter(ModeContext& ctx)
{
    cursor = std::clamp(cursor, 0, toolCount() - 1);
    rowOffset = 0;
    backdropDrawn = false;
    backdropRows = 0;
    backdropCols = 0;
    ctx.requestFullRedraw();
    Terminal::setCursorBlock();
}

void AnsiToolsMode::on_exit(ModeContext& ctx)
{
    backdropDrawn = false;
    ctx.requestFullRedraw();
}

void AnsiToolsMode::clampToVisible(int visibleRows)
{
    cursor = std::clamp(cursor, 0, toolCount() - 1);
    if(cursor < rowOffset)
        rowOffset = cursor;
    else if(cursor >= rowOffset + visibleRows)
        rowOffset = cursor - visibleRows + 1;

    const int maxOffset = std::max(0, toolCount() - visibleRows);
    rowOffset = std::clamp(rowOffset, 0, maxOffset);
}

std::optional<ModeState> AnsiToolsMode::handle(ModeContext& ctx,
                                               const ModeKeyEvent& event)
{
    const int c = keyCode(event.key);
    const int visibleRows = std::max(1, popupHeight(ctx.screenRows()) - 4);

    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(typed::TypedKey::KEY_Q))
    {
        return exitState(ctx);
    }

    if(c == keyCode(typed::TypedKey::KEY_K) ||
       c == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        if(cursor > 0)
            --cursor;
        clampToVisible(visibleRows);
        ctx.requestFullRedraw();
        return std::nullopt;
    }

    if(c == keyCode(typed::TypedKey::KEY_J) ||
       c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        if(cursor + 1 < toolCount())
            ++cursor;
        clampToVisible(visibleRows);
        ctx.requestFullRedraw();
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::ENTER) ||
       c == keyCode(control::ControlKey::CTRL_M))
    {
        if(ctx.editor && ctx.editor->hasBuffer())
        {
            const AnsiToolEntry& entry = kAnsiTools[(std::size_t)cursor];
            for(char ch : entry.literal)
                ctx.editor->insertChar(ch);
            ctx.editor->saveState();
            ctx.setStatusMessage("inserted ANSI tool escape");
            ctx.requestFullRedraw();
        }
        return exitState(ctx);
    }

    return std::nullopt;
}

void AnsiToolsMode::draw(Editor& editor) const
{
    if(!backdropDrawn || backdropRows != editor.screenRows ||
       backdropCols != editor.screenCols)
    {
        editor.drawFullScreenSingle();
        backdropDrawn = true;
        backdropRows = editor.screenRows;
        backdropCols = editor.screenCols;
    }

    const int width = popupWidth(editor.screenCols);
    const int height = popupHeight(editor.screenRows);
    const int left = std::max(1, (editor.screenCols - width) / 2 + 1);
    const int top = std::max(1, (editor.screenRows - height) / 2 + 1);
    const int innerWidth = std::max(1, width - 2);
    const int visibleRows = std::max(1, height - 4);
    const int maxOffset = std::max(0, toolCount() - visibleRows);
    const int firstRow = std::clamp(rowOffset, 0, maxOffset);

    std::string output;
    output.reserve((size_t)width * (size_t)height * 2);
    output += Terminal::ESC_HIDE_CURSOR;

    auto moveTo = [&](int row, int col)
    { output += Terminal::cursorPos(row, col); };

    moveTo(top, left);
    output += editor.theme.uiDim();
    text_utils::appendU8(output, ascii::BOX_ROUNDED_TOP_LEFT);
    text_utils::appendUtf8Repeat(output, ascii::BOX_LIGHT_HORIZONTAL,
                                 innerWidth);
    text_utils::appendU8(output, ascii::BOX_ROUNDED_TOP_RIGHT);

    moveTo(top + 1, left);
    text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
    output += editor.theme.uiAccent();
    output += Terminal::ESC_BOLD;
    appendPadded(output, " ANSI toolbox", innerWidth);
    output += editor.theme.uiDim();
    text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);

    for(int r = 0; r < visibleRows; ++r)
    {
        moveTo(top + 2 + r, left);
        output += editor.theme.uiDim();
        text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);

        const int index = firstRow + r;
        if(index < toolCount())
        {
            appendEntry(output, editor.theme, kAnsiTools[(std::size_t)index],
                        index == cursor, innerWidth);
        }
        else
        {
            output.append(innerWidth, ' ');
        }

        output += editor.theme.uiDim();
        text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
    }

    moveTo(top + height - 2, left);
    output += editor.theme.uiDim();
    text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
    char footer[80];
    std::snprintf(footer, sizeof(footer),
                  " j/k move  Enter insert  q/Esc cancel  %d/%zu", cursor + 1,
                  kAnsiTools.size());
    appendPadded(output, footer, innerWidth);
    output += editor.theme.uiDim();
    text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);

    moveTo(top + height - 1, left);
    text_utils::appendU8(output, ascii::BOX_ROUNDED_BOTTOM_LEFT);
    text_utils::appendUtf8Repeat(output, ascii::BOX_LIGHT_HORIZONTAL,
                                 innerWidth);
    text_utils::appendU8(output, ascii::BOX_ROUNDED_BOTTOM_RIGHT);
    output += editor.theme.reset();

    const bool syncOutput = Terminal::useSynchronizedOutput();
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_BEGIN);
    Terminal::write(output);
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_END);
    Terminal::flush();
}
} // namespace editor::statemachine
