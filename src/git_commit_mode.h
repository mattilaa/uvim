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

struct GitCommitMode
{
    static constexpr const char* name()
    {
        return "GITCOMMIT";
    }

    std::string repoRoot;
    std::string repoDir;
    std::vector<std::string> messageLines = {""};
    int messageCursorRow = 0;
    int messageCursorCol = 0;
    int messageTopRow = 0;
    bool insertMode = false;
    bool commandActive = false;
    std::string commandLine;
    std::vector<std::string> stagedLines;
    std::string currentBranch;
    bool hasStagedFiles = false;
    bool stagedDirty = true;
    enum class Action
    {
        CommitStaged,
        RevertCommit,
        RebaseTodo
    };
    Action action = Action::CommitStaged;
    std::string revertHash;
    std::string revertSubject;
    std::string rebaseBaseHash;
    std::string rebaseHeadHash;
    int rebaseCommandCount = 0;
    std::optional<GitLogMode> returnLog;

    GitCommitMode() = default;

    GitCommitMode(std::string root, std::string dir)
        : repoRoot(std::move(root)), repoDir(std::move(dir))
    {
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);
    std::optional<ModeState> handle(ModeContext& ctx, int key);
    void draw(Editor& editor) const;

private:
    void refreshStaged();
};
