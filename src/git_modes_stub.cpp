#include "git_commit_mode.h"
#include "git_fixup_mode.h"
#include "git_log_mode.h"
#include "git_patch_mode.h"
#include "git_show_mode.h"
#include "git_stage_mode.h"

#include "editor.h"
#include "mode_state_machine.h"
#include "normal_mode.h"
#include "welcome_mode.h"

namespace editor::statemachine
{
namespace
{
std::optional<ModeState> exitMode(ModeContext& ctx)
{
    if(ctx.editor)
        ctx.editor->setStatusMessage("git tools disabled in this build");
    return ctx.hasBuffer() ? ModeState{NormalMode{}} : ModeState{WelcomeMode{}};
}
} // namespace

void GitShowCommitMode::on_enter(ModeContext& ctx)
{
    if(ctx.editor)
        ctx.editor->setStatusMessage("git tools disabled in this build");
}

void GitShowCommitMode::on_exit(ModeContext&) {}

std::optional<ModeState> GitShowCommitMode::handle(ModeContext& ctx,
                                                   const ModeKeyEvent&)
{
    return exitMode(ctx);
}

void GitShowCommitMode::draw(Editor& editor) const
{
    editor.setStatusMessage("git tools disabled in this build");
}

void GitLogMode::on_enter(ModeContext& ctx)
{
    if(ctx.editor)
        ctx.editor->setStatusMessage("git tools disabled in this build");
}

void GitLogMode::on_exit(ModeContext&) {}

std::optional<ModeState> GitLogMode::handle(ModeContext& ctx,
                                            const ModeKeyEvent&)
{
    return exitMode(ctx);
}

void GitLogMode::draw(Editor& editor) const
{
    editor.setStatusMessage("git tools disabled in this build");
}

void GitLogMode::rebuildFilter(Editor&) {}

void GitLogMode::ensurePrettyPreview(Editor&) {}

const char* GitLogMode::graphPrettyFormatArg()
{
    return "--pretty=format:%h%x09%ad%x09%an%x09%D%x09%s";
}

std::optional<GitLogMode::Entry> GitLogMode::parseGraphEntry(std::string_view)
{
    return std::nullopt;
}

void GitLogMode::applyGraphConnector(Entry&, std::string_view) {}

GitStageMode::GitStageMode(std::vector<Node> items, std::string root,
                           std::string dir)
    : nodes(std::move(items)), repoRoot(std::move(root)),
      repoDir(std::move(dir))
{
}

void GitStageMode::on_enter(ModeContext& ctx)
{
    if(ctx.editor)
        ctx.editor->setStatusMessage("git tools disabled in this build");
}

void GitStageMode::on_exit(ModeContext&) {}

std::optional<ModeState> GitStageMode::handle(ModeContext& ctx,
                                              const ModeKeyEvent&)
{
    return exitMode(ctx);
}

void GitStageMode::draw(Editor& editor) const
{
    editor.setStatusMessage("git tools disabled in this build");
}

void GitCommitMode::on_enter(ModeContext& ctx)
{
    if(ctx.editor)
        ctx.editor->setStatusMessage("git tools disabled in this build");
}

void GitCommitMode::on_exit(ModeContext&) {}

std::optional<ModeState> GitCommitMode::handle(ModeContext& ctx,
                                               const ModeKeyEvent&)
{
    return exitMode(ctx);
}

void GitCommitMode::draw(Editor& editor) const
{
    editor.setStatusMessage("git tools disabled in this build");
}

void GitCommitMode::refreshStaged() {}

void GitFixupMode::on_enter(ModeContext& ctx)
{
    if(ctx.editor)
        ctx.editor->setStatusMessage("git tools disabled in this build");
}

void GitFixupMode::on_exit(ModeContext&) {}

std::optional<ModeState> GitFixupMode::handle(ModeContext& ctx,
                                              const ModeKeyEvent&)
{
    return exitMode(ctx);
}

void GitFixupMode::draw(Editor& editor) const
{
    editor.setStatusMessage("git tools disabled in this build");
}

void GitPatchMode::on_enter(ModeContext& ctx)
{
    if(ctx.editor)
        ctx.editor->setStatusMessage("git tools disabled in this build");
}

void GitPatchMode::on_exit(ModeContext&) {}

std::optional<ModeState> GitPatchMode::handle(ModeContext& ctx,
                                              const ModeKeyEvent&)
{
    return exitMode(ctx);
}

void GitPatchMode::draw(Editor& editor) const
{
    editor.setStatusMessage("git tools disabled in this build");
}
} // namespace editor::statemachine
