#pragma once

#include "mode_context.h"
#include "mode_events.h"
#include "mode_state.h"

#include <optional>

class Editor;

namespace editor::statemachine
{
struct GlyphSelectMode
{
    static constexpr const char* name()
    {
        return "GLYPH_SELECT";
    }

    int cursor = 0;
    int rowOffset = 0;

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx,
                                    const ModeKeyEvent& event);

    void draw(Editor& editor) const;

private:
    void clampToVisible(int columns, int visibleRows);

    mutable bool backdropDrawn = false;
    mutable int backdropRows = 0;
    mutable int backdropCols = 0;
    bool insertedDuringSession = false;
};
} // namespace editor::statemachine
