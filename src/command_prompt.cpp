#include "mode_state_machine.h"
#include "editor.h"
#include "terminal.h"

bool CommandPrompt::isActive() const
{
    return active;
}

const std::string& CommandPrompt::getInput() const
{
    return input;
}

void CommandPrompt::setInput(std::string value)
{
    input = std::move(value);
    completions.clear();
    completionIndex = -1;
    originalInput.clear();
}

bool CommandPrompt::handle(
    ModeContext& ctx, int key,
    const std::function<std::optional<ModeState>(std::string_view)>& execute,
    std::optional<ModeState>& nextState)
{
    Editor* ed = ctx.editor;
    auto clearCompletions = [&]()
    {
        completions.clear();
        completionIndex = -1;
        originalInput.clear();
    };

    auto handleTabCompletion = [&](bool reverse)
    {
        std::string inputText = input;
        bool wholeCompletion = false;
        bool helpCompletion = false;
        bool locCompletion = false;
        std::string locCommand;

        if(completions.empty())
        {
            originalInput = inputText;
            locCompletion = false;
            locCommand.clear();

            if(inputText.rfind("set", 0) == 0)
            {
                completions = ctx.getSetCompletions(inputText);
                wholeCompletion = true;
            }
            else
            {
                size_t spacePos = inputText.find(' ');
                if(spacePos != std::string::npos)
                {
                    std::string cmd = inputText.substr(0, spacePos);
                    std::string_view pathPart =
                        std::string_view(inputText).substr(spacePos + 1);
                    while(!pathPart.empty() &&
                          (pathPart.front() == ' ' ||
                           pathPart.front() == '\t'))
                    {
                        pathPart.remove_prefix(1);
                    }

                    if(cmd == "help" || cmd == "h")
                    {
                        completions = ctx.getHelpCompletions(pathPart);
                        helpCompletion = true;
                    }
                    else if(cmd == "e" || cmd == "edit" || cmd == "tabe" ||
                            cmd == "tabnew" || cmd == "w" || cmd == "cd")
                    {
                        completions = ctx.getPathCompletionsRecursive(pathPart);
                    }
                    else if(cmd == "loc" || cmd == "loc!" ||
                            cmd == "loctotal")
                    {
                        completions = ctx.getLocPathCompletions(pathPart);
                        if(completions.empty())
                            completions =
                                ctx.getPathCompletionsRecursive(pathPart);
                        locCompletion = true;
                        locCommand = cmd;
                    }
                    else if(cmd == "git")
                    {
                        if(pathPart.empty() ||
                           std::string_view("stage").rfind(pathPart, 0) == 0)
                            completions.push_back("stage");
                        if(pathPart.empty() ||
                           std::string_view("log").rfind(pathPart, 0) == 0)
                            completions.push_back("log");
                        if(pathPart.empty() ||
                           std::string_view("prettylog").rfind(pathPart, 0) == 0)
                            completions.push_back("prettylog");
                        if(pathPart.empty() ||
                           std::string_view("stash").rfind(pathPart, 0) == 0)
                            completions.push_back("stash");
                        if(pathPart.empty() ||
                           std::string_view("stash pop").rfind(pathPart, 0) == 0)
                            completions.push_back("stash pop");
                    }
                }
                else
                {
                    if(inputText == "help" || inputText == "h")
                    {
                        completions = ctx.getHelpCompletions("");
                        helpCompletion = true;
                    }
                    else if(inputText == "loc" || inputText == "loc!" ||
                            inputText == "loctotal")
                    {
                        completions = ctx.getLocPathCompletions("");
                        if(completions.empty())
                            completions =
                                ctx.getPathCompletionsRecursive("");
                        locCompletion = true;
                        locCommand = inputText;
                    }
                    else if(inputText == "e" || inputText == "edit" ||
                            inputText == "tabe" || inputText == "tabnew" ||
                            inputText == "w" || inputText == "cd")
                    {
                        completions = ctx.getPathCompletionsRecursive("");
                    }
                    else
                    {
                        completions = ctx.getCommandCompletions(inputText);
                    }
                }
            }

            if(completions.empty())
                return;
        }

        if(reverse)
        {
            completionIndex--;
            if(completionIndex < 0)
                completionIndex = (int)completions.size() - 1;
        }
        else
        {
            completionIndex++;
            if(completionIndex >= (int)completions.size())
                completionIndex = 0;
        }

        if(wholeCompletion || originalInput.rfind("set", 0) == 0)
        {
            input = completions[completionIndex];
            return;
        }

        if(helpCompletion || originalInput.rfind("help", 0) == 0 ||
           originalInput.rfind("h", 0) == 0)
        {
            std::string cmd = (originalInput.rfind("h", 0) == 0 &&
                               originalInput.rfind("help", 0) != 0)
                                  ? "h"
                                  : "help";
            input = cmd + " " + completions[completionIndex];
            return;
        }

        size_t spacePos = originalInput.find(' ');
        if(locCompletion)
        {
            input = locCommand + " " + completions[completionIndex];
            return;
        }
        if(spacePos != std::string::npos)
        {
            std::string cmd = originalInput.substr(0, spacePos);
            input = cmd + " " + completions[completionIndex];
            return;
        }

        input = completions[completionIndex];
    };

    auto updatePopup = [&]()
    {
        if(input.empty())
        {
            if(!ctx.isCommandPopupActive())
                ctx.startCommandPopup();
            ctx.updateCommandPopup("");
            return;
        }

        bool isSetQuery = input.rfind("set", 0) == 0;
        bool isHelpQuery = input == "help" || input == "h" ||
                           input.rfind("help ", 0) == 0 ||
                           input.rfind("h ", 0) == 0;
        bool isGitQuery = input == "git" || input.rfind("git ", 0) == 0;
        bool isRegexQuery =
            !input.empty() && (input[0] == keyCode(command::CommandKey::KEY_SLASH) ||
                               input[0] == keyCode(command::CommandKey::KEY_QUESTION));
        if(isRegexQuery)
        {
            ctx.cancelCommandPopup();
            return;
        }
        if(input.find(' ') != std::string::npos && !isSetQuery &&
           !isHelpQuery && !isGitQuery)
        {
            ctx.cancelCommandPopup();
            return;
        }

        if(!ctx.isCommandPopupActive())
            ctx.startCommandPopup();
        ctx.updateCommandPopup(input);
    };

    if(!active)
    {
        if(key == keyCode(command::CommandKey::KEY_COLON))
        {
            active = true;
            input.clear();
            clearCompletions();
            updatePopup();
            ed->needsFullRedraw = true;
            nextState.reset();
            return true;
        }
        return false;
    }

    if(ctx.isCommandPopupActive())
    {
        if(key == keyCode(control::ControlKey::CTRL_K))
        {
            ctx.moveCommandPopupCursor(-1);
            if(auto selection = ctx.commandPopupSelection())
                input = *selection;
            ed->needsFullRedraw = true;
            nextState.reset();
            return true;
        }
        if(key == keyCode(control::ControlKey::CTRL_J))
        {
            ctx.moveCommandPopupCursor(1);
            if(auto selection = ctx.commandPopupSelection())
                input = *selection;
            ed->needsFullRedraw = true;
            nextState.reset();
            return true;
        }
    }

    if(ctx.isCommandHistorySearchActive())
    {
        if(key == keyCode(control::ControlKey::ESC))
        {
            input = ctx.cancelCommandHistorySearch();
            clearCompletions();
            updatePopup();
            ed->needsFullRedraw = true;
            nextState.reset();
            return true;
        }

        if(key == keyCode(control::ControlKey::ENTER))
        {
            input = ctx.acceptCommandHistorySearch();
            clearCompletions();
            updatePopup();
            ed->needsFullRedraw = true;
            nextState.reset();
            return true;
        }

        if(key == keyCode(control::ControlKey::CTRL_J) || key == keyCode(navigation::NavigationKey::ARROW_DOWN))
        {
            ctx.moveCommandHistorySearchCursor(1);
            ed->needsFullRedraw = true;
            nextState.reset();
            return true;
        }

        if(key == keyCode(control::ControlKey::CTRL_K) || key == keyCode(navigation::NavigationKey::ARROW_UP))
        {
            ctx.moveCommandHistorySearchCursor(-1);
            ed->needsFullRedraw = true;
            nextState.reset();
            return true;
        }

        if(key == keyCode(control::ControlKey::BACKSPACE) || key == keyCode(control::ControlKey::DEL) ||
           key == keyCode(control::ControlKey::CTRL_H))
        {
            std::string query(ctx.commandHistorySearchQuery());
            if(!query.empty())
            {
                query.pop_back();
                ctx.updateCommandHistorySearchQuery(query);
                input = query;
                clearCompletions();
                updatePopup();
                ed->needsFullRedraw = true;
            }
            nextState.reset();
            return true;
        }

        if(key == keyCode(control::ControlKey::CTRL_U))
        {
            ctx.updateCommandHistorySearchQuery("");
            input.clear();
            clearCompletions();
            updatePopup();
            ed->needsFullRedraw = true;
            nextState.reset();
            return true;
        }

        if(key >= keyCode(control::ControlKey::SPACE) &&
           key < keyCode(control::ControlKey::DEL))
        {
            std::string query(ctx.commandHistorySearchQuery());
            query += static_cast<char>(key);
            ctx.updateCommandHistorySearchQuery(query);
            input = query;
            clearCompletions();
            updatePopup();
            ed->needsFullRedraw = true;
            nextState.reset();
            return true;
        }

        nextState.reset();
        return true;
    }

    if(key == keyCode(control::ControlKey::ESC))
    {
        active = false;
        input.clear();
        clearCompletions();
        ctx.cancelCommandPopup();
        ed->needsFullRedraw = true;
        nextState.reset();
        return true;
    }

    if(key == keyCode(control::ControlKey::ENTER))
    {
        std::string commandToRun = input;
        if(ctx.isCommandPopupActive())
        {
            if(auto selection = ctx.commandPopupSelection())
            {
                if(commandToRun.find(' ') == std::string::npos ||
                   selection->find(' ') != std::string::npos)
                {
                    commandToRun = *selection;
                }
            }
        }

        // Execute may transition modes immediately, invalidating this object.
        // Clear prompt state first, then execute using a local command copy.
        active = false;
        input.clear();
        clearCompletions();
        ctx.cancelCommandPopup();
        ed->needsFullRedraw = true;
        nextState = execute(commandToRun);
        return true;
    }

    if(key == keyCode(control::ControlKey::BACKSPACE) || key == keyCode(control::ControlKey::DEL))
    {
        if(!input.empty())
        {
            input.pop_back();
            clearCompletions();
            updatePopup();
            ed->needsFullRedraw = true;
        }
        nextState.reset();
        return true;
    }

    if(key == keyCode(control::ControlKey::TAB))
    {
        handleTabCompletion(false);
        updatePopup();
        ed->needsFullRedraw = true;
        nextState.reset();
        return true;
    }

    if(key == keyCode(control::ControlKey::SHIFT_TAB))
    {
        handleTabCompletion(true);
        updatePopup();
        ed->needsFullRedraw = true;
        nextState.reset();
        return true;
    }

    if(key == keyCode(control::ControlKey::CTRL_F))
    {
        ctx.startCommandHistorySearch(input);
        clearCompletions();
        ctx.cancelCommandPopup();
        ed->needsFullRedraw = true;
        nextState.reset();
        return true;
    }

    if(key == keyCode(control::ControlKey::CTRL_K) || key == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        if(auto cmd = ctx.commandHistoryUp())
        {
            input = *cmd;
            clearCompletions();
            ctx.cancelCommandPopup();
            ed->needsFullRedraw = true;
        }
        nextState.reset();
        return true;
    }

    if(key == keyCode(control::ControlKey::CTRL_J) || key == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        if(auto cmd = ctx.commandHistoryDown())
        {
            input = *cmd;
            clearCompletions();
            ctx.cancelCommandPopup();
            ed->needsFullRedraw = true;
        }
        nextState.reset();
        return true;
    }

    if(key >= keyCode(control::ControlKey::SPACE) &&
       key < keyCode(control::ControlKey::DEL))
    {
        if(key == keyCode(command::CommandKey::KEY_SLASH))
        {
            auto isPathCmd = [&]() -> bool
            {
                return input.rfind("e", 0) == 0 ||
                       input.rfind("edit", 0) == 0 ||
                       input.rfind("tabe", 0) == 0 ||
                       input.rfind("tabnew", 0) == 0 ||
                       input.rfind("w", 0) == 0 ||
                       input.rfind("cd", 0) == 0 ||
                       input.rfind("loc", 0) == 0 ||
                       input.rfind("loctotal", 0) == 0;
            };
            if(isPathCmd() && !input.empty() && input.back() == '/')
            {
                nextState.reset();
                return true;
            }
        }
        input += static_cast<char>(key);
        clearCompletions();
        updatePopup();
        ed->needsFullRedraw = true;
        nextState.reset();
        return true;
    }

    nextState.reset();
    return true;
}
