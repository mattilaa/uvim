#pragma once
#include "assembly_instruction_help.h"
#include "mode_context.h"
#include "mode_state.h"
#include "preview_highlight.h"
#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace editor::statemachine
{
struct DebuggerTransport;
#ifdef UVIM_ENABLE_SEARCH_TOOLS
struct FuzzyFindMode;
#endif

struct DebuggerBreakpointLocation
{
    std::string path;
    int line = 0;
    bool enabled = true;
};

struct DebuggerBreakpoint
{
    std::string id, description;
    bool enabled = true, pending = false;
    std::vector<DebuggerBreakpointLocation> locations;
};

struct DebuggerStackFrame
{
    std::string id, label, symbol, path, address, module;
    int line = 0;
    bool selected = false;
    bool sourceAvailable = false;
    std::vector<Token> colors;
};

struct DebuggerMode
{
    static constexpr const char* name()
    {
        return "DEBUG";
    }

    std::string arguments;
    bool attach = false;
    std::string backend = "lldb";
    std::string target;
    std::vector<std::string> programArguments;
    std::string state = "ready";
    std::string sourcePath;
    std::string executionPath;
    bool sourcePinned = false;
    bool followExecutionOnStop = false;
#ifdef UVIM_ENABLE_SEARCH_TOOLS
    std::shared_ptr<FuzzyFindMode> filePicker;
#endif
    int executionLine = 0;
    int frameIndex = 0;
    std::string frameFunction, stopFunction;
    std::vector<std::string> source, variables, threads, threadIds, output;
    std::vector<bool> assemblyOutputLines;
    std::vector<std::string> variableExpressions;
    // Source, output, variables, threads. Each pane preserves its selection.
    int pane = 0;
    std::array<int, 4> cursor{}, offset{};
    bool promptActive = false;
    std::string prompt;
    std::vector<std::string> completions;
    int completionIndex = -1;
    std::string completionOriginal;
    PreviewHighlight highlight;
    std::shared_ptr<DebuggerTransport> transport;
    bool started = false;
    bool hasStoppedSnapshot = false;
    std::vector<DebuggerBreakpoint> breakpoints;
    std::unordered_set<int> breakpointLines;
    bool breakpointListOpen = false;
    int breakpointCursor = 0, breakpointOffset = 0;
    bool sizePopupOpen = false;
    std::string sizePopupTitle;
    std::vector<std::string> sizePopupLines;
    int sizePopupCursor = 0, sizePopupOffset = 0;
    bool framePickerOpen = false, framePickerLoading = false;
    std::string framePickerError;
    std::vector<DebuggerStackFrame> stackFrames;
    std::vector<int> frameMatches;
    int framePickerCursor = 0, framePickerOffset = 0;
    bool framePreviewAssembly = false, framePreviewSourceAvailable = false;
    std::string framePreviewKey, framePreviewId, framePreviewPath;
    std::vector<std::string> framePreviewLines;
    PreviewHighlight framePreviewHighlight;
    int framePreviewLine = 0, framePreviewToken = 0, framePreviewScroll = 0;
    int framePreviewCursor = 0;
    bool assemblyHelpOpen = false;
    int assemblyHelpOffset = 0;
    std::shared_ptr<assembly_instruction_help::State> assemblyHelp =
        std::make_shared<assembly_instruction_help::State>();
    std::vector<std::string> promptCommands, promptMenuEntries;
    int promptMenuCursor = 0, promptMenuOffset = 0;

    DebuggerMode() = default;

    DebuggerMode(std::string args, bool attaching)
        : arguments(std::move(args)), attach(attaching)
    {
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);
    std::optional<ModeState> handle(ModeContext& ctx,
                                    const ModeKeyEvent& event);
    void draw(Editor& editor);
    void processIdle(Editor& editor);
    void applyMessage(const std::string& message);
    bool selectSourceFile(const std::string& path, int line = 1);

private:
    // Keep brief running transitions from changing titles and command hints.
    std::optional<std::chrono::steady_clock::time_point> runningStatusDeadline;
    std::vector<std::string> drawnRows;
    std::string drawnFooter;
    int drawnWidth = 0;
    TaskQueue::TaskId assemblyHelpTask = 0;
    std::string assemblyHelpInstruction;
    std::optional<std::chrono::steady_clock::time_point> assemblyHelpRefreshAt;
    std::string selectedDisassembly() const;
    void updateAssemblyHelp(Editor& editor, bool immediate = false);
    void closeAssemblyHelp(Editor& editor);
    void execute(const std::string& command);
    void complete(ModeContext& ctx);
    void clamp(const Editor& editor);
    void updateBreakpointLines();
    void updatePromptMenu();
    void drawContent(Editor& editor);
    void toggleSelectedBreakpoint();
    void inspectSelectedVariable(char action);
    bool updateFramePicker(bool refresh = false);
    void filterFrames(bool selectCurrent = false);
    void updateFramePreview();
    const DebuggerStackFrame* previewFrame() const;
};
} // namespace editor::statemachine
