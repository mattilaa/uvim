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
struct GitStageMode
{
    static constexpr const char* name()
    {
        return "GITSTAGE";
    }

    struct Node
    {
        std::string name;
        std::string repoPath;
        bool isDir = false;
        bool expanded = true;
        char indexStatus = ' ';
        char worktreeStatus = ' ';
        std::vector<int> children;
    };

    struct VisibleEntry
    {
        int node = -1;
        int depth = 0;
    };

    enum class RowKind
    {
        Header,
        Hint,
        File,
        Blank,
        Summary
    };

    enum class FileGroup
    {
        None,
        Staged,
        Unstaged,
        Untracked
    };

    struct StatusRow
    {
        RowKind kind = RowKind::Blank;
        FileGroup group = FileGroup::None;
        std::string prefix;
        std::string path;
        char indexStatus = ' ';
        char worktreeStatus = ' ';
    };

    std::vector<Node> nodes;
    std::vector<VisibleEntry> visible;
    std::vector<StatusRow> rows;
    std::vector<int> fileRows;
    std::vector<std::string> diffLines;
    std::string repoRoot;
    std::string repoDir;
    std::string viewRoot;
    std::string diffPath;
    bool diffVisible = false;
    bool diffStaged = false;
    bool diffDirty = true;
    std::chrono::steady_clock::time_point lastCursorMove;
    std::unordered_map<std::string, std::vector<std::string>> diffCache;
    std::vector<std::string> diffCacheOrder;
    enum class UntrackedMode
    {
        TrackedOnly,
        UntrackedOnly,
        Both
    };
    UntrackedMode untrackedMode = UntrackedMode::TrackedOnly;
    bool showChangedOnly = false;
    std::unordered_set<std::string> fixupMarked;
    int pendingG = 0;
    int cursor = 0;
    int offset = 0;
    int listHorizontalOffset = 0;
    int diffOffset = 0;
    int diffHorizontalOffset = 0;
    std::optional<Mode> returnMode;
    int returnBrowseCursor = 0;
    int returnBrowseOffset = 0;
    std::string returnBrowseDirectory;

    GitStageMode() = default;
    GitStageMode(std::vector<Node> items, std::string root, std::string dir);

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx,
                                    const ModeKeyEvent& event);

    void draw(Editor& editor) const;
#ifdef UVIM_TESTING
    static int testContentRows(int screenRows, int screenCols);
#endif

private:
    void refreshDiff(Editor& editor);
    bool refreshStatus(Editor& editor);
    void clampCursor();
    void keepCursorVisible(const Editor& editor);
    int selectedRowIndex() const;
    int maxListHorizontalOffset(int listWidth) const;
};
} // namespace editor::statemachine
