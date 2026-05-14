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

struct GitFixupMode
{
    static constexpr const char* name()
    {
        return "GITFIXUP";
    }

    struct Entry
    {
        std::string hash;
        std::string subject;
    };

    std::vector<Entry> entries;
    int cursor = 0;
    int offset = 0;
    bool confirmActive = false;
    std::string confirmHash;
    std::string repoRoot;
    std::string repoDir;
    std::vector<std::string> fixupFiles;
    GitStageMode returnStage;

    GitFixupMode() = default;

    GitFixupMode(std::vector<Entry> items, std::string root, std::string dir,
                 std::vector<std::string> files, GitStageMode stage)
        : entries(std::move(items)), repoRoot(std::move(root)),
          repoDir(std::move(dir)), fixupFiles(std::move(files)),
          returnStage(std::move(stage))
    {
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, int key);

    void draw(Editor& editor) const;
};
