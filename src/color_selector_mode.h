#pragma once

#include "color_constant.h"
#include "mode_context.h"
#include "mode_events.h"
#include "mode_state.h"

#include <optional>

class Editor;

namespace editor::statemachine
{
struct ColorSelectorMode
{
    static constexpr const char* name()
    {
        return "COLOR_SELECTOR";
    }

    int fgRed = 255;
    int fgGreen = 255;
    int fgBlue = 255;
    int bgRed = 0;
    int bgGreen = 0;
    int bgBlue = 0;
    int active = 0;
    bool bold = false;
    bool italic = false;
    bool replacing = false;
    int replaceRow = 0;
    int replaceStartX = 0;
    int replaceLength = 0;

    ColorSelectorMode() = default;

    explicit ColorSelectorMode(bool useBackground)
        : active(useBackground ? 3 : 0)
    {
    }

    static ColorSelectorMode fromAnsiColor(color::AnsiColor ansi,
                                           bool useBackground);
    static std::optional<ColorSelectorMode> fromAnsiLiteralAtCursor(
        const Editor& editor);

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx,
                                    const ModeKeyEvent& event);

    void draw(Editor& editor) const;

private:
    mutable bool backdropDrawn = false;
    mutable int backdropRows = 0;
    mutable int backdropCols = 0;
};
} // namespace editor::statemachine
