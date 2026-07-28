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
    bool previewEnabled = false;
    bool searchPending = false;
    std::chrono::steady_clock::time_point searchDueAt{};
    std::unordered_set<int> selectedMatches;
#ifdef UVIM_ENABLE_RG_CACHE
    std::chrono::steady_clock::time_point lastRgCacheIndexRefresh{};
    bool rgCacheIndexing = false;
    int rgCacheIndexed = 0;
    int rgCacheTotal = 0;
    int rgCacheUpdated = 0;
    int rgCacheRemoved = 0;
    int rgCacheJobs = 0;
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
    void scheduleSearch(Editor& editor);
    void flushPendingSearch(Editor& editor);
#ifdef UVIM_ENABLE_RG_CACHE
    bool syncRgCache(Editor& editor, bool force);
    bool performCachedSearch(Editor& editor);
#endif
    bool performRipgrepSearch(Editor& editor);
    void searchInFile(const std::string& filepath, std::string_view query);
    bool isTextFile(const std::string& filepath) const;
    bool isBinaryFile(const std::string& filepath) const;
    std::string trimString(const std::string& str) const;
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
    bool openSelected(Editor& editor);
};
} // namespace editor::statemachine
