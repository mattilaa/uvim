#pragma once

#include "command_palette.h"
#include "mode.h"
#include "mode_commands.h"
#include "mode_context.h"
#include "mode_state.h"

#include <optional>
#include <string>
#include <vector>

class Editor;

namespace editor::statemachine
{
// Command palette (Space Space, :palette): a floating picker like Ctrl-P
// that lists every ex command with its description. Typing filters by name
// and description, Ctrl-J/K (or arrows) move, Enter runs the command.
struct CommandPaletteMode
{
    static constexpr const char* name()
    {
        return "PALETTE";
    }

    std::string query;
    std::vector<command_palette::Entry> entries;
    std::vector<command_palette::Match> matches;
    int cursor = 0;
    // First visible row; draw() keeps the cursor in view.
    mutable int offset = 0;

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(
        ModeContext& ctx,
        const ModeKeyEvent& event);

    void draw(Editor& editor) const;

private:
    void refilter();
};
} // namespace editor::statemachine
