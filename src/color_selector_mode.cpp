#include "color_selector_mode.h"

#include "ascii.h"
#include "color_constant.h"
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
constexpr std::array<std::string_view, 6> kNames = {"FG R", "FG G", "FG B",
                                                    "BG R", "BG G", "BG B"};
constexpr std::array<std::u8string_view, 8> kPartialBlocks = {
    u8"", u8"▏", u8"▎", u8"▍", u8"▌", u8"▋", u8"▊", u8"▉"};
constexpr std::u8string_view kFullBlock = u8"█";
constexpr std::u8string_view kEmptyBlock = u8"░";

ModeState exitState(ModeContext& ctx)
{
    return ctx.hasBuffer() ? ModeState{NormalMode{}} : ModeState{WelcomeMode{}};
}

int& activeComponent(ColorSelectorMode& mode)
{
    if(mode.active == 0)
        return mode.fgRed;
    if(mode.active == 1)
        return mode.fgGreen;
    if(mode.active == 2)
        return mode.fgBlue;
    if(mode.active == 3)
        return mode.bgRed;
    if(mode.active == 4)
        return mode.bgGreen;
    return mode.bgBlue;
}

int componentValue(const ColorSelectorMode& mode, int index)
{
    if(index == 0)
        return mode.fgRed;
    if(index == 1)
        return mode.fgGreen;
    if(index == 2)
        return mode.fgBlue;
    if(index == 3)
        return mode.bgRed;
    if(index == 4)
        return mode.bgGreen;
    return mode.bgBlue;
}

std::string escapeCode(const ColorSelectorMode& mode)
{
    std::string code;
    if(mode.bold)
        code += color::literal(color::AnsiColor::Bold);
    if(mode.italic)
        code += color::literal(color::AnsiColor::Italic);

    code += color::rgbLiteralFg(mode.fgRed, mode.fgGreen, mode.fgBlue);
    code += color::rgbLiteralBg(mode.bgRed, mode.bgGreen, mode.bgBlue);
    return code;
}

std::string rgbSample(int red, int green, int blue)
{
    return color::rgbBg(red, green, blue);
}

std::string fgSample(int red, int green, int blue)
{
    return color::rgbFg(red, green, blue);
}

std::string channelColor(int index)
{
    switch(index % 3)
    {
    case 0:
        return fgSample(255, 88, 88);
    case 1:
        return fgSample(88, 220, 120);
    default:
        return fgSample(112, 168, 255);
    }
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
    if(used < width)
        out.append(width - used, ' ');
}

void appendSlider(std::string& out, const Theme& theme,
                  const ColorSelectorMode& mode, int index, int width)
{
    const bool selected = index == mode.active;
    const int value = componentValue(mode, index);
    const int labelWidth = text_utils::utf8DisplayWidth(kNames[index]);
    const int fixedWidth = 2 + labelWidth + 1 + 4 + 1 + 1;
    const int sliderWidth = std::max(0, width - fixedWidth);
    const int maxUnits = sliderWidth * 8;
    int filledUnits = maxUnits > 0 ? (value * maxUnits + 127) / 255 : 0;
    if(value > 0 && filledUnits == 0)
        filledUnits = 1;
    filledUnits = std::clamp(filledUnits, 0, maxUnits);
    const int filled = filledUnits / 8;
    const int partial = filledUnits % 8;
    const bool hasPartial = partial > 0 && filled < sliderWidth;
    const int empty = std::max(0, sliderWidth - filled - (hasPartial ? 1 : 0));
    const std::string selectedOrBase =
        selected ? theme.selection() : theme.baseFg();
    const std::string channel = channelColor(index);
    const bool backgroundRow = index >= 3;
    const std::string backgroundPreview =
        backgroundRow ? rgbSample(mode.bgRed, mode.bgGreen, mode.bgBlue) : "";

    out += selectedOrBase;
    out += selected ? "> " : "  ";
    out += channel;
    out.append(kNames[index].data(), kNames[index].size());
    out += selectedOrBase;
    out += " ";

    char valueText[8];
    std::snprintf(valueText, sizeof(valueText), "%3d ", value);
    out += valueText;
    out += "[";
    out += backgroundPreview;
    out += channel;
    text_utils::appendUtf8Repeat(out, kFullBlock, filled);
    if(hasPartial)
        text_utils::appendU8(out, kPartialBlocks[partial]);
    out += backgroundPreview;
    out += theme.uiDim();
    text_utils::appendUtf8Repeat(out, kEmptyBlock, empty);
    out += selectedOrBase;
    out += "]";
    const int used = fixedWidth + sliderWidth;
    if(used < width)
        out.append(width - used, ' ');
    out += theme.reset();
}

void appendPreview(std::string& out, const Theme& theme,
                   const ColorSelectorMode& mode, std::string_view code,
                   int innerWidth)
{
    constexpr std::string_view prefix = " preview: ";
    constexpr std::string_view sampleText = "Example Text";
    constexpr int previewWidth = 27;

    out += theme.baseFg();
    out.append(prefix.data(), prefix.size());
    out += "[ ";
    if(mode.bold)
        out += Terminal::ESC_BOLD;
    if(mode.italic)
        out += Terminal::ESC_ITALIC;
    out += rgbSample(mode.bgRed, mode.bgGreen, mode.bgBlue);
    out += fgSample(mode.fgRed, mode.fgGreen, mode.fgBlue);
    out.append(sampleText.data(), sampleText.size());
    out += theme.reset();
    out += theme.baseFg();
    out += " ] ";

    appendPadded(out, code, std::max(0, innerWidth - previewWidth));
}
} // namespace

void ColorSelectorMode::on_enter(ModeContext& ctx)
{
    active = std::clamp(active, 0, 5);
    fgRed = std::clamp(fgRed, 0, 255);
    fgGreen = std::clamp(fgGreen, 0, 255);
    fgBlue = std::clamp(fgBlue, 0, 255);
    bgRed = std::clamp(bgRed, 0, 255);
    bgGreen = std::clamp(bgGreen, 0, 255);
    bgBlue = std::clamp(bgBlue, 0, 255);
    backdropDrawn = false;
    backdropRows = 0;
    backdropCols = 0;
    ctx.requestFullRedraw();
    Terminal::setCursorBlock();
}

void ColorSelectorMode::on_exit(ModeContext& ctx)
{
    backdropDrawn = false;
    ctx.requestFullRedraw();
}

std::optional<ModeState> ColorSelectorMode::handle(ModeContext& ctx,
                                                   const ModeKeyEvent& event)
{
    const int c = keyCode(event.key);

    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(typed::TypedKey::KEY_Q))
    {
        return exitState(ctx);
    }

    if(c == keyCode(typed::TypedKey::KEY_K) ||
       c == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        active = std::max(0, active - 1);
        ctx.requestFullRedraw();
        return std::nullopt;
    }

    if(c == keyCode(typed::TypedKey::KEY_J) ||
       c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        active = std::min(5, active + 1);
        ctx.requestFullRedraw();
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::CTRL_B))
    {
        bold = !bold;
        ctx.requestFullRedraw();
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::CTRL_I))
    {
        italic = !italic;
        ctx.requestFullRedraw();
        return std::nullopt;
    }

    if(c == keyCode(typed::TypedKey::KEY_H) ||
       c == keyCode(navigation::NavigationKey::ARROW_LEFT))
    {
        int& value = activeComponent(*this);
        value = std::max(0, value - 1);
        ctx.requestFullRedraw();
        return std::nullopt;
    }

    if(c == keyCode(typed::TypedKey::KEY_L) ||
       c == keyCode(navigation::NavigationKey::ARROW_RIGHT))
    {
        int& value = activeComponent(*this);
        value = std::min(255, value + 1);
        ctx.requestFullRedraw();
        return std::nullopt;
    }

    if(c == keyCode(typed::TypedKey::KEY_CAP_H))
    {
        int& value = activeComponent(*this);
        value = std::max(0, value - 10);
        ctx.requestFullRedraw();
        return std::nullopt;
    }

    if(c == keyCode(typed::TypedKey::KEY_CAP_L))
    {
        int& value = activeComponent(*this);
        value = std::min(255, value + 10);
        ctx.requestFullRedraw();
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::ENTER) ||
       c == keyCode(control::ControlKey::CTRL_M))
    {
        if(ctx.editor && ctx.editor->hasBuffer())
        {
            const std::string code = escapeCode(*this);
            for(char ch : code)
                ctx.editor->insertChar(ch);
            ctx.editor->saveState();
            ctx.setStatusMessage("inserted RGB ANSI style");
            ctx.requestFullRedraw();
        }
        return exitState(ctx);
    }

    return std::nullopt;
}

void ColorSelectorMode::draw(Editor& editor) const
{
    if(!backdropDrawn || backdropRows != editor.screenRows ||
       backdropCols != editor.screenCols)
    {
        editor.drawFullScreenSingle();
        backdropDrawn = true;
        backdropRows = editor.screenRows;
        backdropCols = editor.screenCols;
    }

    const int width = std::min(104, std::max(1, editor.screenCols - 2));
    const int height = std::min(12, std::max(1, editor.screenRows - 2));
    const int left = std::max(1, (editor.screenCols - width) / 2 + 1);
    const int top = std::max(1, (editor.screenRows - height) / 2 + 1);
    const int innerWidth = std::max(1, width - 2);
    const int sliderWidth = std::max(1, innerWidth - 1);
    const std::string code = escapeCode(*this);

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
    appendPadded(output, " RGB ANSI style selector", innerWidth);
    output += editor.theme.uiDim();
    text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);

    for(int i = 0; i < 6; ++i)
    {
        moveTo(top + 2 + i, left);
        output += editor.theme.uiDim();
        text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
        output += " ";
        appendSlider(output, editor.theme, *this, i, sliderWidth);
        output += editor.theme.uiDim();
        text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
    }

    moveTo(top + 8, left);
    output += editor.theme.uiDim();
    text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
    appendPreview(output, editor.theme, *this, code, innerWidth);
    output += editor.theme.uiDim();
    text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);

    moveTo(top + 9, left);
    output += editor.theme.uiDim();
    text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
    output += editor.theme.baseFg();
    std::string styleLine = " styles: ";
    styleLine += bold ? "[bold]" : "[    ]";
    styleLine += " ";
    styleLine += italic ? "[italic]" : "[      ]";
    appendPadded(output, styleLine, innerWidth);
    output += editor.theme.uiDim();
    text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);

    moveTo(top + height - 2, left);
    output += editor.theme.uiDim();
    text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
    output += editor.theme.uiDim();
    appendPadded(output,
                 " j/k select  h/l +/-1  H/L +/-10  ^B bold  ^I italic  "
                 "Enter insert  q/Esc cancel",
                 innerWidth);
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
