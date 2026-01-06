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

    ctx.editor->needsFullRedraw = true;
}

void CommandMode::on_exit(ModeContext& ctx)
{
    ctx.commandBuffer.clear();
    completions.clear();
    completionIndex = -1;
}

std::optional<ModeState> CommandMode::handle(ModeContext& ctx,
                                             const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    // Escape -> cancel and return to normal mode
    if(c == Terminal::ESC)
    {
        ctx.setStatusMessage("");
        return NormalMode{};
    }

    // Enter -> execute command
    if(c == Terminal::ENTER)
    {
        if(ctx.commandBuffer.length() > 1)
        {
            std::string_view cmd(ctx.commandBuffer);
            cmd.remove_prefix(1); // Remove leading ':'
            ed->executeCommand(cmd);
            if(ed->commandRequestedModeSet)
            {
                Mode mode = ed->commandRequestedMode;
                std::string path = ed->commandRequestedPath;
                ed->commandRequestedModeSet = false;
                ed->commandRequestedPath.clear();

                if(mode == FILE_BROWSER)
                {
                    std::string prev;
                    if(ed->currentBuffer != nullptr && ed->filename)
                    {
                        prev = *ed->filename;
                    }
                    return FileBrowserMode{path, prev};
                }
            }
        }
        return NormalMode{};
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
        }
        else
        {
            // Backspace on empty command line returns to normal
            return NormalMode{};
        }
        return std::nullopt;
    }

    // Tab completion
    if(c == Terminal::TAB)
    {
        handleTabCompletion(ctx);
        return std::nullopt;
    }

    // Shift+Tab (reverse completion)
    if(c == Terminal::SHIFT_TAB)
    {
        handleReverseTabCompletion(ctx);
        return std::nullopt;
    }

    // Ctrl+W - delete word backward
    if(c == Terminal::CTRL_W)
    {
        deleteWordBackward(ctx);
        return std::nullopt;
    }

    // Ctrl+U - delete to start of line
    if(c == Terminal::CTRL_U)
    {
        ctx.commandBuffer = ":";
        completions.clear();
        completionIndex = -1;
        return std::nullopt;
    }

    // Arrow keys for command history
    if(c == Terminal::ARROW_UP)
    {
        ed->commandHistoryUp();
        return std::nullopt;
    }
    if(c == Terminal::ARROW_DOWN)
    {
        ed->commandHistoryDown();
        return std::nullopt;
    }

    // Regular character input
    if(c >= 32 && c < 127)
    {
        ctx.commandBuffer += static_cast<char>(c);
        // Reset completions when text changes
        completions.clear();
        completionIndex = -1;
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
    Editor* ed = ctx.editor;
    std::string input = ctx.commandBuffer.substr(1); // Remove ':'

    // If we don't have completions yet, generate them
    if(completions.empty())
    {
        originalInput = input;

        // Check if this is a file path completion
        size_t spacePos = input.find(' ');
        if(spacePos != std::string::npos)
        {
            // Complete file path after command
            std::string cmd = input.substr(0, spacePos);
            std::string_view pathPart =
                std::string_view(input).substr(spacePos + 1);

            if(cmd == "e" || cmd == "edit" || cmd == "w" || cmd == "tabe" ||
               cmd == "tabnew" || cmd == "cd")
            {
                completions = ed->getPathCompletions(pathPart);
            }
        }
        else
        {
            // Complete command names
            completions = ed->getCommandCompletions(input);
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
