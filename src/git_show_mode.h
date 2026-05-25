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

#include "git_log_mode.h"

namespace editor::statemachine
{
struct GitShowCommitMode
{
    static constexpr const char* name()
    {
        return "GITSHOW";
    }

    std::string commitHash;
    std::vector<std::string> lines;
    int scrollOffset = 0;
    int horizontalOffset = 0;
    std::optional<GitLogMode> returnLog;
    bool searchActive = false;
    bool searchForward = true;
    std::string searchQuery;
    int searchIndex = 0;
    int searchPrevIndex = 0;
    int searchPrevScroll = 0;

    GitShowCommitMode() = default;

    GitShowCommitMode(std::string hash, std::vector<std::string> content,
                      std::optional<GitLogMode> returnLogState = std::nullopt)
        : commitHash(std::move(hash)), lines(std::move(content)),
          returnLog(std::move(returnLogState))
    {
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx,
                                    const ModeKeyEvent& event);

    void draw(Editor& editor) const;
#ifdef UVIM_TESTING
    static std::string testRenderLine(const Theme& theme, std::string_view line,
                                      std::string_view query,
                                      bool useDefaultColors);
#endif
};
} // namespace editor::statemachine
