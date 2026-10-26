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
    // :showchanges view: d drops a hunk from the unsaved buffer, u undoes.
    bool unsavedChanges = false;
    int currentHunk = -1; // selected hunk (n/N, ]/[), -1 until navigated
    int droppedHunks = 0; // drops made here that u can still undo

    // Commit opened from git log (repoRoot set): d drops hunks into a
    // pending new version of the commit, shown as its new diff; v toggles
    // to the dropped changes; c rewrites the commit; u undoes the last
    // pending drop or, after c, the rewrite.
    std::string repoRoot;
    std::vector<std::string> originalPlain; // plain diff (patch source)
    std::vector<int> hunkMap; // original hunk index of each hunk in lines
    std::vector<int> droppedOriginal; // pending dropped original hunks

    struct PendingEdit
    {
        std::vector<std::string> lines;
        std::vector<int> hunkMap;
        std::vector<int> dropped;
    };

    std::vector<PendingEdit> pendingUndo;
    bool showDropped = false;
    std::vector<std::string> newCommitLines; // `lines` while showDropped
    bool confirmRewrite = false;
    bool confirmQuit = false;
    bool historyChanged = false; // the git log needs reloading on return
    std::string rewrittenFrom;   // original hash after a rewrite (for u)
    std::string rewriteOriginalHead;
    std::string rewriteNewHead;

    GitShowCommitMode() = default;

    GitShowCommitMode(
        std::string hash,
        std::vector<std::string> content,
        std::optional<GitLogMode> returnLogState = std::nullopt)
        : commitHash(std::move(hash)), lines(std::move(content)),
          returnLog(std::move(returnLogState))
    {
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(
        ModeContext& ctx,
        const ModeKeyEvent& event);

    void draw(Editor& editor) const;

private:
    int viewKind() const;
    bool commitEditable() const;
    void dropCommitHunk(ModeContext& ctx);
    void undoCommitEdit(ModeContext& ctx);
    void toggleDroppedView(ModeContext& ctx);
    void startRewrite(ModeContext& ctx);
    void performRewrite(ModeContext& ctx);
    std::string droppedPatch() const;
    int hunkAtView() const;
    void scrollToHunk(ModeContext& ctx, int index);
    void jumpToHunk(ModeContext& ctx, bool forward);
    void reloadChanges(ModeContext& ctx);
    void dropCurrentHunk(ModeContext& ctx);
    void undoDrop(ModeContext& ctx);

public:
#ifdef UVIM_TESTING
    static std::string testRenderLine(
        const Theme& theme,
        std::string_view line,
        std::string_view query,
        bool useDefaultColors);
#endif
};
} // namespace editor::statemachine
