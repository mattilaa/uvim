#pragma once

#include "mode.h"
#include "mode_context.h"
#include "mode_state.h"

#include <optional>

namespace editor::statemachine
{
enum class MlangFormatErrorsSource
{
    FormatErrors,
    LspErrors,
    LspWarnings,
};

struct MlangFormatErrorsMode
{
    static constexpr const char* name()
    {
        return "MLANG FORMAT";
    }

    int cursor = 0;
    int offset = 0;
    MlangFormatErrorsSource source = MlangFormatErrorsSource::FormatErrors;

    MlangFormatErrorsMode() = default;
    explicit MlangFormatErrorsMode(MlangFormatErrorsSource source)
        : source(source),
          warnings(source == MlangFormatErrorsSource::LspWarnings)
    {
    }

    bool warnings = false;

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx,
                                    const ModeKeyEvent& event);

    void draw(Editor& editor) const;
};
} // namespace editor::statemachine
