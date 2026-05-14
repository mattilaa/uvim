#include "editor_mode_controller.h"
#include "editor.h"
#include "enablelog.h"
#include "mode_state_machine.h"
#include "terminal.h"

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

    Mode prevMode = editor.currentMode;
    const ModeState& state = editor.modeStateMachine->state();
    if(std::holds_alternative<WelcomeMode>(state))
    {
        editor.currentMode = WELCOME;
    }
    else if(std::holds_alternative<NormalMode>(state))
    {
        editor.currentMode = NORMAL;
    }
    else if(std::holds_alternative<InsertMode>(state))
    {
        editor.currentMode = INSERT;
    }
    else if(std::holds_alternative<ReplaceMode>(state))
    {
        editor.currentMode = REPLACE;
    }
    else if(std::holds_alternative<VisualMode>(state))
    {
        editor.currentMode = VISUAL;
    }
    else if(std::holds_alternative<VisualLineMode>(state))
    {
        editor.currentMode = VISUAL_LINE;
    }
    else if(std::holds_alternative<VisualBlockMode>(state))
    {
        editor.currentMode = VISUAL_BLOCK;
    }
    else if(std::holds_alternative<CommandMode>(state))
    {
        editor.currentMode = COMMAND;
    }
    else if(std::holds_alternative<SearchForwardMode>(state))
    {
        editor.currentMode = SEARCH_FORWARD;
    }
    else if(std::holds_alternative<SearchBackwardMode>(state))
    {
        editor.currentMode = SEARCH_BACKWARD;
    }
    else if(std::holds_alternative<FileBrowserMode>(state))
    {
        editor.currentMode = FILE_BROWSER;
    }
    else if(std::holds_alternative<FuzzyFindMode>(state))
    {
        editor.currentMode = FUZZY_FIND;
    }
    else if(std::holds_alternative<BufferBrowserMode>(state))
    {
        editor.currentMode = BUFFER_BROWSER;
    }
    else if(std::holds_alternative<GrepSearchMode>(state))
    {
        editor.currentMode = GREP_SEARCH;
    }
    else if(std::holds_alternative<OperatorPendingMode>(state))
    {
        editor.currentMode = OP_PENDING;
    }
    else if(std::holds_alternative<ReferencesMode>(state))
    {
        editor.currentMode = REFERENCES;
    }
    else if(std::holds_alternative<LspInfoMode>(state))
    {
        editor.currentMode = LSP_INFO;
    }
    else if(std::holds_alternative<LocListMode>(state))
    {
        editor.currentMode = LOC_LIST;
    }
    else if(std::holds_alternative<HelpMode>(state))
    {
        editor.currentMode = HELP;
    }
    else if(std::holds_alternative<GitShowCommitMode>(state))
    {
        editor.currentMode = GIT_SHOW;
    }
    else if(std::holds_alternative<GitLogMode>(state))
    {
        editor.currentMode = GIT_LOG;
    }
    else if(std::holds_alternative<GitStageMode>(state))
    {
        editor.currentMode = GIT_STAGE;
    }
    else if(std::holds_alternative<GitCommitMode>(state))
    {
        editor.currentMode = GIT_COMMIT;
    }
    else if(std::holds_alternative<GitFixupMode>(state))
    {
        editor.currentMode = GIT_FIXUP;
    }
    else if(std::holds_alternative<GitPatchMode>(state))
    {
        editor.currentMode = GIT_PATCH;
    }
    else if(std::holds_alternative<CommandOutputMode>(state))
    {
        editor.currentMode = COMMAND_OUTPUT;
    }

    if(editor.currentMode != prevMode)
        editor.needsFullRedraw = true;
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
