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

#include "git_stage_mode.h"

namespace editor::statemachine
{
struct GitPatchMode
{
    static constexpr const char* name()
    {
        return "GITPATCH";
    }

    struct Hunk
    {
        std::string file;
        std::vector<std::string> lines;
        std::string patch;
    };

    std::vector<Hunk> hunks;
    int hunkIndex = 0;
    int scrollOffset = 0;
    bool finished = false;
    std::string repoRoot;
    std::string repoDir;
    std::string targetHash;
    std::vector<std::string> fixupFiles;
    GitStageMode returnStage;

    GitPatchMode() = default;

    GitPatchMode(std::vector<Hunk> items, std::string root, std::string dir,
                 std::string hash, std::vector<std::string> files,
                 GitStageMode stage)
        : hunks(std::move(items)), repoRoot(std::move(root)),
          repoDir(std::move(dir)), targetHash(std::move(hash)),
          fixupFiles(std::move(files)), returnStage(std::move(stage))
    {
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, int key);

    void draw(Editor& editor) const;
};
} // namespace editor::statemachine
