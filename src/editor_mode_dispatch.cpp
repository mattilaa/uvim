#include "editor.h"
#include "enablelog.h"
#include "mode_state_machine.h"
#include "terminal.h"

bool Editor::dispatchModeKey(int c)
{
    if(!modeStateMachine)
    {
        return false;
    }

    modeStateMachine->dispatch(c);
    syncModeFromStateMachine();
    ensureBufferForMode(currentMode);
    if(replayingChange && !Terminal::hasBufferedKeys())
        replayingChange = false;
    return true;
}

void Editor::syncModeFromStateMachine()
{
    if(!modeStateMachine)
    {
        return;
    }

    Mode prevMode = currentMode;
    const ModeState& state = modeStateMachine->state();
    if(std::holds_alternative<WelcomeMode>(state))
    {
        currentMode = WELCOME;
    }
    else if(std::holds_alternative<NormalMode>(state))
    {
        currentMode = NORMAL;
    }
    else if(std::holds_alternative<InsertMode>(state))
    {
        currentMode = INSERT;
    }
    else if(std::holds_alternative<ReplaceMode>(state))
    {
        currentMode = REPLACE;
    }
    else if(std::holds_alternative<VisualMode>(state))
    {
        currentMode = VISUAL;
    }
    else if(std::holds_alternative<VisualLineMode>(state))
    {
        currentMode = VISUAL_LINE;
    }
    else if(std::holds_alternative<VisualBlockMode>(state))
    {
        currentMode = VISUAL_BLOCK;
    }
    else if(std::holds_alternative<CommandMode>(state))
    {
        currentMode = COMMAND;
    }
    else if(std::holds_alternative<SearchForwardMode>(state))
    {
        currentMode = SEARCH_FORWARD;
    }
    else if(std::holds_alternative<SearchBackwardMode>(state))
    {
        currentMode = SEARCH_BACKWARD;
    }
    else if(std::holds_alternative<FileBrowserMode>(state))
    {
        currentMode = FILE_BROWSER;
    }
    else if(std::holds_alternative<FuzzyFindMode>(state))
    {
        currentMode = FUZZY_FIND;
    }
    else if(std::holds_alternative<BufferBrowserMode>(state))
    {
        currentMode = BUFFER_BROWSER;
    }
    else if(std::holds_alternative<GrepSearchMode>(state))
    {
        currentMode = GREP_SEARCH;
    }
    else if(std::holds_alternative<OperatorPendingMode>(state))
    {
        currentMode = OP_PENDING;
    }
    else if(std::holds_alternative<ReferencesMode>(state))
    {
        currentMode = REFERENCES;
    }
    else if(std::holds_alternative<LspInfoMode>(state))
    {
        currentMode = LSP_INFO;
    }
    else if(std::holds_alternative<LocListMode>(state))
    {
        currentMode = LOC_LIST;
    }
    else if(std::holds_alternative<HelpMode>(state))
    {
        currentMode = HELP;
    }
    else if(std::holds_alternative<GitShowCommitMode>(state))
    {
        currentMode = GIT_SHOW;
    }
    else if(std::holds_alternative<GitLogMode>(state))
    {
        currentMode = GIT_LOG;
    }
    else if(std::holds_alternative<GitStageMode>(state))
    {
        currentMode = GIT_STAGE;
    }
    else if(std::holds_alternative<GitCommitMode>(state))
    {
        currentMode = GIT_COMMIT;
    }
    else if(std::holds_alternative<GitFixupMode>(state))
    {
        currentMode = GIT_FIXUP;
    }
    else if(std::holds_alternative<GitPatchMode>(state))
    {
        currentMode = GIT_PATCH;
    }

    if(currentMode != prevMode)
        needsFullRedraw = true;
}

void Editor::handleKeypress(int c)
{
    if(c < 0)
        return;
    if(!locMessage.empty())
        locMessage.clear();
    LOG_DEBUG(LOG, "handleKeypress c={} ('{}') mode={}", c, (char)c,
              static_cast<int>(currentMode));

    if(dispatchModeKey(c))
    {
        return;
    }

    switch(currentMode)
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
