#pragma once

#include "mode_context.h"
#include "mode_events.h"
#include "mode_state.h"
#include "visual_color_range.h"
#include "widgets/popup_base.h"

#include <optional>

class Editor;

namespace editor::statemachine
{
struct ColorPickerMode : widgets::PopupBase
{
    static constexpr const char* name()
    {
        return "COLOR_PICKER";
    }

    int cursor = 0;
    int rowOffset = 0;
    bool background = false;
    std::optional<VisualColorRange> visualTarget;

    ColorPickerMode() = default;

    explicit ColorPickerMode(bool useBackground) : background(useBackground) {}

    static ColorPickerMode forVisualRange(VisualColorRange range,
                                          bool useBackground = false);

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx,
                                    const ModeKeyEvent& event);

    void draw(Editor& editor);

private:
    void clampToVisible(int columns, int visibleRows);
};
} // namespace editor::statemachine
