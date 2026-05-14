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

struct CommandOutputMode
{
    static constexpr const char* name()
    {
        return "RUN";
    }

    std::string command;
    std::vector<std::string> lines;
    int cursor = 0;
    int offset = 0;

    bool visualMode = false;
    int visualAnchor = 0;
    std::unordered_set<int> selectedLines;
    std::unordered_set<int> preVisualSelected;

    bool searchActive = false;
    std::string searchQuery;
    int searchPrevCursor = 0;
    int searchPrevOffset = 0;

    std::string returnDirectory;
    int returnBrowseCursor = 0;
    int returnBrowseOffset = 0;
    std::string previousFile;

    // Interactive process state. While `running`, keys are forwarded to the
    // child via `childFd` and output is appended to `lines` as it streams
    // in. After the child exits the view falls back to normal navigation.
    bool running = false;
    bool started = false;
    int childFd = -1;
    int childPid = -1;
    int exitCode = 0;
    std::string pendingLine;
    bool sawCarriageReturn = false;

    CommandOutputMode() = default;

    CommandOutputMode(std::string cmd, std::string dir = {},
                      int browseCursor = 0, int browseOffset = 0,
                      std::string prevFile = {})
        : command(std::move(cmd)), returnDirectory(std::move(dir)),
          returnBrowseCursor(browseCursor), returnBrowseOffset(browseOffset),
          previousFile(std::move(prevFile))
    {
    }

    CommandOutputMode(std::string cmd, std::vector<std::string> outputLines,
                      std::string dir = {}, int browseCursor = 0,
                      int browseOffset = 0, std::string prevFile = {})
        : command(std::move(cmd)), lines(std::move(outputLines)),
          returnDirectory(std::move(dir)), returnBrowseCursor(browseCursor),
          returnBrowseOffset(browseOffset), previousFile(std::move(prevFile))
    {
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, int key);

    void draw(Editor& editor) const;

    // Called from the main loop's idle tick. Returns true if anything new
    // arrived (output bytes or process exit) so the caller can request a
    // redraw.
    bool poll();

private:
    int contentRows(const Editor& editor) const;
    int displayHeight(int idx, int cols) const;
    void clampOffsetToCursor(const Editor& editor);
    void updateVisualSelection();
    void yankSelection(Editor& editor);
    std::optional<ModeState> returnToFileBrowser();

    void startProcess();
    void forwardKeyToProcess(int key);
    void absorbBytes(const char* data, size_t len);
    void reapChild(bool waitForExit);
};
