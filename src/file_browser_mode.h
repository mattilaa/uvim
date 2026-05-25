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
struct FileBrowserOp
{
    enum class Kind
    {
        Delete,
        Move,
        Paste,
        Mkdir,
    };
    Kind kind = Kind::Delete;
    // Delete: pairs = (trashPath, originalPath) — files currently at trashPath
    // Move:   pairs = (srcPath, dstPath)       — files currently at dstPath
    // Paste:  pairs = (trashPath, dstPath)     — files currently at dstPath;
    //                                            trashPath allocated on undo
    // Mkdir:  pairs = ("", createdPath)        — dir exists at createdPath
    std::vector<std::pair<std::string, std::string>> pairs;
};

struct FileBrowserMode
{
    static constexpr const char* name()
    {
        return "BROWSE";
    }

    std::vector<FileEntry> fileList;
    std::string currentDirectory;
    std::string previousFile;
    int browserCursor = 0;
    int browserOffset = 0;
    bool showHidden = false;
    bool filterActive = false;
    std::string filterQuery;
    std::vector<int> filterMatches;
    std::vector<int> searchMatches;
    std::unordered_map<std::string, std::vector<int>> searchMatchCache;
    std::string lastSearchPattern;
    char lastSearchPrefix = 0;
    int currentSearchMatch = -1;
    std::vector<std::string> searchTabCandidates;
    std::string searchTabSeed;
    int searchTabIndex = -1;
    std::unordered_set<std::string> selectedFiles;
    std::vector<std::string> copyBuffer;
    std::vector<std::string> deleteTargets;
    bool confirmingDelete = false;
    bool confirmingDirCreate = false;
    bool confirmingFileReplace = false;
    std::string pendingFilePath;
    std::string pendingParentRel;
    bool moveMode = false;
    std::vector<FileBrowserOp> undoStack;
    std::vector<FileBrowserOp> redoStack;
    std::vector<std::string> historyBack;
    std::vector<std::string> historyForward;
    bool visualMode = false;
    int visualAnchor = 0;
    bool focusPreviousFile = false;
    std::unordered_set<std::string> preVisualSelected;
    std::shared_ptr<CommandPrompt> commandPrompt;

    FileBrowserMode() = default;

    explicit FileBrowserMode(std::string startDir, std::string prevFile = {},
                             bool focusPrevious = false)
        : currentDirectory(std::move(startDir)),
          previousFile(std::move(prevFile)), focusPreviousFile(focusPrevious)
    {
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx,
                                    const ModeKeyEvent& event);

    void draw(Editor& editor) const;

private:
    void loadDirectory(ModeContext& ctx, std::string pathStr);
    void navigateTo(ModeContext& ctx, std::string pathStr);
    int firstNonDotDotIndex() const;
    void focusPreviousFileEntry(ModeContext& ctx);
    void updateVisualSelection();
    void updateFilter(ModeContext& ctx);
    int listSize() const;
    const FileEntry* entryAt(int index) const;
    std::string formatFileSize(size_t size) const;
    std::string formatFileTime(time_t time) const;
    std::optional<ModeState> executeCommand(ModeContext& ctx,
                                            std::string_view commandLine);
};
} // namespace editor::statemachine
