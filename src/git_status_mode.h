#pragma once

#include "mode.h"
#include "mode_context.h"
#include "mode_state.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace editor::statemachine
{
struct GitStatusMode
{
    static constexpr const char* name()
    {
        return "GITSTATUS";
    }

    std::string repoDir;
    std::vector<std::string> lines;
    int offset = 0;
    std::optional<Mode> returnMode;
    std::string returnBrowseDirectory;
    int returnBrowseCursor = 0;
    int returnBrowseOffset = 0;

    GitStatusMode() = default;
    explicit GitStatusMode(std::string directory)
        : repoDir(std::move(directory))
    {
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);
    std::optional<ModeState> handle(
        ModeContext& ctx,
        const ModeKeyEvent& event);
    void draw(Editor& editor) const;
    void refresh(Editor& editor);
};
} // namespace editor::statemachine
