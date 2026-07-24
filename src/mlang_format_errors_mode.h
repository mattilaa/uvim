#pragma once

#include "mode.h"
#include "mode_context.h"
#include "mode_state.h"

#include <optional>

namespace editor::statemachine
{
struct MlangFormatErrorsMode
{
    static constexpr const char* name()
    {
        return "MLANG FORMAT";
    }

    int cursor = 0;
    int offset = 0;
    bool warnings = false;

    MlangFormatErrorsMode() = default;
    explicit MlangFormatErrorsMode(bool warnings) : warnings(warnings) {}

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx,
                                    const ModeKeyEvent& event);

    void draw(Editor& editor) const;
};
} // namespace editor::statemachine
