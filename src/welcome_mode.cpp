#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include <algorithm>
#include <string>
#include <vector>

// ============================================================================
// WelcomeMode Implementation
// ============================================================================

void WelcomeMode::on_enter(ModeContext& ctx)
{
    ctx.editor->needsFullRedraw = true;
}

void WelcomeMode::on_exit(ModeContext& /* ctx */) {}

std::optional<ModeState> WelcomeMode::handle(ModeContext& ctx,
                                             const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    if(c == Terminal::ESC || c == Terminal::ENTER)
    {
        return NormalMode{};
    }

    if(c == ':')
    {
        return CommandMode{};
    }

    if(c == 'i' || c == 'I')
    {
        return InsertMode{};
    }

    if(c == 'e')
    {
        return FileBrowserMode{"."};
    }

    if(c == 'f' || c == Terminal::CTRL_P)
    {
        return FuzzyFindMode{};
    }

    if(c == 'b' || c == Terminal::CTRL_W)
    {
        return BufferBrowserMode{};
    }

    if(c == '/' || c == Terminal::CTRL_S)
    {
        return GrepSearchMode{};
    }

    if(c == 'q')
    {
        ed->forceQuit();
    }

    return NormalMode{};
}

void WelcomeMode::draw(Editor& editor) const
{
    struct Item
    {
        std::string key;
        std::string desc;
    };

    auto append_with_enter_color = [](std::string& out,
                                      const std::string& line)
    {
        const std::string token = "<Enter>";
        size_t pos = 0;
        while(true)
        {
            size_t hit = line.find(token, pos);
            if(hit == std::string::npos)
            {
                out.append(line, pos, std::string::npos);
                break;
            }
            out.append(line, pos, hit - pos);
            out += Terminal::FG_BRIGHT_CYAN;
            out += token;
            out += Terminal::FG_DEFAULT;
            pos = hit + token.size();
        }
    };

    std::vector<std::string> lines;
    lines.reserve(32);

    lines.push_back("uVim");
    lines.push_back("");
    lines.push_back("Author: Matti Laamanen");
    lines.push_back("");
    lines.push_back("Welcome to uVim");
    lines.push_back("");
    lines.push_back("Getting started:");

    std::vector<Item> commands = {
        {":e <file><Enter>", "open a file"},
        {":Ex<Enter>", "open file browser"},
        {":q<Enter>", "quit"},
        {":q!<Enter>", "quit without saving"},
        {":wa<Enter>", "save all buffers"},
        {":qw<Enter>", "save all buffers and quit"},
    };

    std::vector<Item> keys = {
        {"i", "enter insert mode"},
        {":", "command mode"},
        {"Ctrl-P", "fuzzy find files"},
        {"Ctrl-W", "buffer browser"},
        {"Ctrl-S", "search in files"},
    };

    auto append_items = [&](const std::vector<Item>& items)
    {
        size_t keyWidth = 0;
        for(const auto& item : items)
            keyWidth = std::max(keyWidth, item.key.size());

        for(const auto& item : items)
        {
            std::string line = "  " + item.key;
            line.append(keyWidth - item.key.size(), ' ');
            line += "  ";
            line += item.desc;
            lines.push_back(std::move(line));
        }
    };

    append_items(commands);
    lines.push_back("");
    lines.push_back("Quick keys:");
    append_items(keys);
    lines.push_back("");
    lines.push_back("Press <Enter> or Esc to start editing");

    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

    output += Terminal::ESC_CURSOR_HOME;

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
            output.append(padding, ' ');
            if(row - startRow == 0)
            {
                output += Terminal::ESC_BOLD;
                append_with_enter_color(output, line);
                output += Terminal::ESC_RESET_ALL;
            }
            else
            {
                append_with_enter_color(output, line);
            }
        }
    }

    Terminal::write(output);
    Terminal::write(Terminal::cursorPos(1, 1));
    Terminal::flush();
}
