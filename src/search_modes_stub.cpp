#include "fuzzy_find_mode.h"
#include "grep_search_mode.h"
#include "regex_search_mode.h"

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
        ctx.editor->setStatusMessage("search tools disabled in this build");
    return ctx.hasBuffer() ? ModeState{NormalMode{}} : ModeState{WelcomeMode{}};
}
} // namespace

void FuzzyFindMode::on_enter(ModeContext& ctx)
{
    if(ctx.editor)
        ctx.editor->setStatusMessage("search tools disabled in this build");
}

void FuzzyFindMode::on_exit(ModeContext&) {}

std::optional<ModeState> FuzzyFindMode::handle(ModeContext& ctx,
                                               const ModeKeyEvent&)
{
    return exitMode(ctx);
}

void FuzzyFindMode::draw(Editor& editor) const
{
    editor.setStatusMessage("search tools disabled in this build");
}

void FuzzyFindMode::refreshFileIndex(Editor&) {}

void GrepSearchMode::on_enter(ModeContext& ctx)
{
    if(ctx.editor)
        ctx.editor->setStatusMessage("search tools disabled in this build");
}

void GrepSearchMode::on_exit(ModeContext&) {}

std::optional<ModeState> GrepSearchMode::handle(ModeContext& ctx,
                                                const ModeKeyEvent&)
{
    return exitMode(ctx);
}

void GrepSearchMode::draw(Editor& editor) const
{
    editor.setStatusMessage("search tools disabled in this build");
}

void GrepSearchMode::refreshFileIndex(Editor&) {}

void RegexSearchMode::on_enter(ModeContext& ctx)
{
    if(ctx.editor)
        ctx.editor->setStatusMessage("search tools disabled in this build");
}

void RegexSearchMode::on_exit(ModeContext&) {}

std::optional<ModeState> RegexSearchMode::handle(ModeContext& ctx,
                                                 const ModeKeyEvent&)
{
    return exitMode(ctx);
}

void RegexSearchMode::draw(Editor& editor) const
{
    editor.setStatusMessage("search tools disabled in this build");
}

void RegexSearchMode::refreshFileIndex(Editor&) {}
} // namespace editor::statemachine
