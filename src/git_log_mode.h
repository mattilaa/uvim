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
struct GitLogMode
{
    static constexpr const char* name()
    {
        return "GITLOG";
    }

    struct Entry
    {
        std::string hash;
        std::string date;
        std::string author;
        std::string graph;
        std::string refs;
        std::string subject;
        bool merge = false;

        Entry() = default;

        Entry(std::string hashValue, std::string subjectValue)
            : hash(std::move(hashValue)), subject(std::move(subjectValue))
        {
        }
    };

    std::vector<Entry> entries;
    std::vector<int> filtered;
    std::unordered_set<std::string> selectedHashes;
    bool rangeSelectActive = false;
    int rangeSelectAnchor = 0;
    std::unordered_set<std::string> rangeSelectBase;
    std::string query;
    int scrollOffset = 0;
    int cursor = 0;
    bool fileOnly = false;
    bool searchActive = false;
    bool searchForward = true;
    std::string searchQuery;
    int searchPrevCursor = 0;
    int searchPrevScroll = 0;
    std::string repoRoot;
    std::string repoDir;
    std::string filePath;
    bool prettyView = false;
    std::vector<std::string> previewLines;
    std::string previewHash;
    int diffOffset = 0;
    int diffHorizontalOffset = 0;
    bool diffDirty = true;
    std::chrono::steady_clock::time_point lastCursorMove;
    std::unordered_map<std::string, std::vector<std::string>> previewCache;
    std::vector<std::string> previewCacheOrder;

    GitLogMode() = default;

    GitLogMode(std::vector<Entry> items, bool fileOnlyLog,
               std::string root = {}, std::string dir = {},
               std::string file = {}, bool pretty = false)
        : entries(std::move(items)), fileOnly(fileOnlyLog),
          repoRoot(std::move(root)), repoDir(std::move(dir)),
          filePath(std::move(file)), prettyView(pretty)
    {
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx,
                                    const ModeKeyEvent& event);

    void draw(Editor& editor) const;

    void rebuildFilter(Editor& editor);
    void ensurePrettyPreview(Editor& editor);
    static const char* graphPrettyFormatArg();
    static std::optional<Entry> parseGraphEntry(std::string_view line);
    static void applyGraphConnector(Entry& entry,
                                    std::string_view connectorLine);
#ifdef UVIM_TESTING
    static std::string testRenderLine(const Theme& theme, const Entry& entry,
                                      std::string_view query, bool selected,
                                      int screenCols);
#endif
};
} // namespace editor::statemachine
