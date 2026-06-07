#include "glyph_select_mode.h"

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
struct GlyphEntry
{
    std::string_view glyph;
    std::string_view name;
};

constexpr std::array<GlyphEntry, 49> kGlyphs = {{
    {"\xE2\x86\x90", "left arrow"},
    {"\xE2\x86\x92", "right arrow"},
    {"\xE2\x86\x91", "up arrow"},
    {"\xE2\x86\x93", "down arrow"},
    {"\xE2\x86\x94", "left right arrow"},
    {"\xE2\x86\x95", "up down arrow"},
    {"\xE2\x80\xA2", "bullet"},
    {"\xE2\x97\xA6", "white bullet"},
    {"\xE2\x96\xAA", "black small square"},
    {"\xE2\x96\xAB", "white small square"},
    {"\xE2\x97\x86", "black diamond"},
    {"\xE2\x97\x87", "white diamond"},
    {"\xE2\x9C\x93", "check mark"},
    {"\xE2\x9C\x97", "cross mark"},
    {"\xE2\x98\x85", "black star"},
    {"\xE2\x98\x86", "white star"},
    {"\xE2\x9A\xA0", "warning"},
    {"\xE2\x9A\x99", "gear"},
    {"\xE2\x94\x80", "box horizontal"},
    {"\xE2\x94\x82", "box vertical"},
    {"\xE2\x94\x8C", "box top left"},
    {"\xE2\x94\x90", "box top right"},
    {"\xE2\x94\x94", "box bottom left"},
    {"\xE2\x94\x98", "box bottom right"},
    {"\xE2\x94\x9C", "box tee right"},
    {"\xE2\x94\xA4", "box tee left"},
    {"\xE2\x94\xAC", "box tee down"},
    {"\xE2\x94\xB4", "box tee up"},
    {"\xE2\x94\xBC", "box cross"},
    {"\xCE\xB1", "alpha"},
    {"\xCE\xB2", "beta"},
    {"\xCE\xB3", "gamma"},
    {"\xCE\xB4", "delta"},
    {"\xCE\xBB", "lambda"},
    {"\xCE\xBC", "mu"},
    {"\xCF\x80", "pi"},
    {"\xCE\xA3", "sigma"},
    {"\xCE\xA9", "omega"},
    {"\xE2\x89\xA4", "less or equal"},
    {"\xE2\x89\xA5", "greater or equal"},
    {"\xE2\x89\xA0", "not equal"},
    {"\xE2\x89\x88", "approximately"},
    {"\xE2\x88\x9E", "infinity"},
    {"\xC2\xB1", "plus minus"},
    {"\xC3\x97", "multiply"},
    {"\xC3\xB7", "divide"},
    {"\xC2\xB0", "degree"},
    {"\xE2\x80\xA6", "ellipsis"},
    {"\xC2\xB7", "middle dot"},
}};

int glyphCount()
{
    return (int)kGlyphs.size();
}

int popupWidth(int screenCols)
{
    return std::min(104, std::max(1, screenCols - 2));
}

int popupHeight(int screenRows)
{
    return std::min(22, std::max(1, screenRows - 2));
}

int popupColumns(int innerWidth)
{
    constexpr int cellWidth = 24;
    return std::clamp(innerWidth / cellWidth, 1, 4);
}

int totalRows(int columns)
{
    return (glyphCount() + columns - 1) / columns;
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

void appendCell(std::string& out, const Theme& theme, const GlyphEntry& entry,
                bool selected, int width)
{
    out += selected ? theme.selection() : theme.baseFg();
    out += selected ? "> " : "  ";
    appendPadded(out, entry.glyph, 2);
    out += " ";
    appendPadded(out, entry.name, std::max(0, width - 5));
    out += theme.reset();
}
} // namespace

void GlyphSelectMode::on_enter(ModeContext& ctx)
{
    cursor = std::clamp(cursor, 0, glyphCount() - 1);
    rowOffset = 0;
    backdropDrawn = false;
    backdropRows = 0;
    backdropCols = 0;
    ctx.requestFullRedraw();
    Terminal::setCursorBlock();
}

void GlyphSelectMode::on_exit(ModeContext& ctx)
{
    backdropDrawn = false;
    ctx.requestFullRedraw();
}

void GlyphSelectMode::clampToVisible(int columns, int visibleRows)
{
    cursor = std::clamp(cursor, 0, glyphCount() - 1);
    const int cursorRow = cursor / columns;
    if(cursorRow < rowOffset)
        rowOffset = cursorRow;
    else if(cursorRow >= rowOffset + visibleRows)
        rowOffset = cursorRow - visibleRows + 1;

    const int maxOffset = std::max(0, totalRows(columns) - visibleRows);
    rowOffset = std::clamp(rowOffset, 0, maxOffset);
}

std::optional<ModeState> GlyphSelectMode::handle(ModeContext& ctx,
                                                 const ModeKeyEvent& event)
{
    const int c = keyCode(event.key);
    const int width = popupWidth(ctx.screenCols());
    const int columns = popupColumns(std::max(1, width - 4));
    const int visibleRows = std::max(1, popupHeight(ctx.screenRows()) - 4);

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
        if(cursor + 1 < glyphCount())
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
        cursor = std::min(glyphCount() - 1, cursor + columns);
        clampToVisible(columns, visibleRows);
        ctx.requestFullRedraw();
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::ENTER) ||
       c == keyCode(control::ControlKey::CTRL_M))
    {
        if(ctx.editor && ctx.editor->hasBuffer())
        {
            const GlyphEntry& entry = kGlyphs[(std::size_t)cursor];
            for(char ch : entry.glyph)
                ctx.editor->insertChar(ch);
            ctx.editor->saveState();
            ctx.setStatusMessage("inserted glyph " + std::string(entry.glyph));
            ctx.requestFullRedraw();
        }
        return exitState(ctx);
    }

    return std::nullopt;
}

void GlyphSelectMode::draw(Editor& editor) const
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
    const int columns = popupColumns(innerWidth - 2);
    const int cellWidth = std::max(1, (innerWidth - 2) / columns);
    const int visibleRows = std::max(1, height - 4);
    const int maxOffset = std::max(0, totalRows(columns) - visibleRows);
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
    appendPadded(output, " Glyph selector", innerWidth);
    output += editor.theme.uiDim();
    text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);

    for(int r = 0; r < visibleRows; ++r)
    {
        moveTo(top + 2 + r, left);
        output += editor.theme.uiDim();
        text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
        output += editor.theme.baseFg();
        output += " ";

        const int glyphRow = firstRow + r;
        for(int col = 0; col < columns; ++col)
        {
            const int index = glyphRow * columns + col;
            if(index < glyphCount())
            {
                appendCell(output, editor.theme, kGlyphs[(std::size_t)index],
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

    moveTo(top + height - 2, left);
    output += editor.theme.uiDim();
    text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
    char footer[88];
    std::snprintf(footer, sizeof(footer),
                  " h/j/k/l move  Enter insert  q/Esc cancel  %d/%zu",
                  cursor + 1, kGlyphs.size());
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
