#include "editor.h"
#include "editor_utils.h"
#include "mode_state_machine.h"
#include "substitute_command.h"
#include "terminal.h"
#include "text_utils.h"

// ============================================================================
// CommandMode Implementation
// ============================================================================

namespace editor::statemachine
{
bool CommandMode::updateAssemblyFlagPopup(ModeContext& ctx)
{
#ifdef UVIM_ENABLE_ASM_VIEW
    const std::string_view command = ctx.commandBuffer;
    assembly_compiler::Request request;
    std::string error;
    if(command.rfind(":asm ", 0) == 0 && command.ends_with(" --") &&
       assembly_compiler::parse(
           command.substr(5, command.size() - 7),
           request,
           error))
    {
        assemblyFlagPopup = true;
        auto& editor = *ctx.editor;
        editor.commandPopupActive = true;
        editor.commandPopupAll =
            {"-O2",
             "-O0",
             "-O1",
             "-O3",
             "-Os",
             "-Og",
             "-g",
             "-fno-inline",
             "-fverbose-asm",
             "-masm=intel",
             "-masm=att",
             "-march=native",
             "-Wall",
             "-Wextra",
             "-std=c++20",
             "-std=c++23",
             "-std=c11",
             "-I ",
             "-D ",
             "--compiler clang",
             "--compiler gcc",
             "--split",
             "--raw"};
        const std::string sourcePath =
            request.path == "." && editor.currentBuffer
                ? editor.currentBuffer->filename
                : request.path;
        if(assembly_compiler::isMlangSource(sourcePath))
            editor.commandPopupAll =
                {"-O2",
                 "-O0",
                 "-O1",
                 "-O3",
                 "-Os",
                 "-Oz",
                 "-Og",
                 "-g",
                 "--target-arch x64",
                 "--target-arch x86",
                 "--target-arch aarch64",
                 "-Wno-unwrap",
                 "-Wno-colon-if",
                 "-Wno-colon-while",
                 "--compiler mlang",
                 "--split",
                 "--raw"};
        editor.commandPopupFiltered.clear();
        for(int i = 0; i < static_cast<int>(editor.commandPopupAll.size()); ++i)
            editor.commandPopupFiltered.push_back(i);
        editor.commandPopupCursor = editor.commandPopupOffset = 0;
        editor.commandPopupQuery.clear();
        ctx.setStatusMessage(
            "Assembly flags: Enter/Tab inserts; Esc cancels command");
        ctx.requestFullRedraw();
        return true;
    }
    if(assemblyFlagPopup)
    {
        ctx.cancelCommandPopup();
        ctx.setStatusMessage("");
    }
    assemblyFlagPopup = false;
#else
    (void)ctx;
#endif
    return false;
}

void CommandMode::on_enter(ModeContext& ctx)
{
    ctx.cancelCompletion();
#ifdef UVIM_ENABLE_ASM_VIEW
    assemblySourceRange.reset();
    if(ctx.editor && ctx.editor->pendingVisualColorRange)
    {
        const auto& range = *ctx.editor->pendingVisualColorRange;
        assemblySourceRange = std::pair{
            std::min(range.startY, range.endY) + 1,
            std::max(range.startY, range.endY) + 1};
    }
#endif
    ctx.commandBuffer = ":" + initialInput;
    completions.clear();
    completionIndex = -1;
    originalInput.clear();
    locCompletion = false;
    locCommand.clear();
    ctx.startCommandPopup();
#ifdef UVIM_ENABLE_DEBUGGER
    if(processPicker.update(ctx, ctx.commandBuffer))
        return;
#endif
    if(!updateAssemblyFlagPopup(ctx))
        ctx.updateCommandPopup(initialInput);

    ctx.requestFullRedraw();
}

void CommandMode::on_exit(ModeContext& ctx)
{
#ifdef UVIM_ENABLE_DEBUGGER
    if(processPicker.active)
        processPicker.close(ctx);
#endif
    if(ctx.editor)
        ctx.editor->pendingVisualColorRange.reset();
    ctx.commandBuffer.clear();
    completions.clear();
    completionIndex = -1;
    locCompletion = false;
    locCommand.clear();
    ctx.cancelCommandPopup();
    if(ctx.isCommandHistorySearchActive())
    {
        ctx.cancelCommandHistorySearch();
    }
}

std::optional<ModeState> CommandMode::handle(
    ModeContext& ctx,
    const ModeKeyEvent& event)
{
    const int key = event.key;
    int c = keyCode(key);
    auto updatePopup = [&]()
    {
#ifdef UVIM_ENABLE_DEBUGGER
        if(processPicker.update(ctx, ctx.commandBuffer))
            return;
#endif
        if(updateAssemblyFlagPopup(ctx))
            return;
        auto isLineJumpQuery = [&](std::string_view q) -> bool
        {
            if(q.empty())
                return false;
            size_t i = 0;
            while(i < q.size() &&
                  (q[i] == keyCode(control::ControlKey::SPACE) || q[i] == '\t'))
            {
                ++i;
            }
            if(i >= q.size())
                return false;
            if(q[i] == '+' || q[i] == '-')
                ++i;
            size_t digitsStart = i;
            while(i < q.size() && q[i] >= '0' && q[i] <= '9')
                ++i;
            if(i == digitsStart)
                return false;
            while(i < q.size() &&
                  (q[i] == keyCode(control::ControlKey::SPACE) || q[i] == '\t'))
            {
                ++i;
            }
            return i == q.size();
        };

        if(ctx.commandBuffer.length() <= 1)
        {
            if(!ctx.isCommandPopupActive())
                ctx.startCommandPopup();
            ctx.updateCommandPopup("");
            return;
        }

        std::string query = ctx.commandBuffer.substr(1);
        if(isLineJumpQuery(query))
        {
            ctx.cancelCommandPopup();
            return;
        }
        bool isSetQuery = query.rfind("set", 0) == 0;
        bool isHelpQuery = query == "help" || query == "h" ||
                           query.rfind("help ", 0) == 0 ||
                           query.rfind("h ", 0) == 0;
        bool isGitQuery = query == "git" || query.rfind("git ", 0) == 0;
        if(text_utils::contains(query, keyChar(control::ControlKey::SPACE)) &&
           !isSetQuery && !isHelpQuery && !isGitQuery &&
           !editor::helper::isRegexQuery(query))
        {
            ctx.cancelCommandPopup();
            return;
        }

        if(!ctx.isCommandPopupActive())
            ctx.startCommandPopup();
        ctx.updateCommandPopup(query);
    };

#ifdef UVIM_ENABLE_DEBUGGER
    if(processPicker.handle(ctx, c, ctx.commandBuffer) ==
       DebuggerProcessPicker::Action::Handled)
        return std::nullopt;
#endif

    if(ctx.isCommandHistorySearchActive())
    {
        if(c == keyCode(control::ControlKey::ESC))
        {
            ctx.editor->noteDoubleEscStatusClear();
            std::string restored = ctx.cancelCommandHistorySearch();
            ctx.commandBuffer = ":" + restored;
            updatePopup();
            return std::nullopt;
        }

        if(c == keyCode(control::ControlKey::ENTER))
        {
            std::string selected = ctx.acceptCommandHistorySearch();
            ctx.commandBuffer = ":" + selected;
            updatePopup();
            return std::nullopt;
        }

        if(c == keyCode(control::ControlKey::CTRL_J) ||
           c == keyCode(navigation::NavigationKey::ARROW_DOWN))
        {
            ctx.moveCommandHistorySearchCursor(1);
            return std::nullopt;
        }

        if(c == keyCode(control::ControlKey::CTRL_K) ||
           c == keyCode(navigation::NavigationKey::ARROW_UP))
        {
            ctx.moveCommandHistorySearchCursor(-1);
            return std::nullopt;
        }

        if(c == keyCode(control::ControlKey::BACKSPACE) || c == 127 ||
           c == keyCode(control::ControlKey::CTRL_H))
        {
            std::string query(ctx.commandHistorySearchQuery());
            if(!query.empty())
            {
                query.pop_back();
                ctx.updateCommandHistorySearchQuery(query);
                ctx.commandBuffer = ":" + query;
            }
            return std::nullopt;
        }

        if(c == keyCode(control::ControlKey::CTRL_U))
        {
            ctx.updateCommandHistorySearchQuery("");
            ctx.commandBuffer = ":";
            return std::nullopt;
        }

        if(c >= 32 && c < 127)
        {
            std::string query(ctx.commandHistorySearchQuery());
            query += static_cast<char>(c);
            ctx.updateCommandHistorySearchQuery(query);
            ctx.commandBuffer = ":" + query;
            return std::nullopt;
        }

        return std::nullopt;
    }

    // Escape -> cancel and return to normal mode
    if(c == keyCode(control::ControlKey::ESC))
    {
        ctx.editor->noteDoubleEscStatusClear();
        ctx.setStatusMessage("");
        ctx.cancelCommandPopup();
        return ctx.hasBuffer() ? ModeState{NormalMode{}}
                               : ModeState{WelcomeMode{}};
    }

#ifdef UVIM_ENABLE_ASM_VIEW
    // Selecting a compiler flag only edits the command. Compilation requires
    // a second Enter after the picker closes; Escape above cancels everything.
    if(assemblyFlagPopup)
    {
        if(c == keyCode(control::ControlKey::CTRL_J) ||
           c == keyCode(navigation::NavigationKey::ARROW_DOWN))
        {
            ctx.moveCommandPopupCursor(1);
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::CTRL_K) ||
           c == keyCode(navigation::NavigationKey::ARROW_UP) ||
           c == keyCode(control::ControlKey::SHIFT_TAB))
        {
            ctx.moveCommandPopupCursor(-1);
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::ENTER) ||
           c == keyCode(control::ControlKey::TAB))
        {
            if(auto selected = ctx.commandPopupSelection())
            {
                ctx.commandBuffer.resize(ctx.commandBuffer.size() - 2);
                ctx.commandBuffer += *selected;
                if(ctx.commandBuffer.back() != ' ')
                    ctx.commandBuffer += ' ';
            }
            assemblyFlagPopup = false;
            completions.clear();
            completionIndex = -1;
            ctx.cancelCommandPopup();
            ctx.setStatusMessage(
                "Enter: compile | --: more flags | Esc: cancel");
            ctx.requestFullRedraw();
            return std::nullopt;
        }
    }

#endif

    // Enter -> execute command
    if(c == keyCode(control::ControlKey::ENTER))
    {
        if(ctx.isCommandPopupActive())
        {
            if(auto selection = ctx.commandPopupSelection())
            {
                std::string_view query =
                    std::string_view(ctx.commandBuffer).substr(1);
                bool hasSpace = text_utils::contains(
                    query,
                    keyChar(control::ControlKey::SPACE));
                auto starts_with_ci = [&](std::string_view text,
                                          std::string_view prefix) -> bool
                {
                    if(prefix.size() > text.size())
                        return false;
                    auto to_lower_ascii = [](unsigned char ch) -> unsigned char
                    {
                        if(ch >= keyCode(typed::TypedKey::KEY_CAP_A) &&
                           ch <= keyCode(typed::TypedKey::KEY_CAP_Z))
                        {
                            ch = static_cast<unsigned char>(
                                ch - keyCode(typed::TypedKey::KEY_CAP_A) +
                                keyCode(typed::TypedKey::KEY_A));
                        }
                        return ch;
                    };
                    for(size_t i = 0; i < prefix.size(); ++i)
                    {
                        unsigned char a =
                            to_lower_ascii(static_cast<unsigned char>(text[i]));
                        unsigned char b = to_lower_ascii(
                            static_cast<unsigned char>(prefix[i]));
                        if(a != b)
                            return false;
                    }
                    return true;
                };

                bool shouldReplace = query.empty();
                if(!hasSpace && !shouldReplace)
                {
                    shouldReplace = !text_utils::contains(
                                        *selection,
                                        keyChar(control::ControlKey::SPACE)) &&
                                    starts_with_ci(*selection, query);
                }
                else if(
                    hasSpace && text_utils::contains(
                                    *selection,
                                    keyChar(control::ControlKey::SPACE)))
                {
                    shouldReplace = true;
                }

                if(shouldReplace && !ctx.isExactCommand(query))
                    ctx.commandBuffer = ":" + *selection;
            }
        }

        if(ctx.commandBuffer.length() > 1)
        {
            ctx.cancelCommandPopup();
            std::string_view cmd(ctx.commandBuffer);
            cmd.remove_prefix(
                1); // Remove leading keyCode(command::CommandKey::KEY_COLON)
            if(ctx.editor && cmd != "colorselect" && cmd != "colorpicker")
                ctx.editor->takeVisualColorRange();
            ctx.executeCommand(cmd);
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
            if(ctx.currentMode() == LSP_INFO)
            {
                return LspInfoMode{};
            }
            if(ctx.currentMode() == TOOL_INFO)
            {
                return ToolInfoMode{};
            }
            if(ctx.currentMode() == TASK_LIST)
            {
                return TaskListMode{};
            }
#endif
            if(ctx.editor && ctx.editor->getModeStateMachine())
            {
#if defined(UVIM_ENABLE_GIT_TOOLS) || defined(UVIM_ENABLE_AUXILIARY_VIEWS) ||  \
    defined(UVIM_ENABLE_COLOR_TOOLS)
                const ModeState& state =
                    ctx.editor->getModeStateMachine()->state();
#endif
#ifdef UVIM_ENABLE_GIT_TOOLS
                if(std::holds_alternative<GitLogMode>(state))
                    return std::get<GitLogMode>(state);
                if(std::holds_alternative<GitShowCommitMode>(state))
                    return std::get<GitShowCommitMode>(state);
                if(std::holds_alternative<GitStageMode>(state))
                    return std::get<GitStageMode>(state);
                if(std::holds_alternative<GitStatusMode>(state))
                    return std::get<GitStatusMode>(state);
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
#ifdef UVIM_ENABLE_DEBUGGER
                if(mode == DEBUGGER)
                    return DebuggerMode{
                        path.substr(2),
                        path.rfind("a ", 0) == 0};
#endif
#ifdef UVIM_ENABLE_ASM_VIEW
                if(mode == ASSEMBLY)
                    return AssemblyMode{path, assemblySourceRange};
#endif
#ifdef UVIM_ENABLE_BROWSER_TOOLS
                if(mode == FUNCTION_BROWSER)
                    return FunctionBrowserMode{};
#endif
#ifdef UVIM_ENABLE_BROWSER_TOOLS
                if(mode == FILE_BROWSER)
                {
                    std::string prev;
                    if(ctx.hasCurrentBuffer() && ctx.hasFilename())
                    {
                        prev = std::string(ctx.currentFilename());
                    }
                    return FileBrowserMode{path, prev};
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
                if(mode == TASK_LIST)
                {
                    return TaskListMode{};
                }
                if(mode == COMMAND_PALETTE)
                {
                    return CommandPaletteMode{};
                }
                if(mode == LOC_LIST)
                {
                    return LocListMode{};
                }
                if(mode == HELP)
                {
                    std::string prev;
                    if(ctx.hasCurrentBuffer() && ctx.hasFilename())
                    {
                        prev = std::string(ctx.currentFilename());
                    }
                    return HelpMode{path, prev}; // path contains the topic
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
#endif
            }
        }
        return ctx.hasBuffer() ? ModeState{NormalMode{}}
                               : ModeState{WelcomeMode{}};
    }

    // Backspace
    if(c == keyCode(control::ControlKey::BACKSPACE) || c == 127 ||
       c == keyCode(control::ControlKey::CTRL_H))
    {
        if(ctx.commandBuffer.length() > 1)
        {
            ctx.commandBuffer.pop_back();
            // Reset completions when text changes
            completions.clear();
            completionIndex = -1;
            locCompletion = false;
            locCommand.clear();
            updatePopup();
        }
        else
        {
            ctx.cancelCommandPopup();
            // Backspace on empty command line returns to normal
            return ctx.hasBuffer() ? ModeState{NormalMode{}}
                                   : ModeState{WelcomeMode{}};
        }
        return std::nullopt;
    }

    // Ctrl+F - fuzzy search command history
    if(c == keyCode(control::ControlKey::CTRL_F))
    {
        std::string seed;
        if(ctx.commandBuffer.length() > 1)
        {
            seed = ctx.commandBuffer.substr(1);
        }
        ctx.startCommandHistorySearch(seed);
        ctx.cancelCommandPopup();
        completions.clear();
        completionIndex = -1;
        locCompletion = false;
        locCommand.clear();
        return std::nullopt;
    }

    // Tab completion
    if(c == keyCode(control::ControlKey::TAB))
    {
        // While the command name is typed, Tab takes the popup's highlighted
        // item, fuzzy matches included (":lcto" -> ":loctotal"), also after
        // picking it with Ctrl-J/K, and the prompt continues from there.
        // Argument completion (paths, ...) starts after a space.
        const std::string input = ctx.commandBuffer.substr(1);
        if(input.starts_with("git rebase --") &&
           ctx.isCommandPopupActive())
        {
            if(auto selection = ctx.commandPopupSelection())
            {
                ctx.commandBuffer = ":" + *selection;
                completions.clear();
                completionIndex = -1;
                updatePopup();
                return std::nullopt;
            }
        }
        if(ctx.isCommandPopupActive() &&
           (!text_utils::contains(input, keyChar(control::ControlKey::SPACE)) ||
            editor::helper::isRegexQuery(input)))
        {
            if(auto selection = ctx.commandPopupSelection())
            {
                ctx.commandBuffer = ":" + *selection;
                completions.clear();
                completionIndex = -1;
                originalInput.clear();
                updatePopup();
                return std::nullopt;
            }
        }

        bool isLocTotal = ctx.commandBuffer.rfind(":loctotal", 0) == 0;
        bool isLoc = ctx.commandBuffer.rfind(":loc", 0) == 0 && !isLocTotal;
        if(isLocTotal || isLoc)
        {
            std::string locInput = ctx.commandBuffer.substr(1);
            size_t spacePos =
                ctx.commandBuffer.find(keyChar(control::ControlKey::SPACE));
            std::string_view pathPart;
            if(text_utils::is_found(spacePos))
                pathPart =
                    std::string_view(ctx.commandBuffer).substr(spacePos + 1);
            while(!pathPart.empty() &&
                  (pathPart.front() == keyCode(control::ControlKey::SPACE) ||
                   pathPart.front() == '\t'))
            {
                pathPart.remove_prefix(1);
            }

            if(completions.empty() || originalInput != locInput)
            {
                completions = ctx.getLocPathCompletions(pathPart);
                if(completions.empty())
                    completions = ctx.getPathCompletionsRecursive(pathPart);
                completionIndex = -1;
                originalInput = locInput;
            }

            if(!completions.empty())
            {
                completionIndex++;
                if(completionIndex >= (int)completions.size())
                    completionIndex = 0;
                if(isLocTotal)
                    locCommand = "loctotal";
                else
                    locCommand = ctx.commandBuffer.rfind(":loc!", 0) == 0
                                     ? "loc!"
                                     : "loc";
                ctx.commandBuffer =
                    ":" + locCommand + " " + completions[completionIndex];
            }
            updatePopup();
            return std::nullopt;
        }

        handleTabCompletion(ctx);
        updatePopup();
        return std::nullopt;
    }

    // Shift+Tab (reverse completion)
    if(c == keyCode(control::ControlKey::SHIFT_TAB))
    {
        bool isLocTotal = ctx.commandBuffer.rfind(":loctotal", 0) == 0;
        bool isLoc = ctx.commandBuffer.rfind(":loc", 0) == 0 && !isLocTotal;
        if(isLocTotal || isLoc)
        {
            std::string input = ctx.commandBuffer.substr(1);
            size_t spacePos =
                ctx.commandBuffer.find(keyChar(control::ControlKey::SPACE));
            std::string_view pathPart;
            if(text_utils::is_found(spacePos))
                pathPart =
                    std::string_view(ctx.commandBuffer).substr(spacePos + 1);
            while(!pathPart.empty() &&
                  (pathPart.front() == keyCode(control::ControlKey::SPACE) ||
                   pathPart.front() == '\t'))
            {
                pathPart.remove_prefix(1);
            }

            if(completions.empty() || originalInput != input)
            {
                completions = ctx.getLocPathCompletions(pathPart);
                if(completions.empty())
                    completions = ctx.getPathCompletionsRecursive(pathPart);
                completionIndex = -1;
                originalInput = input;
            }

            if(!completions.empty())
            {
                completionIndex--;
                if(completionIndex < 0)
                    completionIndex = (int)completions.size() - 1;
                if(isLocTotal)
                    locCommand = "loctotal";
                else
                    locCommand = ctx.commandBuffer.rfind(":loc!", 0) == 0
                                     ? "loc!"
                                     : "loc";
                ctx.commandBuffer =
                    ":" + locCommand + " " + completions[completionIndex];
            }
            updatePopup();
            return std::nullopt;
        }

        handleReverseTabCompletion(ctx);
        updatePopup();
        return std::nullopt;
    }

    // Ctrl+W - delete word backward
    if(c == keyCode(control::ControlKey::CTRL_W))
    {
        deleteWordBackward(ctx);
        completions.clear();
        completionIndex = -1;
        locCompletion = false;
        locCommand.clear();
        updatePopup();
        return std::nullopt;
    }

    // Ctrl+U - delete to start of line
    if(c == keyCode(control::ControlKey::CTRL_U))
    {
        ctx.commandBuffer = ":";
        completions.clear();
        completionIndex = -1;
        locCompletion = false;
        locCommand.clear();
        updatePopup();
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::CTRL_K))
    {
        if(ctx.isCommandPopupActive())
        {
            ctx.moveCommandPopupCursor(-1);
            if(auto selection = ctx.commandPopupSelection())
            {
                if(!text_utils::contains(
                       ctx.commandBuffer,
                       keyChar(control::ControlKey::SPACE)) ||
                   text_utils::contains(
                       *selection,
                       keyChar(control::ControlKey::SPACE)))
                {
                    ctx.commandBuffer = ":" + *selection;
                }
            }
            return std::nullopt;
        }
    }

    if(c == keyCode(control::ControlKey::CTRL_J))
    {
        if(ctx.isCommandPopupActive())
        {
            ctx.moveCommandPopupCursor(1);
            if(auto selection = ctx.commandPopupSelection())
            {
                if(!text_utils::contains(
                       ctx.commandBuffer,
                       keyChar(control::ControlKey::SPACE)) ||
                   text_utils::contains(
                       *selection,
                       keyChar(control::ControlKey::SPACE)))
                {
                    ctx.commandBuffer = ":" + *selection;
                }
            }
            return std::nullopt;
        }
    }

    // Arrow keys for command history
    if(c == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        if(auto cmd = ctx.commandHistoryUp())
        {
            ctx.commandBuffer = ":" + *cmd;
            completions.clear();
            completionIndex = -1;
            locCompletion = false;
            locCommand.clear();
            updatePopup();
        }
        return std::nullopt;
    }
    if(c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        if(auto cmd = ctx.commandHistoryDown())
        {
            ctx.commandBuffer = ":" + *cmd;
            completions.clear();
            completionIndex = -1;
            locCompletion = false;
            locCommand.clear();
            updatePopup();
        }
        return std::nullopt;
    }

    // Regular character input
    if(c >= 32 && c < 127)
    {
        if(c == keyCode(command::CommandKey::KEY_SLASH))
        {
            auto isPathCmd = [&]() -> bool
            {
                return ctx.commandBuffer.rfind(":e", 0) == 0 ||
                       ctx.commandBuffer.rfind(":edit", 0) == 0 ||
                       ctx.commandBuffer.rfind(":tabe", 0) == 0 ||
                       ctx.commandBuffer.rfind(":tabnew", 0) == 0 ||
                       ctx.commandBuffer.rfind(":w", 0) == 0 ||
                       ctx.commandBuffer.rfind(":cd", 0) == 0 ||
                       ctx.commandBuffer.rfind(":loc", 0) == 0 ||
                       ctx.commandBuffer.rfind(":loctotal", 0) == 0;
            };
            if(isPathCmd() && !ctx.commandBuffer.empty() &&
               ctx.commandBuffer.back() ==
                   keyCode(command::CommandKey::KEY_SLASH))
            {
                return std::nullopt;
            }
        }
        ctx.commandBuffer += static_cast<char>(c);
        // Reset completions when text changes
        completions.clear();
        completionIndex = -1;
        locCompletion = false;
        locCommand.clear();
        updatePopup();
    }

    return std::nullopt;
}

void CommandMode::handleTabCompletion(ModeContext& ctx)
{
    std::string input = ctx.commandBuffer.substr(
        1); // Remove keyCode(command::CommandKey::KEY_COLON)
    if(command::execution::isSubstituteCommand(input) && input != "s" &&
       input != "substitute")
        return;
    bool wholeCompletion = false;
    bool helpCompletion = false;
    bool locCompletionLocal = false;
    std::string locCommandLocal;

    // If we don't have completions yet, generate them
    if(completions.empty())
    {
        originalInput = input;

        bool isLocTotal = ctx.commandBuffer.rfind(":loctotal", 0) == 0;
        bool isLoc = ctx.commandBuffer.rfind(":loc", 0) == 0 && !isLocTotal;
        if(isLocTotal || isLoc)
        {
            locCompletionLocal = true;
            if(isLocTotal)
                locCommandLocal = "loctotal";
            else
                locCommandLocal =
                    ctx.commandBuffer.rfind(":loc!", 0) == 0 ? "loc!" : "loc";
            size_t spacePos =
                ctx.commandBuffer.find(keyChar(control::ControlKey::SPACE));
            std::string_view pathPart;
            if(text_utils::is_found(spacePos))
                pathPart =
                    std::string_view(ctx.commandBuffer).substr(spacePos + 1);
            while(!pathPart.empty() &&
                  (pathPart.front() == keyCode(control::ControlKey::SPACE) ||
                   pathPart.front() == '\t'))
            {
                pathPart.remove_prefix(1);
            }
            completions = ctx.getLocPathCompletions(pathPart);
            if(completions.empty())
                completions = ctx.getPathCompletionsRecursive(pathPart);
        }
        else if(input.rfind("set", 0) == 0)
        {
            completions = ctx.getSetCompletions(input);
            wholeCompletion = true;
        }
        else
        {
            // Check if this is a file path or help topic completion
            size_t spacePos = input.find(keyChar(control::ControlKey::SPACE));
            if(text_utils::is_found(spacePos))
            {
                std::string cmd = input.substr(0, spacePos);
                std::string_view pathPart =
                    std::string_view(input).substr(spacePos + 1);
                while(
                    !pathPart.empty() &&
                    (pathPart.front() == keyCode(control::ControlKey::SPACE) ||
                     pathPart.front() == '\t'))
                {
                    pathPart.remove_prefix(1);
                }

                if(cmd == "help" || cmd == "h")
                {
                    completions = ctx.getHelpCompletions(pathPart);
                    helpCompletion = true;
                }
                else if(
                    cmd == "e" || cmd == "edit" || cmd == "tabe" ||
                    cmd == "tabnew"
#ifdef UVIM_ENABLE_DEBUGGER
                    || cmd == "dbgrun"
#endif
                )
                {
                    completions = ctx.getPathCompletionsRecursive(pathPart);
                }
                else if(cmd == "w" || cmd == "cd")
                {
                    completions = ctx.getPathCompletionsRecursive(pathPart);
                }
                else if(cmd == "loc" || cmd == "loc!" || cmd == "loctotal")
                {
                    completions = ctx.getLocPathCompletions(pathPart);
                    if(completions.empty())
                        completions = ctx.getPathCompletionsRecursive(pathPart);
                    locCompletionLocal = true;
                    locCommandLocal = cmd;
                }
                else if(cmd == "git")
                {
                    std::vector<std::string> subcommands;
                    for(const auto& command : ctx.getCommandCompletions(""))
                        if(command.starts_with("git "))
                            subcommands.push_back(command.substr(4));
                    completions =
                        editor::helper::fuzzyCompletions(subcommands, pathPart);
                }
            }
            else
            {
                if(input == "help" || input == "h")
                {
                    completions = ctx.getHelpCompletions("");
                    completions.insert(completions.begin(), input);
                    helpCompletion = true;
                }
                else if(
                    input == "loc" || input == "loc!" || input == "loctotal")
                {
                    completions = ctx.getLocPathCompletions("");
                    if(completions.empty())
                        completions = ctx.getPathCompletionsRecursive("");
                    locCompletionLocal = true;
                    locCommandLocal = input;
                }
                else if(
                    input == "e" || input == "edit" || input == "tabe" ||
                    input == "tabnew")
                {
                    completions = ctx.getPathCompletionsRecursive("");
                }
                else
                {
                    // Complete command names
                    completions = ctx.getCommandCompletions(input);
                }
            }
        }

        if(completions.empty())
        {
            if(locCompletionLocal)
            {
                ctx.setStatusMessage("loc completions: 0");
            }
            return;
        }
    }

    if(locCompletionLocal)
    {
        locCompletion = true;
        locCommand = locCommandLocal;
    }

    // Cycle through completions
    completionIndex++;
    if(completionIndex >= static_cast<int>(completions.size()))
    {
        completionIndex = 0;
    }

    // Apply completion
    if(wholeCompletion || originalInput.rfind("set", 0) == 0)
    {
        ctx.commandBuffer = ":" + completions[completionIndex];
        return;
    }

    if(helpCompletion || originalInput.rfind("help", 0) == 0 ||
       originalInput.rfind("h", 0) == 0)
    {
        if(completions[completionIndex] == "help" ||
           completions[completionIndex] == "h")
        {
            input = completions[completionIndex];
            return;
        }
        std::string cmd = (originalInput.rfind("h", 0) == 0 &&
                           originalInput.rfind("help", 0) != 0)
                              ? "h"
                              : "help";
        ctx.commandBuffer = ":" + cmd + " " + completions[completionIndex];
        return;
    }

    size_t spacePos = originalInput.find(keyChar(control::ControlKey::SPACE));
    if(locCompletion)
    {
        ctx.commandBuffer =
            ":" + locCommand + " " + completions[completionIndex];
        return;
    }

    if(text_utils::is_found(spacePos))
    {
        std::string cmd = originalInput.substr(0, spacePos);
        ctx.commandBuffer = ":" + cmd + " " + completions[completionIndex];
        return;
    }

    ctx.commandBuffer = ":" + completions[completionIndex];
}

void CommandMode::handleReverseTabCompletion(ModeContext& ctx)
{
    if(completions.empty())
    {
        handleTabCompletion(ctx);
        return;
    }

    completionIndex--;
    if(completionIndex < 0)
    {
        completionIndex = static_cast<int>(completions.size() - 1);
    }

    size_t spacePos = originalInput.find(keyChar(control::ControlKey::SPACE));
    if(locCompletion)
    {
        ctx.commandBuffer =
            ":" + locCommand + " " + completions[completionIndex];
        return;
    }

    if(text_utils::is_found(spacePos))
    {
        std::string cmd = originalInput.substr(0, spacePos);
        ctx.commandBuffer = ":" + cmd + " " + completions[completionIndex];
    }
    else
    {
        ctx.commandBuffer = ":" + completions[completionIndex];
    }
}

void CommandMode::deleteWordBackward(ModeContext& ctx)
{
    if(ctx.commandBuffer.length() <= 1)
    {
        return;
    }

    std::string& buf = ctx.commandBuffer;
    size_t pos = buf.length() - 1;

    // Skip trailing spaces
    while(pos > 0 && buf[pos] == keyCode(control::ControlKey::SPACE))
    {
        pos--;
    }

    // Delete word characters
    while(pos > 0 && buf[pos] != keyCode(control::ControlKey::SPACE) &&
          buf[pos] != keyCode(command::CommandKey::KEY_COLON))
    {
        pos--;
    }

    // Keep the keyCode(command::CommandKey::KEY_COLON)
    if(pos == 0)
    {
        buf = ":";
    }
    else
    {
        buf = buf.substr(0, pos + 1);
    }

    completions.clear();
    completionIndex = -1;
}
} // namespace editor::statemachine
