#pragma once

#include "file_entry.h"
#include "mode.h"
#include "mode_commands.h"
#include "mode_context.h"
#include "mode_state.h"
#include "search_types.h"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace widgets
{
class PickerCanvas;
}

namespace editor::statemachine
{
struct GrepSearchMode
{
    static constexpr const char* name()
    {
        return "GREP";
    }

    std::vector<GrepMatch> matches;
    std::string query;
    int cursor = 0;
    int offset = 0;
    bool searching = false;
    bool caseSensitive = false;
    bool previewEnabled = true;
    // Preview pane cache (floating picker).
    mutable std::string previewPath;
    mutable std::vector<std::string> previewLines;
    bool searchPending = false;
    std::chrono::steady_clock::time_point searchDueAt{};
    std::unordered_set<int> selectedMatches;
    // Identifies the newest background search; older results are dropped.
    std::uint64_t activeSearch = 0;
    std::uint64_t activeSearchTask = 0;
    // What the last search ran for; a changed ":<line>" suffix alone does
    // not trigger a new search.
    std::string lastSearchedPattern;
    bool lastSearchedCaseSensitive = false;
#ifdef UVIM_ENABLE_RG_CACHE
    std::chrono::steady_clock::time_point lastRgCacheIndexRefresh{};
    bool rgCacheSyncPending = false;
    std::string lastCachedQuery;
    bool lastCachedCaseSensitive = false;
    std::vector<GrepMatch> lastCachedMatches;
#endif

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx,
                                    const ModeKeyEvent& event);

    void draw(Editor& editor);
    void refreshFileIndex(Editor& editor);
    bool processIdle(Editor& editor);

private:
    void loadFileIndex(Editor& editor);
    void initialize(Editor& editor);
    void performSearch(Editor& editor);
    // False when the query change needs no new search.
    bool scheduleSearch(Editor& editor);
    void flushPendingSearch(Editor& editor);
    void startBackgroundSearch(Editor& editor);
    bool adoptBackgroundResults(Editor& editor);
    // Query without a trailing ":<line>" jump suffix (see picker_query.h).
    std::string searchPattern() const;
    void appendStatusBar(Editor& editor, std::string& output) const;
    void appendPreview(Editor& editor, widgets::PickerCanvas& canvas) const;
#ifdef UVIM_ENABLE_RG_CACHE
    bool syncRgCache(Editor& editor, bool force);
    bool performCachedSearch(Editor& editor);
#endif
    // Worker-thread safe: only touch this object's matches/query.
    bool performRipgrepSearch(bool respectGitignore,
                              const std::function<bool()>& cancelled);
    void searchInFile(const std::string& filepath, std::string_view query);
    static std::vector<FileEntry>
    collectProjectFiles(bool useGitFileIndex, bool respectGitignore,
                        const std::function<bool()>& cancelled = {});
    static bool isTextFile(const std::string& filepath);
    static bool isBinaryFile(const std::string& filepath);
    static std::string trimString(const std::string& str);
    bool selectMatch(Editor& editor);
    void resultUp(Editor& editor);
    void resultDown(Editor& editor);
    void resultHalfPageUp(Editor& editor);
    void resultHalfPageDown(Editor& editor);
    void searchAddChar(Editor& editor, char c);
    void searchBackspace(Editor& editor);
    void searchDeleteWord(Editor& editor);
    void searchClear();
    void toggleGitignore(Editor& editor);
    void togglePreview();
    void toggleSelection();
    void prewarmAroundCursor(Editor& editor) const;
    bool openSelected(Editor& editor);
};
} // namespace editor::statemachine
