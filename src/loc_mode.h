#pragma once

#include "file_entry.h"
#include "mode.h"
#include "mode_commands.h"
#include "mode_context.h"
#include "mode_state.h"
#include "search_types.h"

#include <chrono>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace editor::statemachine
{
struct LocListMode
{
    static constexpr const char* name()
    {
        return "LOC";
    }

    enum class SortMode
    {
        Normal,
        Desc,
        Asc,
    };

    int cursor = 0;
    int offset = 0;
    int countWidth = 1;
    SortMode sortMode = SortMode::Normal;
    std::optional<Mode> returnMode;
    int returnBrowseCursor = 0;
    int returnBrowseOffset = 0;
    std::string returnBrowseDirectory;

    LocListMode() = default;

    explicit LocListMode(std::optional<Mode> returnMode, int browseCursor = 0,
                         int browseOffset = 0, std::string browseDirectory = {})
        : returnMode(returnMode), returnBrowseCursor(browseCursor),
          returnBrowseOffset(browseOffset),
          returnBrowseDirectory(std::move(browseDirectory))
    {
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx,
                                    const ModeKeyEvent& event);

    void draw(Editor& editor) const;
};
} // namespace editor::statemachine
