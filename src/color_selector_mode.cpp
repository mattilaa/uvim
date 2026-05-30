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
constexpr std::array<std::string_view, 3> kNames = {"R", "G", "B"};

ModeState exitState(ModeContext& ctx)
{
    return ctx.hasBuffer() ? ModeState{NormalMode{}} : ModeState{WelcomeMode{}};
}

int& activeComponent(ColorSelectorMode& mode)
{
    if(mode.active == 0)
        return mode.red;
    if(mode.active == 1)
        return mode.green;
    return mode.blue;
}

int componentValue(const ColorSelectorMode& mode, int index)
{
    if(index == 0)
        return mode.red;
    if(index == 1)
        return mode.green;
    return mode.blue;
}

std::string escapeCode(const ColorSelectorMode& mode)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "\\x1b[%d;2;%d;%d;%dm",
                  mode.background ? 48 : 38, mode.red, mode.green, mode.blue);
    return buffer;
}

std::string rgbSample(int red, int green, int blue)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "\x1b[48;2;%d;%d;%dm", red, green,
                  blue);
    return buffer;
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
    const int sliderWidth = std::max(0, width - 10);
    const int filled = std::clamp(value * sliderWidth / 255, 0, sliderWidth);

    out += selected ? theme.selection() : theme.baseFg();
    out += selected ? "> " : "  ";
    out.append(kNames[index].data(), kNames[index].size());
    out += " ";

    char valueText[8];
    std::snprintf(valueText, sizeof(valueText), "%3d ", value);
    out += valueText;
    out += "[";
    out += theme.uiAccent();
    out.append(filled, '=');
    out += selected ? theme.selection() : theme.baseFg();
    out.append(sliderWidth - filled, ' ');
    out += "]";
    const int used = 10 + sliderWidth;
    if(used < width)
        out.append(width - used, ' ');
    out += theme.reset();
}
} // namespace

void ColorSelectorMode::on_enter(ModeContext& ctx)
{
    active = std::clamp(active, 0, 2);
    red = std::clamp(red, 0, 255);
    green = std::clamp(green, 0, 255);
    blue = std::clamp(blue, 0, 255);
    ctx.requestFullRedraw();
    Terminal::setCursorBlock();
}

void ColorSelectorMode::on_exit(ModeContext& ctx)
{
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
        active = std::min(2, active + 1);
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
            ctx.setStatusMessage(background ? "inserted RGB ANSI background"
                                            : "inserted RGB ANSI color");
            ctx.requestFullRedraw();
        }
        return exitState(ctx);
    }

    return std::nullopt;
}

void ColorSelectorMode::draw(Editor& editor) const
{
    editor.drawFullScreenSingle();

    const int width = std::min(74, std::max(1, editor.screenCols - 2));
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
    output += "+";
    output.append(innerWidth, '-');
    output += "+";

    moveTo(top + 1, left);
    output += "|";
    output += editor.theme.uiAccent();
    output += Terminal::ESC_BOLD;
    appendPadded(output,
                 background ? " RGB ANSI background selector"
                            : " RGB ANSI selector",
                 innerWidth);
    output += editor.theme.uiDim();
    output += "|";

    for(int i = 0; i < 3; ++i)
    {
        moveTo(top + 2 + i, left);
        output += editor.theme.uiDim();
        output += "| ";
        appendSlider(output, editor.theme, *this, i, sliderWidth);
        output += editor.theme.uiDim();
        output += "|";
    }

    moveTo(top + 5, left);
    output += editor.theme.uiDim();
    output += "| ";
    output += editor.theme.baseFg();
    output += "[";
    output += rgbSample(red, green, blue);
    output += "      ";
    output += editor.theme.reset();
    output += editor.theme.baseFg();
    output += "] ";
    appendPadded(output, code, std::max(1, innerWidth - 10));
    output += editor.theme.uiDim();
    output += "|";

    moveTo(top + height - 2, left);
    output += editor.theme.uiDim();
    output += "|";
    output += editor.theme.uiDim();
    appendPadded(output,
                 " j/k select  h/l +/-1  H/L +/-10  Enter insert  q/Esc "
                 "cancel",
                 innerWidth);
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
