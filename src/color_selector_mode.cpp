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
#include <charconv>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

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
constexpr std::string_view kAnsiLiteralPrefix = "\\x1b[";
constexpr std::string_view kAnsiEscapePrefix = "\x1b[";

struct Rgb
{
    int red;
    int green;
    int blue;
};

struct AnsiLiteralSpan
{
    int start = 0;
    int length = 0;
};

struct ParsedSequence
{
    int start = 0;
    int end = 0;
    std::vector<int> params;
};

ModeState exitState(ModeContext& ctx)
{
    return ctx.hasBuffer() ? ModeState{NormalMode{}} : ModeState{WelcomeMode{}};
}

Rgb ansiRgb(int code, bool background)
{
    const int normalized = background ? code - 10 : code;
    switch(normalized)
    {
    case 30:
        return {0, 0, 0};
    case 31:
        return {170, 0, 0};
    case 32:
        return {0, 170, 0};
    case 33:
        return {170, 85, 0};
    case 34:
        return {0, 0, 170};
    case 35:
        return {170, 0, 170};
    case 36:
        return {0, 170, 170};
    case 37:
        return {170, 170, 170};
    case 90:
        return {85, 85, 85};
    case 91:
        return {255, 85, 85};
    case 92:
        return {85, 255, 85};
    case 93:
        return {255, 255, 85};
    case 94:
        return {85, 85, 255};
    case 95:
        return {255, 85, 255};
    case 96:
        return {85, 255, 255};
    case 97:
        return {255, 255, 255};
    default:
        return background ? Rgb{0, 0, 0} : Rgb{255, 255, 255};
    }
}

void setFg(ColorSelectorMode& mode, Rgb rgb)
{
    mode.fgRed = rgb.red;
    mode.fgGreen = rgb.green;
    mode.fgBlue = rgb.blue;
}

void setBg(ColorSelectorMode& mode, Rgb rgb)
{
    mode.bgRed = rgb.red;
    mode.bgGreen = rgb.green;
    mode.bgBlue = rgb.blue;
}

std::optional<int> parseInt(std::string_view text)
{
    int value = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if(result.ec != std::errc{} || result.ptr != end)
        return std::nullopt;
    return value;
}

std::optional<std::vector<int>> parseParams(std::string_view body)
{
    std::vector<int> params;
    std::size_t start = 0;
    while(start <= body.size())
    {
        const std::size_t sep = body.find(';', start);
        const std::size_t end =
            sep == std::string_view::npos ? body.size() : sep;
        const std::string_view part = body.substr(start, end - start);
        if(part.empty())
            return std::nullopt;
        std::optional<int> value = parseInt(part);
        if(!value)
            return std::nullopt;
        params.push_back(*value);
        if(sep == std::string_view::npos)
            break;
        start = sep + 1;
    }
    return params;
}

std::optional<ParsedSequence> parseSequenceAt(std::string_view line, int start)
{
    if(start < 0)
        return std::nullopt;
    std::size_t prefixSize = 0;
    if((std::size_t)start + kAnsiLiteralPrefix.size() <= line.size() &&
       line.substr((std::size_t)start, kAnsiLiteralPrefix.size()) ==
           kAnsiLiteralPrefix)
    {
        prefixSize = kAnsiLiteralPrefix.size();
    }
    else if((std::size_t)start + kAnsiEscapePrefix.size() <= line.size() &&
            line.substr((std::size_t)start, kAnsiEscapePrefix.size()) ==
                kAnsiEscapePrefix)
    {
        prefixSize = kAnsiEscapePrefix.size();
    }
    else
    {
        return std::nullopt;
    }

    const std::size_t bodyStart = (std::size_t)start + prefixSize;
    const std::size_t marker = line.find('m', bodyStart);
    if(marker == std::string_view::npos)
        return std::nullopt;

    std::optional<std::vector<int>> params =
        parseParams(line.substr(bodyStart, marker - bodyStart));
    if(!params)
        return std::nullopt;

    return ParsedSequence{start, (int)marker + 1, std::move(*params)};
}

std::vector<ParsedSequence> parseSequences(std::string_view line)
{
    std::vector<ParsedSequence> sequences;
    std::size_t pos = 0;
    while(pos < line.size())
    {
        const std::size_t literal = line.find(kAnsiLiteralPrefix, pos);
        const std::size_t escape = line.find(kAnsiEscapePrefix, pos);
        const std::size_t found =
            std::min(literal == std::string_view::npos ? line.size() : literal,
                     escape == std::string_view::npos ? line.size() : escape);
        if(found == line.size())
            break;
        std::optional<ParsedSequence> sequence =
            parseSequenceAt(line, (int)found);
        if(sequence)
        {
            pos = (std::size_t)sequence->end;
            sequences.push_back(std::move(*sequence));
        }
        else
        {
            pos = found + 1;
        }
    }
    return sequences;
}

void applyParams(ColorSelectorMode& mode, const std::vector<int>& params)
{
    for(std::size_t i = 0; i < params.size(); ++i)
    {
        const int param = params[i];
        if(param == 0)
        {
            mode = ColorSelectorMode{};
            continue;
        }
        if(param == 1)
        {
            mode.bold = true;
            continue;
        }
        if(param == 3)
        {
            mode.italic = true;
            continue;
        }
        if((param >= 30 && param <= 37) || (param >= 90 && param <= 97))
        {
            setFg(mode, ansiRgb(param, false));
            mode.active = 0;
            continue;
        }
        if((param >= 40 && param <= 47) || (param >= 100 && param <= 107))
        {
            setBg(mode, ansiRgb(param, true));
            mode.active = 3;
            continue;
        }
        if((param == 38 || param == 48) && i + 4 < params.size() &&
           params[i + 1] == 2)
        {
            Rgb rgb{std::clamp(params[i + 2], 0, 255),
                    std::clamp(params[i + 3], 0, 255),
                    std::clamp(params[i + 4], 0, 255)};
            if(param == 38)
            {
                setFg(mode, rgb);
                mode.active = 0;
            }
            else
            {
                setBg(mode, rgb);
                mode.active = 3;
            }
            i += 4;
        }
    }
}

std::optional<AnsiLiteralSpan> spanAtOrBeforeCursor(
    const std::vector<ParsedSequence>& sequences, int cursorX)
{
    std::optional<AnsiLiteralSpan> previous;
    std::optional<AnsiLiteralSpan> nextSpan;
    for(std::size_t i = 0; i < sequences.size();)
    {
        int start = sequences[i].start;
        int end = sequences[i].end;
        std::size_t next = i + 1;
        while(next < sequences.size() && sequences[next].start == end)
        {
            end = sequences[next].end;
            ++next;
        }

        if(cursorX >= start && cursorX <= end)
            return AnsiLiteralSpan{start, end - start};
        if(end <= cursorX)
            previous = AnsiLiteralSpan{start, end - start};
        else if(start >= cursorX && !nextSpan)
            nextSpan = AnsiLiteralSpan{start, end - start};

        i = next;
    }
    return previous ? previous : nextSpan;
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

bool applyVisualColorRange(Editor& editor, VisualColorRange range,
                           const std::string& code)
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
    (*editor.lines)[range.startY].insert((std::size_t)range.startX, code);

    if(editor.cursorY)
        *editor.cursorY = range.startY;
    if(editor.cursorX)
        *editor.cursorX = range.startX;
    if(editor.dirty)
        *editor.dirty = true;
    editor.needsFullRedraw = true;
    return true;
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

ColorSelectorMode ColorSelectorMode::fromAnsiColor(color::AnsiColor ansi,
                                                   bool useBackground)
{
    ColorSelectorMode mode{useBackground};
    const std::string_view literal = color::literal(ansi);
    if(std::optional<ParsedSequence> sequence = parseSequenceAt(literal, 0))
        applyParams(mode, sequence->params);
    mode.active = useBackground ? 3 : 0;
    return mode;
}

ColorSelectorMode ColorSelectorMode::forVisualRange(VisualColorRange range,
                                                    bool useBackground)
{
    ColorSelectorMode mode{useBackground};
    mode.visualTarget = range;
    return mode;
}

std::optional<ColorSelectorMode> ColorSelectorMode::fromAnsiLiteralAtCursor(
    const Editor& editor)
{
    if(!editor.lines || !editor.cursorX || !editor.cursorY ||
       *editor.cursorY < 0 || *editor.cursorY >= (int)editor.lines->size())
    {
        return std::nullopt;
    }

    const int row = *editor.cursorY;
    const std::string& line = (*editor.lines)[row];
    const std::vector<ParsedSequence> sequences = parseSequences(line);
    std::optional<AnsiLiteralSpan> span =
        spanAtOrBeforeCursor(sequences,
                             std::clamp(*editor.cursorX, 0, (int)line.size()));
    if(!span)
        return std::nullopt;

    ColorSelectorMode mode;
    for(const ParsedSequence& sequence : sequences)
    {
        if(sequence.start >= span->start &&
           sequence.end <= span->start + span->length)
        {
            applyParams(mode, sequence.params);
        }
    }

    mode.replacing = true;
    mode.replaceRow = row;
    mode.replaceStartX = span->start;
    mode.replaceLength = span->length;
    return mode;
}

std::optional<ColorSelectorMode>
ColorSelectorMode::fromAnsiLiteralAtCursorAndRemove(Editor& editor)
{
    std::optional<ColorSelectorMode> mode = fromAnsiLiteralAtCursor(editor);
    if(!mode || !editor.lines || !editor.cursorX || !editor.cursorY ||
       mode->replaceRow < 0 ||
       mode->replaceRow >= (int)editor.lines->size())
    {
        return std::nullopt;
    }

    std::string& line = (*editor.lines)[mode->replaceRow];
    const int start = std::clamp(mode->replaceStartX, 0, (int)line.size());
    const int length =
        std::clamp(mode->replaceLength, 0, (int)line.size() - start);
    line.erase((std::size_t)start, (std::size_t)length);
    *editor.cursorY = mode->replaceRow;
    *editor.cursorX = start;
    if(editor.dirty)
        *editor.dirty = true;
    editor.saveState();

    mode->replaceStartX = start;
    mode->replaceLength = 0;
    return mode;
}

void ColorSelectorMode::on_enter(ModeContext& ctx)
{
    active = std::clamp(active, 0, 5);
    fgRed = std::clamp(fgRed, 0, 255);
    fgGreen = std::clamp(fgGreen, 0, 255);
    fgBlue = std::clamp(fgBlue, 0, 255);
    bgRed = std::clamp(bgRed, 0, 255);
    bgGreen = std::clamp(bgGreen, 0, 255);
    bgBlue = std::clamp(bgBlue, 0, 255);
    replaceRow = std::max(0, replaceRow);
    replaceStartX = std::max(0, replaceStartX);
    replaceLength = std::max(0, replaceLength);
    resetBackdrop();
    ctx.requestFullRedraw();
    Terminal::setCursorBlock();
}

void ColorSelectorMode::on_exit(ModeContext& ctx)
{
    resetBackdrop();
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
            if(visualTarget)
            {
                if(applyVisualColorRange(*ctx.editor, *visualTarget, code))
                {
                    ctx.setStatusMessage("colored selected text");
                    ctx.editor->saveState();
                }
                visualTarget.reset();
            }
            else if(replacing && ctx.editor->lines && ctx.editor->cursorX &&
               ctx.editor->cursorY && replaceRow >= 0 &&
               replaceRow < (int)ctx.editor->lines->size())
            {
                std::string& line = (*ctx.editor->lines)[replaceRow];
                const int start = std::clamp(replaceStartX, 0, (int)line.size());
                const int length =
                    std::clamp(replaceLength, 0, (int)line.size() - start);
                line.erase((std::size_t)start, (std::size_t)length);
                line.insert((std::size_t)start, code);
                *ctx.editor->cursorY = replaceRow;
                *ctx.editor->cursorX = start + (int)code.size();
                if(ctx.editor->dirty)
                    *ctx.editor->dirty = true;
                ctx.setStatusMessage("updated RGB ANSI style");
            }
            else
            {
                for(char ch : code)
                    ctx.editor->insertChar(ch);
                ctx.setStatusMessage("inserted RGB ANSI style");
            }
            ctx.editor->saveState();
            ctx.requestFullRedraw();
        }
        return exitState(ctx);
    }

    return std::nullopt;
}

void ColorSelectorMode::draw(Editor& editor)
{
    drawBackdropIfNeeded(editor);

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
    const std::string footer =
        std::string(" j/k select  h/l +/-1  H/L +/-10  ^B bold  ^I italic  "
                    "Enter ") +
        (replacing ? "replace" : "insert") + "  q/Esc cancel";
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
