#include "editor_mode_controller.h"
#include "editor.h"
#include "enablelog.h"
#include "mode_state_machine.h"
#include "terminal.h"

#include <optional>

using namespace editor::statemachine;

namespace
{
class ModeChangeRecognizer
{
public:
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const WelcomeMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const NormalMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const InsertMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const ReplaceMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const VisualMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const VisualLineMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const VisualBlockMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const CommandMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const SearchForwardMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const SearchBackwardMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const FileBrowserMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const FuzzyFindMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const BufferBrowserMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const GrepSearchMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const RegexSearchMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const OperatorPendingMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const ReferencesMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const LspInfoMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const LocListMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const HelpMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const GitShowCommitMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const GitLogMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const GitStageMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const GitCommitMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const GitFixupMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const GitPatchMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const CommandOutputMode& state) const
    {
        return recognize(ctx, state);
    }
#ifdef UVIM_ENABLE_COLOR_TOOLS
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const ColorPickerMode& state) const
    {
        return recognize(ctx, state);
    }
    std::optional<Mode> operator()(ModeContext& ctx,
                                   const ColorSelectorMode& state) const
    {
        return recognize(ctx, state);
    }
#endif

private:
    template <typename State>
    std::optional<Mode> recognize(ModeContext& ctx, const State& state) const
    {
        Mode nextMode = modeForState(ModeState{state});
        if(nextMode == ctx.currentMode())
            return {};
        return nextMode;
    }
};
} // namespace

EditorModeController::EditorModeController(Editor& editor) : editor(editor) {}

bool EditorModeController::dispatchModeKey(int c)
{
    if(!editor.modeStateMachine)
    {
        return false;
    }

    editor.modeStateMachine->dispatch(c);
    syncModeFromStateMachine();
    editor.ensureBufferForMode(editor.currentMode);
    if(editor.replayingChange && !Terminal::hasBufferedKeys())
        editor.replayingChange = false;
    return true;
}

void EditorModeController::syncModeFromStateMachine()
{
    if(!editor.modeStateMachine)
    {
        return;
    }

    const ModeState& state = editor.modeStateMachine->state();
    ModeContext ctx = createModeContext(&editor);
    std::optional<Mode> nextMode =
        std::visit([&ctx](const auto& concreteState) -> std::optional<Mode>
                   { return ModeChangeRecognizer{}(ctx, concreteState); },
                   state);
    if(!nextMode)
    {
        return;
    }

    ctx.setCurrentMode(*nextMode);
    ctx.requestFullRedraw();
}

void EditorModeController::handleKeypress(int c)
{
    if(c < 0)
        return;
    if(!editor.locMessage.empty())
        editor.locMessage.clear();
    LOG_DEBUG(LOG, "handleKeypress c={} ('{}') mode={}", c, (char)c,
              static_cast<int>(editor.currentMode));

    if(handleEmojiPopupKey(c))
        return;

    if(dispatchModeKey(c))
    {
        return;
    }

    switch(editor.currentMode)
    {
    case NORMAL:
        handleNormalMode(c);
        break;
    case INSERT:
    case REPLACE:
        handleInsertMode(c);
        break;
    case VISUAL:
    case VISUAL_LINE:
        handleVisualMode(c);
        break;
    case VISUAL_BLOCK:
        handleVisualBlockMode(c);
        break;
    case COMMAND:
        handleCommandMode(c);
        break;
    case SEARCH_FORWARD:
    case SEARCH_BACKWARD:
        handleSearchMode(c);
        break;
    case FILE_BROWSER:
        break;
    case FUZZY_FIND:
        break;
    case BUFFER_BROWSER:
        break;
    case REGEX_SEARCH:
        break;
    case OP_PENDING:
        handleOperatorPendingMode(c);
        break;
    default:
        break;
    }
}

bool EditorModeController::handleEmojiPopupKey(int c)
{
    if(!editor.emojiPopupActive)
        return false;

    if(c == keyCode(control::ControlKey::CTRL_J) ||
       c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        editor.emojiNext();
        return true;
    }

    if(c == keyCode(control::ControlKey::CTRL_K) ||
       c == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        editor.emojiPrev();
        return true;
    }

    if(c == keyCode(control::ControlKey::ENTER))
    {
        editor.acceptEmoji();
        return true;
    }

    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(control::ControlKey::CTRL_C))
    {
        editor.cancelEmojiPopup();
        return true;
    }

    if(c == keyCode(control::ControlKey::BACKSPACE) ||
       c == keyCode(control::ControlKey::DEL) ||
       c == keyCode(control::ControlKey::CTRL_H))
    {
        if(!editor.emojiQuery.empty())
        {
            editor.emojiQuery.pop_back();
            editor.rebuildEmojiFilter();
            editor.needsFullRedraw = true;
        }
        return true;
    }

    if(c >= 32 && c < 127)
    {
        editor.emojiQuery.push_back((char)c);
        editor.rebuildEmojiFilter();
        editor.needsFullRedraw = true;
        return true;
    }

    return true;
}
