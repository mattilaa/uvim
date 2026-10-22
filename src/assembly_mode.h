#pragma once

#include "assembly_compiler.h"
#include "mode_context.h"
#include "mode_state.h"
#include "preview_highlight.h"
#include "task_queue.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace editor::statemachine
{
struct AssemblyMode
{
    static constexpr const char* name()
    {
        return "ASM";
    }

    struct Content
    {
        assembly_compiler::Result result;
        bool active = true;
        bool compiling = true;
        bool docsLoading = false;
        size_t docsGeneration = 0;
        std::string docsTitle;
        std::string docsText;
    };

    std::string arguments;
    std::string sourceName;
    std::shared_ptr<Content> content;
    int cursor = 0;
    int offset = 0;
    int horizontal = 0;
    bool split = false;
    bool sourcePane = false;
    int sourceCursor = 0;
    int sourceOffset = 0;
    int sourceHorizontal = 0;
    bool inspect = false;
    int docsOffset = 0;

    AssemblyMode() = default;

    explicit AssemblyMode(std::string args) : arguments(std::move(args)) {}

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);
    std::optional<ModeState> handle(ModeContext& ctx,
                                    const ModeKeyEvent& event);
    void draw(Editor& editor);

private:
    PreviewHighlight highlight;
    PreviewHighlight sourceHighlight;
    bool initialSyncPending = true;
    TaskQueue::TaskId compileTask = 0;
    TaskQueue::TaskId docsTask = 0;
    void ensureVisible(const Editor& editor);
    void synchronizeSelection();
    int linkedSourceLine() const;
    void inspectLine(Editor& editor);
};
} // namespace editor::statemachine
