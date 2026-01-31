#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include <algorithm>

// ============================================================================
// CommandMode Implementation
// ============================================================================

void CommandMode::on_enter(ModeContext& ctx)
{
    ctx.commandBuffer = ":";
    completions.clear();
    completionIndex = -1;
    originalInput.clear();
    ctx.startCommandPopup();
    ctx.updateCommandPopup("");

    ctx.requestFullRedraw();
}

void CommandMode::on_exit(ModeContext& ctx)
{
    ctx.commandBuffer.clear();
    completions.clear();
    completionIndex = -1;
    ctx.cancelCommandPopup();
    if(ctx.isCommandHistorySearchActive())
    {
        ctx.cancelCommandHistorySearch();
    }
}

std::optional<ModeState> CommandMode::handle(ModeContext& ctx,
                                             const KeyEvent& event)
{
    int c = event.key;
    auto updatePopup = [&]()
    {
        if(ctx.commandBuffer.length() <= 1)
        {
            if(!ctx.isCommandPopupActive())
                ctx.startCommandPopup();
            ctx.updateCommandPopup("");
            return;
        }

        std::string query = ctx.commandBuffer.substr(1);
        bool isSetQuery = query.rfind("set", 0) == 0;
        bool isHelpQuery = query == "help" || query == "h" ||
                           query.rfind("help ", 0) == 0 ||
                           query.rfind("h ", 0) == 0;
        if(query.find(' ') != std::string::npos && !isSetQuery && !isHelpQuery)
        {
            ctx.cancelCommandPopup();
            return;
        }

        if(!ctx.isCommandPopupActive())
            ctx.startCommandPopup();
        ctx.updateCommandPopup(query);
    };

    if(ctx.isCommandHistorySearchActive())
    {
        if(c == Terminal::ESC)
        {
            ctx.editor->noteDoubleEscStatusClear();
            std::string restored = ctx.cancelCommandHistorySearch();
            ctx.commandBuffer = ":" + restored;
            updatePopup();
            return std::nullopt;
        }

        if(c == Terminal::ENTER)
        {
            std::string selected = ctx.acceptCommandHistorySearch();
            ctx.commandBuffer = ":" + selected;
            updatePopup();
            return std::nullopt;
        }

        if(c == Terminal::CTRL_J || c == Terminal::ARROW_DOWN)
        {
            ctx.moveCommandHistorySearchCursor(1);
            return std::nullopt;
        }

        if(c == Terminal::CTRL_K || c == Terminal::ARROW_UP)
        {
            ctx.moveCommandHistorySearchCursor(-1);
            return std::nullopt;
        }

        if(c == Terminal::BACKSPACE || c == 127 || c == Terminal::CTRL_H)
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

        if(c == Terminal::CTRL_U)
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
    if(c == Terminal::ESC)
    {
        ctx.editor->noteDoubleEscStatusClear();
        ctx.setStatusMessage("");
        ctx.cancelCommandPopup();
        return ctx.hasBuffer() ? ModeState{NormalMode{}}
                               : ModeState{WelcomeMode{}};
    }

    // Enter -> execute command
    if(c == Terminal::ENTER)
    {
        if(ctx.isCommandPopupActive())
        {
            if(auto selection = ctx.commandPopupSelection())
            {
                std::string_view query =
                    std::string_view(ctx.commandBuffer).substr(1);
                bool hasSpace = query.find(' ') != std::string::npos;
                auto starts_with_ci = [&](std::string_view text,
                                          std::string_view prefix) -> bool
                {
                    if(prefix.size() > text.size())
                        return false;
                    for(size_t i = 0; i < prefix.size(); ++i)
                    {
                        unsigned char a =
                            static_cast<unsigned char>(text[i]);
                        unsigned char b =
                            static_cast<unsigned char>(prefix[i]);
                        if(a >= 'A' && a <= 'Z')
                            a = (unsigned char)(a - 'A' + 'a');
                        if(b >= 'A' && b <= 'Z')
                            b = (unsigned char)(b - 'A' + 'a');
                        if(a != b)
                            return false;
                    }
                    return true;
                };

                bool shouldReplace = query.empty();
                if(!hasSpace && !shouldReplace)
                {
                    shouldReplace = starts_with_ci(*selection, query);
                }
                else if(hasSpace && selection->find(' ') != std::string::npos)
                {
                    shouldReplace = true;
                }

                if(shouldReplace)
                    ctx.commandBuffer = ":" + *selection;
            }
        }

        if(ctx.commandBuffer.length() > 1)
        {
            ctx.cancelCommandPopup();
            std::string_view cmd(ctx.commandBuffer);
            cmd.remove_prefix(1); // Remove leading ':'
            ctx.executeCommand(cmd);
            if(ctx.currentMode() == LSP_INFO)
            {
                return LspInfoMode{};
            }
            Mode mode = NORMAL;
            std::string path;
            if(ctx.takeCommandRequest(mode, path))
            {
                if(mode == FILE_BROWSER)
                {
                    std::string prev;
                    if(ctx.hasCurrentBuffer() && ctx.hasFilename())
                    {
                        prev = std::string(ctx.currentFilename());
                    }
                    return FileBrowserMode{path, prev};
                }
                if(mode == LSP_INFO)
                {
                    return LspInfoMode{};
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
            }
        }
        return ctx.hasBuffer() ? ModeState{NormalMode{}}
                               : ModeState{WelcomeMode{}};
    }

    // Backspace
    if(c == Terminal::BACKSPACE || c == 127 || c == Terminal::CTRL_H)
    {
        if(ctx.commandBuffer.length() > 1)
        {
            ctx.commandBuffer.pop_back();
            // Reset completions when text changes
            completions.clear();
            completionIndex = -1;
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
    if(c == Terminal::CTRL_F)
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
        return std::nullopt;
    }

    // Tab completion
    if(c == Terminal::TAB)
    {
        handleTabCompletion(ctx);
        updatePopup();
        return std::nullopt;
    }

    // Shift+Tab (reverse completion)
    if(c == Terminal::SHIFT_TAB)
    {
        handleReverseTabCompletion(ctx);
        updatePopup();
        return std::nullopt;
    }

    // Ctrl+W - delete word backward
    if(c == Terminal::CTRL_W)
    {
        deleteWordBackward(ctx);
        updatePopup();
        return std::nullopt;
    }

    // Ctrl+U - delete to start of line
    if(c == Terminal::CTRL_U)
    {
        ctx.commandBuffer = ":";
        completions.clear();
        completionIndex = -1;
        updatePopup();
        return std::nullopt;
    }

    if(c == Terminal::CTRL_K)
    {
        if(ctx.isCommandPopupActive())
        {
            ctx.moveCommandPopupCursor(-1);
            if(auto selection = ctx.commandPopupSelection())
            {
                if(ctx.commandBuffer.find(' ') == std::string::npos ||
                   selection->find(' ') != std::string::npos)
                {
                    ctx.commandBuffer = ":" + *selection;
                }
            }
            return std::nullopt;
        }
    }

    if(c == Terminal::CTRL_J)
    {
        if(ctx.isCommandPopupActive())
        {
            ctx.moveCommandPopupCursor(1);
            if(auto selection = ctx.commandPopupSelection())
            {
                if(ctx.commandBuffer.find(' ') == std::string::npos ||
                   selection->find(' ') != std::string::npos)
                {
                    ctx.commandBuffer = ":" + *selection;
                }
            }
            return std::nullopt;
        }
    }

    // Arrow keys for command history
    if(c == Terminal::ARROW_UP)
    {
        if(auto cmd = ctx.commandHistoryUp())
        {
            ctx.commandBuffer = ":" + *cmd;
            completions.clear();
            completionIndex = -1;
            updatePopup();
        }
        return std::nullopt;
    }
    if(c == Terminal::ARROW_DOWN)
    {
        if(auto cmd = ctx.commandHistoryDown())
        {
            ctx.commandBuffer = ":" + *cmd;
            completions.clear();
            completionIndex = -1;
            updatePopup();
        }
        return std::nullopt;
    }

    // Regular character input
    if(c >= 32 && c < 127)
    {
        ctx.commandBuffer += static_cast<char>(c);
        // Reset completions when text changes
        completions.clear();
        completionIndex = -1;
        updatePopup();
    }

    return std::nullopt;
}

void Editor::handleCommandMode(int c)
{
    if(dispatchModeKey(c))
    {
        return;
    }

    if(c == Terminal::ESC)
    {
        noteDoubleEscStatusClear();
        commandBuffer.clear();
        setMode(NORMAL);
        return;
    }

    if(c == Terminal::ENTER)
    {
        if(commandBuffer.length() > 1)
        {
            std::string_view cmd(commandBuffer);
            cmd.remove_prefix(1);
            executeCommand(cmd);
        }
        commandBuffer.clear();
        setMode(NORMAL);
        return;
    }

    if(c == Terminal::BACKSPACE || c == 127 || c == 8)
    {
        if(commandBuffer.length() > 1)
        {
            commandBuffer.pop_back();
        }
        else
        {
            setMode(NORMAL);
        }
        return;
    }

    if(c >= 32 && c < 127)
    {
        commandBuffer += (char)c;
    }
}
void CommandMode::handleTabCompletion(ModeContext& ctx)
{
    std::string input = ctx.commandBuffer.substr(1); // Remove ':'
    bool wholeCompletion = false;
    bool helpCompletion = false;

    // If we don't have completions yet, generate them
    if(completions.empty())
    {
        originalInput = input;

        if(input.rfind("set", 0) == 0)
        {
            completions = ctx.getSetCompletions(input);
            wholeCompletion = true;
        }
        else
        {
            // Check if this is a file path or help topic completion
            size_t spacePos = input.find(' ');
            if(spacePos != std::string::npos)
            {
                std::string cmd = input.substr(0, spacePos);
                std::string_view pathPart =
                    std::string_view(input).substr(spacePos + 1);

                if(cmd == "help" || cmd == "h")
                {
                    completions = ctx.getHelpCompletions(pathPart);
                    helpCompletion = true;
                }
                else if(cmd == "e" || cmd == "edit" || cmd == "w" ||
                        cmd == "tabe" || cmd == "tabnew" || cmd == "cd")
                {
                    completions = ctx.getPathCompletions(pathPart);
                }
            }
            else
            {
                if(input == "help" || input == "h")
                {
                    completions = ctx.getHelpCompletions("");
                    helpCompletion = true;
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
            return;
        }
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
        std::string cmd = (originalInput.rfind("h", 0) == 0 &&
                           originalInput.rfind("help", 0) != 0)
                              ? "h"
                              : "help";
        ctx.commandBuffer = ":" + cmd + " " + completions[completionIndex];
        return;
    }

    size_t spacePos = originalInput.find(' ');
    if(spacePos != std::string::npos)
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
        completionIndex = completions.size() - 1;
    }

    size_t spacePos = originalInput.find(' ');
    if(spacePos != std::string::npos)
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
    while(pos > 0 && buf[pos] == ' ')
    {
        pos--;
    }

    // Delete word characters
    while(pos > 0 && buf[pos] != ' ' && buf[pos] != ':')
    {
        pos--;
    }

    // Keep the ':'
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
