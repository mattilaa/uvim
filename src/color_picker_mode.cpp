#include "color_picker_mode.h"

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
struct ColorEntry
{
    std::string_view name;
    std::string_view code;
    std::string_view sample;
};

constexpr std::array<ColorEntry, 33> kColors = {{
    {"reset", "\\x1b[0m", "\x1b[0m"},
    {"fg black", "\\x1b[30m", "\x1b[40m"},
    {"fg red", "\\x1b[31m", "\x1b[41m"},
    {"fg green", "\\x1b[32m", "\x1b[42m"},
    {"fg yellow", "\\x1b[33m", "\x1b[43m"},
    {"fg blue", "\\x1b[34m", "\x1b[44m"},
    {"fg magenta", "\\x1b[35m", "\x1b[45m"},
    {"fg cyan", "\\x1b[36m", "\x1b[46m"},
    {"fg white", "\\x1b[37m", "\x1b[47m"},
    {"fg bright black", "\\x1b[90m", "\x1b[100m"},
    {"fg bright red", "\\x1b[91m", "\x1b[101m"},
    {"fg bright green", "\\x1b[92m", "\x1b[102m"},
    {"fg bright yellow", "\\x1b[93m", "\x1b[103m"},
    {"fg bright blue", "\\x1b[94m", "\x1b[104m"},
    {"fg bright magenta", "\\x1b[95m", "\x1b[105m"},
    {"fg bright cyan", "\\x1b[96m", "\x1b[106m"},
    {"fg bright white", "\\x1b[97m", "\x1b[107m"},
    {"bg black", "\\x1b[40m", "\x1b[40m"},
    {"bg red", "\\x1b[41m", "\x1b[41m"},
    {"bg green", "\\x1b[42m", "\x1b[42m"},
    {"bg yellow", "\\x1b[43m", "\x1b[43m"},
    {"bg blue", "\\x1b[44m", "\x1b[44m"},
    {"bg magenta", "\\x1b[45m", "\x1b[45m"},
    {"bg cyan", "\\x1b[46m", "\x1b[46m"},
    {"bg white", "\\x1b[47m", "\x1b[47m"},
    {"bg bright black", "\\x1b[100m", "\x1b[100m"},
    {"bg bright red", "\\x1b[101m", "\x1b[101m"},
    {"bg bright green", "\\x1b[102m", "\x1b[102m"},
    {"bg bright yellow", "\\x1b[103m", "\x1b[103m"},
    {"bg bright blue", "\\x1b[104m", "\x1b[104m"},
    {"bg bright magenta", "\\x1b[105m", "\x1b[105m"},
    {"bg bright cyan", "\\x1b[106m", "\x1b[106m"},
    {"bg bright white", "\\x1b[107m", "\x1b[107m"},
}};

int totalRows(int columns)
{
    return ((int)kColors.size() + columns - 1) / columns;
}

int pickerColumns(int width)
{
    constexpr int cellWidth = 25;
    return std::clamp(width / cellWidth, 1, 3);
}

int pickerWidth(int screenCols)
{
    return std::min(78, std::max(1, screenCols - 2));
}

int pickerHeight(int screenRows)
{
    return std::min(16, std::max(1, screenRows - 2));
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

void appendCell(std::string& out, const Theme& theme, const ColorEntry& entry,
                bool selected, int width)
{
    out += selected ? theme.selection() : theme.baseFg();
    out += selected ? "> " : "  ";
    out += theme.baseFg();
    out += "[";
    out.append(entry.sample.data(), entry.sample.size());
    out += "  ";
    out += theme.reset();
    out += selected ? theme.selection() : theme.baseFg();
    out += "]";
    out += " ";
    appendPadded(out, entry.name, std::max(0, width - 8));
    out += theme.reset();
}

ModeState exitState(ModeContext& ctx)
{
    return ctx.hasBuffer() ? ModeState{NormalMode{}} : ModeState{WelcomeMode{}};
}
} // namespace

void ColorPickerMode::on_enter(ModeContext& ctx)
{
    cursor = std::clamp(cursor, 0, (int)kColors.size() - 1);
    rowOffset = 0;
    ctx.requestFullRedraw();
    Terminal::setCursorBlock();
}

void ColorPickerMode::on_exit(ModeContext& ctx)
{
    ctx.requestFullRedraw();
}

void ColorPickerMode::clampToVisible(int columns, int visibleRows)
{
    cursor = std::clamp(cursor, 0, (int)kColors.size() - 1);
    const int cursorRow = cursor / columns;
    if(cursorRow < rowOffset)
        rowOffset = cursorRow;
    else if(cursorRow >= rowOffset + visibleRows)
        rowOffset = cursorRow - visibleRows + 1;

    const int maxOffset = std::max(0, totalRows(columns) - visibleRows);
    rowOffset = std::clamp(rowOffset, 0, maxOffset);
}

std::optional<ModeState> ColorPickerMode::handle(ModeContext& ctx,
                                                 const ModeKeyEvent& event)
{
    const int c = keyCode(event.key);
    const int width = pickerWidth(ctx.screenCols());
    const int columns = pickerColumns(width - 4);
    const int visibleRows = std::max(1, pickerHeight(ctx.screenRows()) - 4);

    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(typed::TypedKey::KEY_Q))
    {
        return exitState(ctx);
    }

    if(c == keyCode(typed::TypedKey::KEY_H) ||
       c == keyCode(navigation::NavigationKey::ARROW_LEFT))
    {
        if(cursor > 0)
            --cursor;
        clampToVisible(columns, visibleRows);
        ctx.requestFullRedraw();
        return std::nullopt;
    }

    if(c == keyCode(typed::TypedKey::KEY_L) ||
       c == keyCode(navigation::NavigationKey::ARROW_RIGHT))
    {
        if(cursor + 1 < (int)kColors.size())
            ++cursor;
        clampToVisible(columns, visibleRows);
        ctx.requestFullRedraw();
        return std::nullopt;
    }

    if(c == keyCode(typed::TypedKey::KEY_K) ||
       c == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        cursor = std::max(0, cursor - columns);
        clampToVisible(columns, visibleRows);
        ctx.requestFullRedraw();
        return std::nullopt;
    }

    if(c == keyCode(typed::TypedKey::KEY_J) ||
       c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        cursor = std::min((int)kColors.size() - 1, cursor + columns);
        clampToVisible(columns, visibleRows);
        ctx.requestFullRedraw();
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::ENTER) ||
       c == keyCode(control::ControlKey::CTRL_M))
    {
        if(ctx.editor && ctx.editor->hasBuffer())
        {
            for(char ch : kColors[cursor].code)
                ctx.editor->insertChar(ch);
            ctx.editor->saveState();
            ctx.setStatusMessage("inserted " +
                                 std::string(kColors[cursor].name));
            ctx.requestFullRedraw();
        }
        return exitState(ctx);
    }

    return std::nullopt;
}

void ColorPickerMode::draw(Editor& editor) const
{
    editor.drawFullScreenSingle();

    const int width = pickerWidth(editor.screenCols);
    const int height = pickerHeight(editor.screenRows);
    const int left = std::max(1, (editor.screenCols - width) / 2 + 1);
    const int top = std::max(1, (editor.screenRows - height) / 2 + 1);
    const int innerWidth = std::max(1, width - 2);
    const int columns = pickerColumns(innerWidth - 2);
    const int cellWidth = std::max(1, (innerWidth - 2) / columns);
    const int visibleRows = std::max(1, height - 4);
    const int rows = totalRows(columns);
    const int maxOffset = std::max(0, rows - visibleRows);
    const int firstRow = std::clamp(rowOffset, 0, maxOffset);

    std::string output;
    output.reserve((size_t)width * (size_t)height * 2);
    output += Terminal::ESC_HIDE_CURSOR;

    auto moveTo = [&](int row, int col)
    { output += Terminal::cursorPos(row, col); };

    moveTo(top, left);
    output += editor.theme.uiDim();
    output += "+";
    output.append(innerWidth, '-');
    output += "+";

    moveTo(top + 1, left);
    output += "|";
    output += editor.theme.uiAccent();
    output += Terminal::ESC_BOLD;
    appendPadded(output, " ANSI colors", innerWidth);
    output += editor.theme.uiDim();
    output += "|";

    for(int r = 0; r < visibleRows; ++r)
    {
        moveTo(top + 2 + r, left);
        output += editor.theme.uiDim();
        output += "|";
        output += editor.theme.baseFg();
        output += " ";

        const int colorRow = firstRow + r;
        for(int col = 0; col < columns; ++col)
        {
            const int index = colorRow * columns + col;
            if(index < (int)kColors.size())
            {
                appendCell(output, editor.theme, kColors[index],
                           index == cursor, cellWidth);
            }
            else
            {
                output.append(cellWidth, ' ');
            }
        }

        const int used = 1 + columns * cellWidth;
        if(used < innerWidth)
            output.append(innerWidth - used, ' ');
        output += editor.theme.uiDim();
        output += "|";
    }

    moveTo(top + height - 2, left);
    output += editor.theme.uiDim();
    output += "|";
    output += editor.theme.uiDim();
    char footer[96];
    std::snprintf(footer, sizeof(footer),
                  " h/j/k/l move  Enter insert  q/Esc cancel  %d/%zu",
                  cursor + 1, kColors.size());
    appendPadded(output, footer, innerWidth);
    output += editor.theme.uiDim();
    output += "|";

    moveTo(top + height - 1, left);
    output += "+";
    output.append(innerWidth, '-');
    output += "+";
    output += editor.theme.reset();

    Terminal::write(output);
    Terminal::flush();
}
} // namespace editor::statemachine
