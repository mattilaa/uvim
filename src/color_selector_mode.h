#pragma once

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

    int red = 255;
    int green = 255;
    int blue = 255;
    int active = 0;

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx,
                                    const ModeKeyEvent& event);

    void draw(Editor& editor) const;
};
} // namespace editor::statemachine
