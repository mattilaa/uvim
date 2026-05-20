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
struct BufferBrowserMode
{
    static constexpr const char* name()
    {
        return "BUFFERS";
    }

    std::vector<BufferMatch> bufferMatches;
    std::string bufferQuery;
    int bufferCursor = 0;
    int bufferOffset = 0;

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, int key);

    void draw(Editor& editor) const;

private:
    void updateMatches(Editor& editor);
    void selectMatch(Editor& editor);
    void closeSelectedBuffer(Editor& editor);
    void closeMatchedBuffers(Editor& editor);
};
} // namespace editor::statemachine
