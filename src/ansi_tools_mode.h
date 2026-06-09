#pragma once

#include "mode_context.h"
#include "mode_events.h"
#include "mode_state.h"
#include "popup_base.h"

#include <optional>

class Editor;

namespace editor::statemachine
{
struct AnsiToolsMode : PopupBase
{
    static constexpr const char* name()
    {
        return "ANSI_TOOLS";
    }

    int cursor = 0;
    int rowOffset = 0;

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx,
                                    const ModeKeyEvent& event);

    void draw(Editor& editor) const;

private:
    void clampToVisible(int visibleRows);

    bool insertedDuringSession = false;
};
} // namespace editor::statemachine
