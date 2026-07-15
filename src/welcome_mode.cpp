#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>
#include <string>
#include <vector>

// ============================================================================
// WelcomeMode Implementation
// ============================================================================

namespace editor::statemachine
{
void WelcomeMode::on_enter(ModeContext& ctx)
{
    commandPrompt = ctx.commandPrompt();
    ctx.requestFullRedraw();
}

void WelcomeMode::on_exit(ModeContext& /* ctx */) {}

std::optional<ModeState> WelcomeMode::handle(ModeContext& ctx,
                                             const ModeKeyEvent& event)
{
    const int key = event.key;
    int c = keyCode(key);

    std::optional<ModeState> nextState;
    if(commandPrompt &&
       commandPrompt->handle(
           ctx, c, [&](std::string_view commandLine)
           { return executeCommand(ctx, commandLine); }, nextState))
    {
        return nextState;
    }

    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(control::ControlKey::ENTER))
    {
        if(c == keyCode(control::ControlKey::ESC))
            ctx.editor->noteDoubleEscStatusClear();
        ctx.commandBuffer.clear();
        return std::nullopt;
    }

    if(ctx.commandBuffer == " ")
    {
        ctx.commandBuffer.clear();
        ctx.setStatusMessage("");
        if(c == keyCode(typed::TypedKey::KEY_X))
        {
#ifdef UVIM_ENABLE_BROWSER_TOOLS
            return FileBrowserMode{"."};
#else
            return std::nullopt;
#endif
        }
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::SPACE))
    {
        ctx.commandBuffer = " ";
        ctx.setStatusMessage("Leader");
        return std::nullopt;
    }

    if(c == keyCode(typed::TypedKey::KEY_I) ||
       c == keyCode(typed::TypedKey::KEY_CAP_I))
    {
        return InsertMode{};
    }

    if(c == keyCode(typed::TypedKey::KEY_E))
    {
#ifdef UVIM_ENABLE_BROWSER_TOOLS
        return FileBrowserMode{"."};
#else
        return std::nullopt;
#endif
    }

#ifdef UVIM_ENABLE_SEARCH_TOOLS
    if(c == keyCode(typed::TypedKey::KEY_F) ||
       c == keyCode(control::ControlKey::CTRL_P))
    {
        return FuzzyFindMode{};
    }

    if(c == keyCode(command::CommandKey::KEY_SLASH) ||
       c == keyCode(control::ControlKey::CTRL_S))
    {
        return GrepSearchMode{};
    }
#endif

#ifdef UVIM_ENABLE_BROWSER_TOOLS
    if(c == keyCode(typed::TypedKey::KEY_B) ||
       c == keyCode(control::ControlKey::CTRL_W))
    {
        return BufferBrowserMode{};
    }
#endif

    if(c == keyCode(typed::TypedKey::KEY_Q))
    {
        ctx.forceQuit();
    }

    return std::nullopt;
}

std::optional<ModeState>
WelcomeMode::executeCommand(ModeContext& ctx, std::string_view commandLine)
{
    std::string previousFile;
    if(ctx.hasCurrentBuffer() && ctx.hasFilename())
    {
        previousFile = std::string(ctx.currentFilename());
    }

    return dispatchCommandLine(
        ctx, commandLine,
        [&](ModeContext& /* ctx */, const ParsedCommand& command,
            std::optional<ModeState>& nextState) -> bool
        {
            if(command.cmd == "help" || command.cmd == "h")
            {
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
                nextState = HelpMode{command.args, previousFile};
                return true;
#else
                return false;
#endif
            }
            return false;
        },
        [&](ModeContext& ctx, std::string_view line)
        { return dispatchEditorCommand(ctx, line, previousFile, true); });
}

void WelcomeMode::draw(Editor& editor) const
{
    struct Item
    {
        std::string key;
        std::string desc;
    };

    auto append_with_enter_color =
        [&](std::string& out, const std::string& line)
    {
        const std::string token = "<Enter>";
        size_t pos = 0;
        while(true)
        {
            size_t hit = line.find(token, pos);
            if(text_utils::is_not_found(hit))
            {
                out.append(line, pos, text_utils::npos());
                break;
            }
            out.append(line, pos, hit - pos);
            out += editor.theme.uiInfo();
            out += token;
            out += editor.theme.baseFg();
            pos = hit + token.size();
        }
    };

    std::vector<std::string> lines;
    lines.reserve(32);

#ifdef UVIM_DISPLAY_VERSION
    lines.push_back(std::string("uVim v") + UVIM_DISPLAY_VERSION);
#else
    lines.push_back("uVim");
#endif
    lines.push_back("");
    lines.push_back("Author: Matti Laamanen");
    lines.push_back("");
    lines.push_back("Getting started:");

    std::vector<Item> commands = {
        {":e <file><Enter>", "Open a file / start editing a new file"},
        {":q<Enter>", "Quit"},
        {":q!<Enter>", "Quit without saving"},
        {":wa<Enter>", "Save all buffers"},
        {":qw<Enter>", "Save all buffers and quit"},
        {":help", "Open help page (supports fuzzy find)"},
    };

    std::vector<Item> keys = {
        {"<Leader>x", "Open file browser"},
        {"i", "Enter insert mode / start editing an empty file"},
        {":", "Command mode"},
        {"Ctrl-P", "Fuzzy find files"},
        {"Ctrl-W", "Buffer browser"},
        {"Ctrl-S", "Search in files (grep)"},
    };

    auto append_items = [&](const std::vector<Item>& items)
    {
        size_t keyWidth = 0;
        for(const auto& item : items)
            keyWidth = std::max(keyWidth, item.key.size());

        for(const auto& item : items)
        {
            std::string line = "  " + item.key;
            line.append(keyWidth - item.key.size(),
                        keyCode(control::ControlKey::SPACE));
            line += "  ";
            line += item.desc;
            lines.push_back(std::move(line));
        }
    };

    append_items(commands);
    lines.push_back("");
    lines.push_back("Quick keys:");
    append_items(keys);

    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

    output += Terminal::ESC_CURSOR_HOME;
    output += editor.theme.reset();

    int maxWidth = 0;
    for(const auto& line : lines)
        maxWidth = std::max(maxWidth, (int)line.size());

    int totalLines = (int)lines.size();
    int startRow = std::max(0, (editor.screenRows - totalLines) / 2);
    int startCol = std::max(0, (editor.screenCols - maxWidth) / 2);

    for(int row = 0; row < editor.screenRows; row++)
    {
        if(row == 0)
        {
            output += Terminal::ESC_CLEAR_LINE;
        }
        else
        {
            output += Terminal::NEWLINE_CLEAR;
        }

        if(row < startRow || row >= startRow + totalLines)
        {
            continue;
        }

        const std::string& line = lines[row - startRow];
        int padding = startCol;

        if(!line.empty())
        {
            output.append(padding, keyCode(control::ControlKey::SPACE));
            if(row - startRow == 0)
            {
                output += Terminal::ESC_BOLD;
                append_with_enter_color(output, line);
                output += editor.theme.reset();
            }
            else
            {
                append_with_enter_color(output, line);
            }
        }
    }

    int labelRow = editor.screenRows + 1;
    int promptRow = editor.screenRows + 2;

    output += Terminal::cursorPos(promptRow, 1);
    output += Terminal::ESC_CLEAR_LINE;
    output += Terminal::cursorPos(labelRow, 1);
    output += Terminal::ESC_CLEAR_LINE;

    if(commandPrompt && commandPrompt->isActive())
    {
        output += Terminal::cursorPos(promptRow, 1);
        output += editor.theme.baseFg();
        output += ":";
        output += commandPrompt->getInput();

        output += Terminal::cursorPos(labelRow, 1);
        output += Terminal::ESC_BOLD;
        output += " COMMAND | ";
        output += editor.theme.reset();
    }

    editor.drawCommandHistoryPopup(output);
    editor.drawCommandPopup(output);

    if(commandPrompt && commandPrompt->isActive())
    {
        output += Terminal::ESC_SHOW_CURSOR;
        int row = editor.screenRows + 2;
        int col = 2 + (int)commandPrompt->getInput().size();
        output += Terminal::cursorPos(row, col);
    }
    else
    {
        output += Terminal::ESC_HIDE_CURSOR;
    }

    Terminal::write(output);
    Terminal::flush();
}
} // namespace editor::statemachine
