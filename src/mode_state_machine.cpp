#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"

namespace editor::statemachine
{
namespace
{
struct ModeStateToMode
{
    Mode operator()(const WelcomeMode&) const
    {
        return WELCOME;
    }

    Mode operator()(const NormalMode&) const
    {
        return NORMAL;
    }

    Mode operator()(const InsertMode&) const
    {
        return INSERT;
    }

    Mode operator()(const ReplaceMode&) const
    {
        return REPLACE;
    }

    Mode operator()(const VisualMode&) const
    {
        return VISUAL;
    }

    Mode operator()(const VisualLineMode&) const
    {
        return VISUAL_LINE;
    }

    Mode operator()(const VisualBlockMode&) const
    {
        return VISUAL_BLOCK;
    }

    Mode operator()(const CommandMode&) const
    {
        return COMMAND;
    }

    Mode operator()(const SearchForwardMode&) const
    {
        return SEARCH_FORWARD;
    }

    Mode operator()(const SearchBackwardMode&) const
    {
        return SEARCH_BACKWARD;
    }

#ifdef UVIM_ENABLE_BROWSER_TOOLS
    Mode operator()(const FileBrowserMode&) const
    {
        return FILE_BROWSER;
    }
#endif
#ifdef UVIM_ENABLE_SEARCH_TOOLS
    Mode operator()(const FuzzyFindMode&) const
    {
        return FUZZY_FIND;
    }
#endif

#ifdef UVIM_ENABLE_BROWSER_TOOLS
    Mode operator()(const BufferBrowserMode&) const
    {
        return BUFFER_BROWSER;
    }
#endif

#ifdef UVIM_ENABLE_SEARCH_TOOLS
    Mode operator()(const GrepSearchMode&) const
    {
        return GREP_SEARCH;
    }

    Mode operator()(const RegexSearchMode&) const
    {
        return REGEX_SEARCH;
    }
#endif

    Mode operator()(const OperatorPendingMode&) const
    {
        return OP_PENDING;
    }

#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
    Mode operator()(const ReferencesMode&) const
    {
        return REFERENCES;
    }

    Mode operator()(const LspInfoMode&) const
    {
        return LSP_INFO;
    }

    Mode operator()(const ToolInfoMode&) const
    {
        return TOOL_INFO;
    }

    Mode operator()(const LocListMode&) const
    {
        return LOC_LIST;
    }

    Mode operator()(const MlangFormatErrorsMode&) const
    {
        return MLANG_FORMAT_ERRORS;
    }

    Mode operator()(const HelpMode&) const
    {
        return HELP;
    }
#endif

#ifdef UVIM_ENABLE_GIT_TOOLS
    Mode operator()(const GitShowCommitMode&) const
    {
        return GIT_SHOW;
    }

    Mode operator()(const GitLogMode&) const
    {
        return GIT_LOG;
    }

    Mode operator()(const GitStageMode&) const
    {
        return GIT_STAGE;
    }

    Mode operator()(const GitCommitMode&) const
    {
        return GIT_COMMIT;
    }

    Mode operator()(const GitFixupMode&) const
    {
        return GIT_FIXUP;
    }

    Mode operator()(const GitPatchMode&) const
    {
        return GIT_PATCH;
    }
#endif

#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
    Mode operator()(const CommandOutputMode&) const
    {
        return COMMAND_OUTPUT;
    }

    Mode operator()(const GlyphSelectMode&) const
    {
        return GLYPH_SELECT;
    }
#endif
#ifdef UVIM_ENABLE_COLOR_TOOLS
    Mode operator()(const AnsiToolsMode&) const
    {
        return ANSI_TOOLS;
    }

    Mode operator()(const ColorPickerMode&) const
    {
        return COLOR_PICKER;
    }

    Mode operator()(const ColorSelectorMode&) const
    {
        return COLOR_SELECTOR;
    }
#endif
};
} // namespace

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
    if(text_utils::is_found(spacePos))
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

#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
    if(ctx.currentMode() == LSP_INFO)
    {
        return LspInfoMode{};
    }
    if(ctx.currentMode() == TOOL_INFO)
    {
        return ToolInfoMode{};
    }
#endif
    if(ctx.editor && ctx.editor->getModeStateMachine())
    {
#if defined(UVIM_ENABLE_GIT_TOOLS) || defined(UVIM_ENABLE_AUXILIARY_VIEWS)
        const ModeState& state = ctx.editor->getModeStateMachine()->state();
#endif
#ifdef UVIM_ENABLE_GIT_TOOLS
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
#endif
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
        if(std::holds_alternative<GlyphSelectMode>(state))
            return std::get<GlyphSelectMode>(state);
#endif
#ifdef UVIM_ENABLE_COLOR_TOOLS
        if(std::holds_alternative<AnsiToolsMode>(state))
            return std::get<AnsiToolsMode>(state);
        if(std::holds_alternative<ColorPickerMode>(state))
            return std::get<ColorPickerMode>(state);
        if(std::holds_alternative<ColorSelectorMode>(state))
            return std::get<ColorSelectorMode>(state);
#endif
    }

    Mode mode = NORMAL;
    std::string path;
    if(ctx.takeCommandRequest(mode, path))
    {
#ifdef UVIM_ENABLE_BROWSER_TOOLS
        if(mode == FILE_BROWSER)
        {
            return FileBrowserMode{path, std::string(previousFile)};
        }
#endif
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
        if(mode == LSP_INFO)
        {
            return LspInfoMode{};
        }
        if(mode == TOOL_INFO)
        {
            return ToolInfoMode{};
        }
        if(mode == LOC_LIST)
        {
            return LocListMode{};
        }
        if(mode == MLANG_FORMAT_ERRORS)
        {
            return MlangFormatErrorsMode{};
        }
        if(mode == HELP)
        {
            return HelpMode{path, std::string(previousFile)};
        }
#endif
#ifdef UVIM_ENABLE_GIT_TOOLS
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
#else
        (void)path;
#endif
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
// Note: ModeStateMachine inherits from StateMachine<ModeState, ModeContext>.
// Mode dispatch wraps raw key codes in ModeKeyEvent so mode handlers receive
// trigger data through typed event objects.
//
// The constructors are defined inline in the header using the base class.
//

void ModeStateMachine::transitionToMode(Mode mode)
{
    transitionTo(stateForMode(context(), mode));
}

Mode modeForState(const ModeState& state)
{
    return std::visit(ModeStateToMode{}, state);
}

ModeState stateForMode(ModeContext& ctx, Mode mode)
{
    switch(mode)
    {
    case WELCOME:
        return WelcomeMode{};
    case NORMAL:
        return NormalMode{};
    case INSERT:
        return InsertMode{};
    case REPLACE:
        return ReplaceMode{};
    case VISUAL:
        return VisualMode{};
    case VISUAL_LINE:
        return VisualLineMode{};
    case VISUAL_BLOCK:
        return VisualBlockMode{};
    case COMMAND:
        return CommandMode{};
    case SEARCH_FORWARD:
        return SearchForwardMode{};
    case SEARCH_BACKWARD:
        return SearchBackwardMode{};
#ifdef UVIM_ENABLE_BROWSER_TOOLS
    case FILE_BROWSER:
        return FileBrowserMode{};
#else
    case FILE_BROWSER:
        return NormalMode{};
#endif
#ifdef UVIM_ENABLE_SEARCH_TOOLS
    case FUZZY_FIND:
        return FuzzyFindMode{};
#else
    case FUZZY_FIND:
        return NormalMode{};
#endif
#ifdef UVIM_ENABLE_BROWSER_TOOLS
    case BUFFER_BROWSER:
        return BufferBrowserMode{};
#else
    case BUFFER_BROWSER:
        return NormalMode{};
#endif
#ifdef UVIM_ENABLE_SEARCH_TOOLS
    case GREP_SEARCH:
        return GrepSearchMode{};
    case REGEX_SEARCH:
        return RegexSearchMode{};
#else
    case GREP_SEARCH:
    case REGEX_SEARCH:
        return NormalMode{};
#endif
    case OP_PENDING:
        return OperatorPendingMode{ctx.pendingOperator, ctx.pendingCount};
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
    case REFERENCES:
        return ReferencesMode{};
    case LSP_INFO:
        return LspInfoMode{};
    case TOOL_INFO:
        return ToolInfoMode{};
    case LOC_LIST:
        return LocListMode{};
    case MLANG_FORMAT_ERRORS:
        return MlangFormatErrorsMode{};
    case HELP:
        return HelpMode{};
#else
    case REFERENCES:
    case LSP_INFO:
    case TOOL_INFO:
    case LOC_LIST:
    case MLANG_FORMAT_ERRORS:
    case HELP:
        return NormalMode{};
#endif
#ifdef UVIM_ENABLE_GIT_TOOLS
    case GIT_SHOW:
        return GitShowCommitMode{};
    case GIT_LOG:
        return GitLogMode{};
    case GIT_STAGE:
        return GitStageMode{};
    case GIT_COMMIT:
        return GitCommitMode{};
    case GIT_FIXUP:
        return GitFixupMode{};
    case GIT_PATCH:
        return GitPatchMode{};
#else
    case GIT_SHOW:
    case GIT_LOG:
    case GIT_STAGE:
    case GIT_COMMIT:
    case GIT_FIXUP:
    case GIT_PATCH:
        return NormalMode{};
#endif
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
    case COMMAND_OUTPUT:
        return CommandOutputMode{};
    case GLYPH_SELECT:
        return GlyphSelectMode{};
#else
    case COMMAND_OUTPUT:
    case GLYPH_SELECT:
        return NormalMode{};
#endif
#ifdef UVIM_ENABLE_COLOR_TOOLS
    case ANSI_TOOLS:
        return AnsiToolsMode{};
    case COLOR_PICKER:
        return ColorPickerMode{};
    case COLOR_SELECTOR:
        return ColorSelectorMode{};
#else
    case ANSI_TOOLS:
    case COLOR_PICKER:
    case COLOR_SELECTOR:
        return NormalMode{};
#endif
    }

    return NormalMode{};
}

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
} // namespace editor::statemachine
