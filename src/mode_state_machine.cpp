#include "mode_state_machine.h"
#include "editor.h"
#include "terminal.h"
#include "text_utils.h"

ParsedCommand parseCommandLine(std::string_view commandLine)
{
    while(!commandLine.empty() && text_utils::is_space(commandLine.front()))
    {
        commandLine.remove_prefix(1);
    }

    if(commandLine.empty())
    {
        return {};
    }

    ParsedCommand result;
    size_t spacePos = commandLine.find(' ');
    if(spacePos != std::string_view::npos)
    {
        result.cmd = std::string(commandLine.substr(0, spacePos));
        std::string_view argsView = commandLine.substr(spacePos + 1);
        while(!argsView.empty() && text_utils::is_space(argsView.front()))
        {
            argsView.remove_prefix(1);
        }
        result.args = std::string(argsView);
    }
    else
    {
        result.cmd = std::string(commandLine);
    }

    return result;
}

std::optional<ModeState>
dispatchCommandLine(ModeContext& ctx, std::string_view commandLine,
                    const ModeCommandCallback& modeHandler,
                    const CommandFallback& fallbackHandler)
{
    ParsedCommand command = parseCommandLine(commandLine);
    if(command.cmd.empty())
    {
        return std::nullopt;
    }

    std::optional<ModeState> nextState;
    if(modeHandler && modeHandler(ctx, command, nextState))
    {
        return nextState;
    }

    if(fallbackHandler)
    {
        return fallbackHandler(ctx, commandLine);
    }

    return std::nullopt;
}

std::optional<ModeState> dispatchEditorCommand(ModeContext& ctx,
                                               std::string_view commandLine,
                                               std::string_view previousFile,
                                               bool returnToNormalIfBuffer)
{
    ctx.executeCommand(commandLine);

    if(ctx.currentMode() == LSP_INFO)
    {
        return LspInfoMode{};
    }
    if(ctx.editor && ctx.editor->getModeStateMachine())
    {
        const ModeState& state = ctx.editor->getModeStateMachine()->state();
        if(std::holds_alternative<GitLogMode>(state))
            return std::get<GitLogMode>(state);
        if(std::holds_alternative<GitShowCommitMode>(state))
            return std::get<GitShowCommitMode>(state);
        if(std::holds_alternative<GitStageMode>(state))
            return std::get<GitStageMode>(state);
        if(std::holds_alternative<GitCommitMode>(state))
            return std::get<GitCommitMode>(state);
        if(std::holds_alternative<GitFixupMode>(state))
            return std::get<GitFixupMode>(state);
        if(std::holds_alternative<GitPatchMode>(state))
            return std::get<GitPatchMode>(state);
    }

    Mode mode = NORMAL;
    std::string path;
    if(ctx.takeCommandRequest(mode, path))
    {
        if(mode == FILE_BROWSER)
        {
            return FileBrowserMode{path, std::string(previousFile)};
        }
        if(mode == LSP_INFO)
        {
            return LspInfoMode{};
        }
        if(mode == LOC_LIST)
        {
            return LocListMode{};
        }
        if(mode == HELP)
        {
            return HelpMode{path, std::string(previousFile)};
        }
        if(mode == GIT_STAGE)
        {
            GitStageMode stage;
            stage.returnMode = ctx.editor->commandRequestedReturnMode;
            if(stage.returnMode.has_value() &&
               stage.returnMode.value() == FILE_BROWSER)
            {
                stage.returnBrowseCursor =
                    ctx.editor->commandRequestedBrowseCursor;
                stage.returnBrowseOffset =
                    ctx.editor->commandRequestedBrowseOffset;
                stage.returnBrowseDirectory =
                    ctx.editor->commandRequestedBrowseDirectory;
            }
            ctx.editor->commandRequestedReturnMode.reset();
            ctx.editor->commandRequestedBrowseCursor = 0;
            ctx.editor->commandRequestedBrowseOffset = 0;
            ctx.editor->commandRequestedBrowseDirectory.clear();
            return stage;
        }
        if(mode == GIT_COMMIT)
        {
            return GitCommitMode{};
        }
    }

    if(returnToNormalIfBuffer && ctx.hasBuffer())
    {
        return NormalMode{};
    }

    return std::nullopt;
}

// ============================================================================
// ModeStateMachine Implementation
// ============================================================================
//
// Note: ModeStateMachine now inherits from StateMachine<ModeState, ModeContext,
// std::string_view>, so all dispatch/transition logic is provided by the base
// class template. Only helpers like createModeContext are implemented here.
//
// The constructors are defined inline in the header using the base class.
//

// ============================================================================
// Helper: Create context from Editor
// ============================================================================

ModeContext createModeContext(Editor* editor)
{
    return ModeContext{
        .editor = editor,
        .commandBuffer = editor->commandBuffer,
        .repeatCount = editor->repeatCount,
        .pendingOperator = editor->pendingOperator,
        .pendingAwaitingObject = editor->pendingAwaitingObject,
        .pendingObjectType = editor->pendingObjectType,
        .pendingCount = editor->pendingCount,
        .searchQuery = editor->searchQuery,
        .searchForward = editor->searchForward,
        .savedCursorX = editor->savedCursorX,
        .savedCursorY = editor->savedCursorY,
    };
}

ModeState defaultExitMode(const Editor* editor)
{
    if(!editor || !editor->hasBuffer())
        return WelcomeMode{};
    return NormalMode{};
}
