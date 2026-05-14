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

struct FuzzyFindMode
{
    static constexpr const char* name()
    {
        return "FUZZY";
    }

    std::vector<FuzzyMatch> matches;
    std::vector<FileEntry> projectFiles;
    std::string query;
    int cursor = 0;
    int offset = 0;
    bool projectFilesInitialized = false;

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, int key);

    void draw(Editor& editor) const;

private:
    void initializeFiles(Editor& editor);
    void updateMatches(Editor& editor);
    void moveDown(Editor& editor);
    void moveUp(Editor& editor);
    void halfPageDown(Editor& editor);
    void halfPageUp(Editor& editor);
    void addChar(Editor& editor, char c);
    void backspace(Editor& editor);
    void deleteWord(Editor& editor);
    void clearQuery(Editor& editor);
    void toggleGitignore(Editor& editor);
    bool select(Editor& editor);
};
