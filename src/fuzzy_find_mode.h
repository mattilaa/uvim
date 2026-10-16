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

namespace widgets
{
class PickerCanvas;
}

namespace editor::statemachine
{
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
    bool filenameFirst = false;
    bool previewEnabled = true;
    // Pattern the current matches were computed for (query minus any
    // ":<line>" suffix); typing only the suffix keeps the matches.
    std::string matchedPattern;
    bool matchesValid = false;
    std::unordered_set<std::string> selectedFiles;
    // Preview pane cache (floating picker).
    mutable std::string previewPath;
    mutable std::vector<std::string> previewLines;

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx,
                                    const ModeKeyEvent& event);

    void draw(Editor& editor) const;
    void refreshFileIndex(Editor& editor);

private:
    void appendPreview(widgets::PickerCanvas& canvas) const;
    void initializeFiles(Editor& editor);
    void updateMatches(Editor& editor);
    void moveDown(Editor& editor);
    void moveUp(Editor& editor);
    void halfPageDown(Editor& editor);
    void halfPageUp(Editor& editor);
    bool refreshForQueryChange(Editor& editor);
    void addChar(Editor& editor, char c);
    void backspace(Editor& editor);
    void deleteWord(Editor& editor);
    void clearQuery(Editor& editor);
    void toggleFilenameFirst(Editor& editor);
    void toggleGitignore(Editor& editor);
    void toggleSelection();
    void prewarmAroundCursor(Editor& editor) const;
    bool select(Editor& editor);
    bool openSelected(Editor& editor);
};
} // namespace editor::statemachine
