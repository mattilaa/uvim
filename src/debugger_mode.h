#pragma once
#include "mode_context.h"
#include "mode_state.h"
#include "preview_highlight.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace editor::statemachine
{
struct DebuggerTransport;

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
    int executionLine = 0;
    std::vector<std::string> source, variables, threads, threadIds, output;
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

private:
    void execute(const std::string& command);
    void complete(ModeContext& ctx);
    void clamp(int rows);
};
} // namespace editor::statemachine
