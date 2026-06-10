#pragma once

#ifndef UVIM_MINIMAL
#include "buffer_browser_mode.h"
#endif
#ifdef UVIM_ENABLE_COLOR_TOOLS
#include "ansi_tools_mode.h"
#include "color_picker_mode.h"
#include "color_selector_mode.h"
#endif
#include "command_mode.h"
#ifndef UVIM_MINIMAL
#include "command_output_mode.h"
#include "file_browser_mode.h"
#endif
#ifdef UVIM_ENABLE_SEARCH_TOOLS
#include "fuzzy_find_mode.h"
#endif
#ifndef UVIM_MINIMAL
#include "git_commit_mode.h"
#include "git_fixup_mode.h"
#include "git_log_mode.h"
#include "git_patch_mode.h"
#include "git_show_mode.h"
#include "git_stage_mode.h"
#include "glyph_select_mode.h"
#endif
#ifdef UVIM_ENABLE_SEARCH_TOOLS
#include "grep_search_mode.h"
#endif
#ifndef UVIM_MINIMAL
#include "help_mode.h"
#endif
#include "insert_mode.h"
#ifndef UVIM_MINIMAL
#include "loc_mode.h"
#include "lsp_info_mode.h"
#endif
#include "mode_commands.h"
#include "mode_state.h"
#include "normal_mode.h"
#include "operator_pending_mode.h"
#ifndef UVIM_MINIMAL
#include "references_mode.h"
#endif
#ifdef UVIM_ENABLE_SEARCH_TOOLS
#include "regex_search_mode.h"
#endif
#include "replace_mode.h"
#include "search_backward_mode.h"
#include "search_forward_mode.h"
#include "state_machine.h"
#include "visual_block_mode.h"
#include "visual_line_mode.h"
#include "visual_mode.h"
#include "welcome_mode.h"

namespace editor::statemachine
{
using ModeStateMachineBase = StateMachine<ModeState, ModeContext>;

class ModeStateMachine : public ModeStateMachineBase
{
public:
    explicit ModeStateMachine(ModeContext ctx)
        : ModeStateMachineBase(std::move(ctx), NormalMode{})
    {
    }

    template <typename InitialState>
    ModeStateMachine(ModeContext ctx, InitialState&& initial)
        : ModeStateMachineBase(std::move(ctx),
                               std::forward<InitialState>(initial))
    {
    }

    void dispatch(char key)
    {
        dispatch(static_cast<int>(static_cast<unsigned char>(key)));
    }

    void dispatch(int key)
    {
        ModeStateMachineBase::dispatch(ModeKeyEvent{key});
    }

    template <typename Event>
    void dispatch(const Event& event)
    {
        ModeStateMachineBase::dispatch(event);
    }

    void transitionToMode(Mode mode);
};

ModeContext createModeContext(Editor* editor);
Mode modeForState(const ModeState& state);
ModeState stateForMode(ModeContext& ctx, Mode mode);
} // namespace editor::statemachine
