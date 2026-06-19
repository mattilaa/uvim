#include "color_picker_mode.h"

#include "ascii.h"
#include "color_constant.h"
#include "color_selector_mode.h"
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
    color::AnsiColor code;
    color::AnsiColor sample;
};

constexpr std::array<ColorEntry, 33> kColors = {{
    {"reset", color::AnsiColor::Reset, color::AnsiColor::Reset},
    {"fg black", color::AnsiColor::FgBlack, color::AnsiColor::BgBlack},
    {"fg red", color::AnsiColor::FgRed, color::AnsiColor::BgRed},
    {"fg green", color::AnsiColor::FgGreen, color::AnsiColor::BgGreen},
    {"fg yellow", color::AnsiColor::FgYellow, color::AnsiColor::BgYellow},
    {"fg blue", color::AnsiColor::FgBlue, color::AnsiColor::BgBlue},
    {"fg magenta", color::AnsiColor::FgMagenta, color::AnsiColor::BgMagenta},
    {"fg cyan", color::AnsiColor::FgCyan, color::AnsiColor::BgCyan},
    {"fg white", color::AnsiColor::FgWhite, color::AnsiColor::BgWhite},
    {"fg bright black", color::AnsiColor::FgBrightBlack,
     color::AnsiColor::BgBrightBlack},
    {"fg bright red", color::AnsiColor::FgBrightRed,
     color::AnsiColor::BgBrightRed},
    {"fg bright green", color::AnsiColor::FgBrightGreen,
     color::AnsiColor::BgBrightGreen},
    {"fg bright yellow", color::AnsiColor::FgBrightYellow,
     color::AnsiColor::BgBrightYellow},
    {"fg bright blue", color::AnsiColor::FgBrightBlue,
     color::AnsiColor::BgBrightBlue},
    {"fg bright magenta", color::AnsiColor::FgBrightMagenta,
     color::AnsiColor::BgBrightMagenta},
    {"fg bright cyan", color::AnsiColor::FgBrightCyan,
     color::AnsiColor::BgBrightCyan},
    {"fg bright white", color::AnsiColor::FgBrightWhite,
     color::AnsiColor::BgBrightWhite},
    {"bg black", color::AnsiColor::BgBlack, color::AnsiColor::BgBlack},
    {"bg red", color::AnsiColor::BgRed, color::AnsiColor::BgRed},
    {"bg green", color::AnsiColor::BgGreen, color::AnsiColor::BgGreen},
    {"bg yellow", color::AnsiColor::BgYellow, color::AnsiColor::BgYellow},
    {"bg blue", color::AnsiColor::BgBlue, color::AnsiColor::BgBlue},
    {"bg magenta", color::AnsiColor::BgMagenta, color::AnsiColor::BgMagenta},
    {"bg cyan", color::AnsiColor::BgCyan, color::AnsiColor::BgCyan},
    {"bg white", color::AnsiColor::BgWhite, color::AnsiColor::BgWhite},
    {"bg bright black", color::AnsiColor::BgBrightBlack,
     color::AnsiColor::BgBrightBlack},
    {"bg bright red", color::AnsiColor::BgBrightRed,
     color::AnsiColor::BgBrightRed},
    {"bg bright green", color::AnsiColor::BgBrightGreen,
     color::AnsiColor::BgBrightGreen},
    {"bg bright yellow", color::AnsiColor::BgBrightYellow,
     color::AnsiColor::BgBrightYellow},
    {"bg bright blue", color::AnsiColor::BgBrightBlue,
     color::AnsiColor::BgBrightBlue},
    {"bg bright magenta", color::AnsiColor::BgBrightMagenta,
     color::AnsiColor::BgBrightMagenta},
    {"bg bright cyan", color::AnsiColor::BgBrightCyan,
     color::AnsiColor::BgBrightCyan},
    {"bg bright white", color::AnsiColor::BgBrightWhite,
     color::AnsiColor::BgBrightWhite},
}};

int colorCount(bool background)
{
    return background ? 17 : (int)kColors.size();
}

const ColorEntry& colorAt(int index, bool background)
{
    if(!background || index == 0)
        return kColors[index];
    return kColors[16 + index];
}

int totalRows(int columns, bool background)
{
    return (colorCount(background) + columns - 1) / columns;
}

int pickerColumns(int width)
{
    constexpr int cellWidth = 25;
    return std::clamp(width / cellWidth, 1, 3);
}

int pickerWidth(int screenCols)
{
    return std::min(96, std::max(1, screenCols - 2));
}

int pickerHeight(int screenRows)
{
    return std::min(18, std::max(1, screenRows - 2));
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
    out += color::ansi(entry.sample);
    out += "  ";
    out += theme.reset();
    out += selected ? theme.selection() : theme.baseFg();
    out += "]";
    out += " ";
    appendPadded(out, entry.name, std::max(0, width - 7));
    out += theme.reset();
}

std::string actualEscape(color::AnsiColor code)
{
    return color::ansi(code);
}

bool hasTextPrefix(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() &&
           text.substr(0, prefix.size()) == prefix;
}

bool hasWord(std::string_view text, std::string_view word)
{
    return text.find(word) != std::string_view::npos;
}

std::string readablePreviewFg(const ColorEntry& entry)
{
    if(hasTextPrefix(entry.name, "fg "))
        return color::rgbBg(32, 32, 32);
    if(hasWord(entry.name, "white") || hasWord(entry.name, "yellow") ||
       hasWord(entry.name, "cyan"))
        return color::rgbFg(16, 16, 16);
    return color::rgbFg(235, 235, 235);
}

void appendPreview(std::string& out, const Theme& theme,
                   const ColorEntry& entry, int innerWidth)
{
    constexpr std::string_view prefix = " preview: ";
    constexpr std::string_view sampleText = "Example Text";
    constexpr int previewWidth = 27;

    out += theme.baseFg();
    out.append(prefix.data(), prefix.size());
    out += "[ ";
    out += readablePreviewFg(entry);
    out += actualEscape(entry.code);
    out.append(sampleText.data(), sampleText.size());
    out += theme.reset();
    out += theme.baseFg();
    out += " ] ";

    const int codeWidth = std::max(0, innerWidth - previewWidth);
    appendPadded(out, color::literal(entry.code), codeWidth);
}

ModeState exitState(ModeContext& ctx)
{
    return ctx.hasBuffer() ? ModeState{NormalMode{}} : ModeState{WelcomeMode{}};
}

bool applyVisualColorRange(Editor& editor, VisualColorRange range,
                           std::string_view code)
{
    if(!editor.lines || editor.lines->empty())
        return false;

    range.startY = std::clamp(range.startY, 0, (int)editor.lines->size() - 1);
    range.endY = std::clamp(range.endY, 0, (int)editor.lines->size() - 1);
    if(range.startY > range.endY)
        std::swap(range.startY, range.endY);

    range.startX =
        std::clamp(range.startX, 0, (int)(*editor.lines)[range.startY].size());
    range.endX =
        std::clamp(range.endX, 0, (int)(*editor.lines)[range.endY].size());

    const std::string reset = color::literal(color::AnsiColor::Reset);
    (*editor.lines)[range.endY].insert((std::size_t)range.endX, reset);
    (*editor.lines)[range.startY].insert((std::size_t)range.startX,
                                         std::string(code));

    if(editor.cursorY)
        *editor.cursorY = range.startY;
    if(editor.cursorX)
        *editor.cursorX = range.startX;
    if(editor.dirty)
        *editor.dirty = true;
    editor.needsFullRedraw = true;
    return true;
}
} // namespace

ColorPickerMode ColorPickerMode::forVisualRange(VisualColorRange range,
                                                bool useBackground)
{
    ColorPickerMode mode{useBackground};
    mode.visualTarget = range;
    return mode;
}

void ColorPickerMode::on_enter(ModeContext& ctx)
{
    cursor = std::clamp(cursor, 0, colorCount(background) - 1);
    rowOffset = 0;
    backdropDrawn = false;
    backdropRows = 0;
    backdropCols = 0;
    ctx.requestFullRedraw();
    Terminal::setCursorBlock();
}

void ColorPickerMode::on_exit(ModeContext& ctx)
{
    backdropDrawn = false;
    ctx.requestFullRedraw();
}

void ColorPickerMode::clampToVisible(int columns, int visibleRows)
{
    cursor = std::clamp(cursor, 0, colorCount(background) - 1);
    const int cursorRow = cursor / columns;
    if(cursorRow < rowOffset)
        rowOffset = cursorRow;
    else if(cursorRow >= rowOffset + visibleRows)
        rowOffset = cursorRow - visibleRows + 1;

    const int maxOffset =
        std::max(0, totalRows(columns, background) - visibleRows);
    rowOffset = std::clamp(rowOffset, 0, maxOffset);
}

std::optional<ModeState> ColorPickerMode::handle(ModeContext& ctx,
                                                 const ModeKeyEvent& event)
{
    const int c = keyCode(event.key);
    const int width = pickerWidth(ctx.screenCols());
    const int columns = pickerColumns(width - 4);
    const int visibleRows = std::max(1, pickerHeight(ctx.screenRows()) - 5);

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
        if(cursor + 1 < colorCount(background))
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
        cursor = std::min(colorCount(background) - 1, cursor + columns);
        clampToVisible(columns, visibleRows);
        ctx.requestFullRedraw();
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::ENTER) ||
       c == keyCode(control::ControlKey::CTRL_M))
    {
        if(ctx.editor && ctx.editor->hasBuffer())
        {
            const ColorEntry& entry = colorAt(cursor, background);
            if(visualTarget)
            {
                if(applyVisualColorRange(*ctx.editor, *visualTarget,
                                         color::literal(entry.code)))
                {
                    ctx.editor->saveState();
                    ctx.setStatusMessage("colored selected text");
                }
                visualTarget.reset();
            }
            else
            {
                for(char ch : std::string_view(color::literal(entry.code)))
                    ctx.editor->insertChar(ch);
                ctx.editor->saveState();
                ctx.setStatusMessage("inserted " + std::string(entry.name));
            }
            ctx.requestFullRedraw();
        }
        return exitState(ctx);
    }

    if(c == keyCode(typed::TypedKey::KEY_S))
    {
        const ColorEntry& entry = colorAt(cursor, background);
        ColorSelectorMode mode =
            ColorSelectorMode::fromAnsiColor(entry.code, background);
        mode.visualTarget = visualTarget;
        return mode;
    }

    return std::nullopt;
}

void ColorPickerMode::draw(Editor& editor) const
{
    if(!backdropDrawn || backdropRows != editor.screenRows ||
       backdropCols != editor.screenCols)
    {
        editor.drawFullScreenSingle();
        backdropDrawn = true;
        backdropRows = editor.screenRows;
        backdropCols = editor.screenCols;
    }

    const int width = pickerWidth(editor.screenCols);
    const int height = pickerHeight(editor.screenRows);
    const int left = std::max(1, (editor.screenCols - width) / 2 + 1);
    const int top = std::max(1, (editor.screenRows - height) / 2 + 1);
    const int innerWidth = std::max(1, width - 2);
    const int columns = pickerColumns(innerWidth - 2);
    const int cellWidth = std::max(1, (innerWidth - 2) / columns);
    const int visibleRows = std::max(1, height - 5);
    const int rows = totalRows(columns, background);
    const int maxOffset = std::max(0, rows - visibleRows);
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
    appendPadded(output,
                 background ? " ANSI background colors" : " ANSI colors",
                 innerWidth);
    output += editor.theme.uiDim();
    text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);

    for(int r = 0; r < visibleRows; ++r)
    {
        moveTo(top + 2 + r, left);
        output += editor.theme.uiDim();
        text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
        output += editor.theme.baseFg();
        output += " ";

        const int colorRow = firstRow + r;
        for(int col = 0; col < columns; ++col)
        {
            const int index = colorRow * columns + col;
            if(index < colorCount(background))
            {
                appendCell(output, editor.theme, colorAt(index, background),
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
        text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
    }

    moveTo(top + height - 3, left);
    output += editor.theme.uiDim();
    text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
    appendPreview(output, editor.theme, colorAt(cursor, background),
                  innerWidth);
    output += editor.theme.uiDim();
    text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);

    moveTo(top + height - 2, left);
    output += editor.theme.uiDim();
    text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
    output += editor.theme.uiDim();
    char footer[96];
    std::snprintf(footer, sizeof(footer),
                  " h/j/k/l move  s RGB adjust  Enter insert  q/Esc cancel  "
                  "%d/%zu",
                  cursor + 1, (size_t)colorCount(background));
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
