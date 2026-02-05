#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>
#include <sstream>

// ============================================================================
// Help Mode Implementation
// ============================================================================

void HelpMode::on_enter(ModeContext& ctx)
{
    if(previousFile.empty() && ctx.hasCurrentBuffer() && ctx.hasFilename())
    {
        previousFile = std::string(ctx.currentFilename());
    }

    loadHelpContent(topic);
    ctx.requestFullRedraw();
}

void HelpMode::on_exit(ModeContext& /* ctx */) {}

std::optional<ModeState> HelpMode::handle(ModeContext& ctx,
                                          const KeyEvent& event)
{
    int c = event.key;

    std::optional<ModeState> nextState;
    if(commandPrompt.handle(
           ctx, c, [&](std::string_view commandLine)
           { return executeCommand(ctx, commandLine); }, nextState))
    {
        return nextState;
    }

    // ========================================================================
    // Exit
    // ========================================================================

    if(c == Terminal::ESC || c == 'q')
    {
        if(c == Terminal::ESC)
            ctx.editor->noteDoubleEscStatusClear();
        if(!previousFile.empty())
        {
            ctx.openFile(std::string_view(previousFile));
            return NormalMode{};
        }
        return WelcomeMode{};
    }

    // ========================================================================
    // Navigation
    // ========================================================================

    if(c == 'j' || c == Terminal::ARROW_DOWN)
    {
        int maxScroll = std::max(0, (int)lines.size() - (ctx.screenRows() - 3));
        if(scrollOffset < maxScroll)
        {
            scrollOffset++;
        }
    }
    else if(c == 'k' || c == Terminal::ARROW_UP)
    {
        if(scrollOffset > 0)
        {
            scrollOffset--;
        }
    }
    else if(c == 'G')
    {
        scrollOffset = std::max(0, (int)lines.size() - (ctx.screenRows() - 3));
    }
    else if(c == 'g')
    {
        int nextChar = Terminal::readKey();
        if(nextChar == 'g')
        {
            scrollOffset = 0;
        }
    }
    else if(c == Terminal::CTRL_D)
    {
        int half = (ctx.screenRows() - 3) / 2;
        scrollOffset += half;
        int maxScroll = std::max(0, (int)lines.size() - (ctx.screenRows() - 3));
        if(scrollOffset > maxScroll)
            scrollOffset = maxScroll;
    }
    else if(c == Terminal::CTRL_U)
    {
        int half = (ctx.screenRows() - 3) / 2;
        scrollOffset -= half;
        if(scrollOffset < 0)
            scrollOffset = 0;
    }

    ctx.requestFullRedraw();
    return std::nullopt;
}

void HelpMode::draw(Editor& editor) const
{
    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

    output += Terminal::ESC_CURSOR_HOME;
    output += editor.theme.reset();

    // Draw header
    output += Terminal::ESC_CLEAR_LINE;
    output += editor.theme.uiAccent();
    output += Terminal::ESC_BOLD;
    output += "  HELP";
    if(!topic.empty())
    {
        output += ": " + topic;
    }
    output += editor.theme.reset();
    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.uiDim();
    output += "  [q: quit] [j/k: scroll] [gg/G: top/bottom] [:help <topic>: "
              "navigate]";
    output += editor.theme.baseFg();

    int availableRows = editor.screenRows - 2;

    // Draw help content
    for(int i = 0; i < availableRows && i + scrollOffset < (int)lines.size();
        i++)
    {
        output += Terminal::NEWLINE_CLEAR;

        const std::string& line = lines[i + scrollOffset];

        // Apply syntax highlighting to the line
        output += "  ";

        // Check if line is a topic title (starts with # or all caps)
        if(!line.empty() && line[0] == '#')
        {
            // Topic title
            output += editor.theme.syntax(TOKEN_KEYWORD);
            output += Terminal::ESC_BOLD;
            output += line.substr(1); // Skip #
            output += editor.theme.reset();
        }
        else if(!line.empty() && std::isupper(line[0]) &&
                line.find(':') != std::string::npos && line.find(':') < 30)
        {
            // Section header (e.g., "COMMANDS:")
            output += editor.theme.uiAccent();
            output += Terminal::ESC_BOLD;
            output += line;
            output += editor.theme.reset();
        }
        else
        {
            // Regular line with command highlighting
            std::string processedLine;
            size_t pos = 0;

            while(pos < line.length())
            {
                // Look for commands starting with : or starting with uppercase
                if(line[pos] == ':')
                {
                    // Find end of command
                    size_t end = pos + 1;
                    while(end < line.length() &&
                          (std::isalnum(line[end]) || line[end] == '!' ||
                           line[end] == '?'))
                    {
                        end++;
                    }

                    // Highlight command
                    processedLine += editor.theme.syntax(TOKEN_STRING);
                    processedLine += Terminal::ESC_BOLD;
                    processedLine += line.substr(pos, end - pos);
                    processedLine += editor.theme.reset();
                    pos = end;
                }
                else if(line[pos] == '`')
                {
                    // Code/command in backticks
                    size_t end = line.find('`', pos + 1);
                    if(end != std::string::npos)
                    {
                        processedLine += editor.theme.syntax(TOKEN_FUNCTION);
                        processedLine += line.substr(pos + 1, end - pos - 1);
                        processedLine += editor.theme.reset();
                        pos = end + 1;
                    }
                    else
                    {
                        processedLine += line[pos++];
                    }
                }
                else
                {
                    processedLine += line[pos++];
                }
            }

            output += processedLine;
        }

        output += editor.theme.reset();
    }

    // Fill remaining lines
    for(int i = std::min((int)lines.size() - scrollOffset, availableRows);
        i < availableRows; i++)
    {
        output += Terminal::NEWLINE_CLEAR;
        output += editor.theme.uiGutter();
        output += "  ~";
        output += editor.theme.baseFg();
    }

    // Status bar
    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.statusBar();

    std::string status = " HELP";
    if(!topic.empty())
        status += " | " + topic;

    std::string right = " " + std::to_string(scrollOffset + 1) + "-" +
                        std::to_string(std::min(scrollOffset + availableRows,
                                                (int)lines.size())) +
                        "/" + std::to_string(lines.size()) + " ";

    output += status;
    int padding = editor.screenCols - status.length() - right.length();
    if(padding > 0)
    {
        output.append(padding, ' ');
    }
    output += right;
    output += editor.theme.reset();

    // Message line
    output += Terminal::NEWLINE_CLEAR;
    if(commandPrompt.isActive())
    {
        output += ":";
        output += commandPrompt.getInput();
    }
    else if(!editor.statusMessage.empty())
    {
        output += editor.statusMessage.substr(
            0,
            std::min((size_t)editor.screenCols, editor.statusMessage.length()));
    }

    editor.drawCommandHistoryPopup(output);
    editor.drawCommandPopup(output);

    if(commandPrompt.isActive())
    {
        output += Terminal::ESC_SHOW_CURSOR;
        int row = editor.screenRows + 2;
        int col = 2 + (int)commandPrompt.getInput().size();
        output += Terminal::cursorPos(row, col);
    }
    else
    {
        output += Terminal::ESC_HIDE_CURSOR;
    }

    Terminal::write(output);
    Terminal::flush();
}

void HelpMode::loadHelpContent(const std::string& helpTopic)
{
    lines.clear();

    // Convert topic to lowercase for matching
    std::string topic_lower = helpTopic;
    while(!topic_lower.empty() && text_utils::is_space(topic_lower.front()))
        topic_lower.erase(topic_lower.begin());
    while(!topic_lower.empty() && text_utils::is_space(topic_lower.back()))
        topic_lower.pop_back();
    std::transform(topic_lower.begin(), topic_lower.end(), topic_lower.begin(),
                   ::tolower);

    // Load help content based on topic
    if(topic_lower.empty() || topic_lower == "index" || topic_lower == "help")
    {
        lines = {
            "# uvim Help",
            "",
            "Welcome to uvim! Type `:help <topic>` for specific help.",
            "",
            "AVAILABLE TOPICS:",
            "  `:help commands`   - List of all commands",
            "  `:help modes`      - Editor modes",
            "  `:help navigation` - Moving around",
            "  `:help editing`    - Editing text",
            "  `:help files`      - File operations",
            "  `:help buffers`    - Buffer management",
            "  `:help search`     - Searching and replacing",
            "  `:help clipboard`  - Clipboard operations",
            "  `:help git`        - Git integrations",
            "",
            "QUICK START:",
            "  `i`        - Enter insert mode",
            "  `ESC`      - Return to normal mode",
            "  `:w`       - Save file",
            "  `:q`       - Quit",
            "  `:wq`      - Save and quit",
            "",
            "NAVIGATION:",
            "  `h j k l`  - Move left/down/up/right",
            "  `gg`       - Go to top",
            "  `G`        - Go to bottom",
            "  `Ctrl-f`   - Fuzzy file finder",
            "  `Space-e`  - File browser",
            "",
            "Press `q` to close this help window.",
        };
    }
    else if(topic_lower == "commands")
    {
        lines = {
            "# Command Reference",
            "",
            "FILE OPERATIONS:",
            "  `:w`              - Write (save) current file",
            "  `:w <file>`       - Save to specific file",
            "  `:q`              - Quit (fails if unsaved changes)",
            "  `:q!`             - Force quit without saving",
            "  `:wq` or `:x`     - Save and quit",
            "  `:wa`             - Write all buffers",
            "  `:qa`             - Quit all buffers",
            "  `:qa!`            - Force quit all",
            "  `:wqa` or `:xa`   - Write all and quit",
            "",
            "BUFFER MANAGEMENT:",
            "  `:bn` or `:bnext`     - Next buffer",
            "  `:bp` or `:bprev`     - Previous buffer",
            "  `:bd` or `:bdelete`   - Delete buffer",
            "  `:bd!`                - Force delete buffer",
            "  `:ls` or `:buffers`   - List buffers",
            "  `:b <n>`              - Switch to buffer n",
            "  `:enew`               - Create new buffer",
            "",
            "FILE BROWSER:",
            "  `:Ex` or `:Explore`   - Open file browser",
            "  `:cd <path>`          - Change directory",
            "  `:pwd`                - Print working directory",
            "",
            "LOC:",
            "  `:loc <path>`         - Count LOC (non-empty, non-comment)",
            "  `:loc! <path>`        - Show LOC list view",
            "  `:loc%`               - Count LOC in current buffer",
            "  `:loctotal <path>`    - Count total LOC (respects .gitignore)",
            "  In LOC view: `s` sort asc/desc, `Esc` reset sort",
            "",
            "FILE BROWSER COMMANDS:",
            "  `:q`                  - Exit file browser",
            "  `:cd <path>`          - Change directory",
            "  `:mkdir <name>`       - Create directory",
            "  `:touch <name>`       - Create file",
            "  `:delete` or `:d`     - Delete selected file",
            "  `:rename <name>`      - Rename selected file",
            "",
            "GIT:",
            "  `gb`      - Toggle git blame gutter",
            "  `gbv`     - Show commit diff for line under cursor",
            "  `gl`      - Show git log (repo)",
            "  `glf`     - Show git log (current file)",
            "  `:set disablegitdefaultcolors` - Use editor theme for git views",
            "  `:set enablegitdefaultcolors`  - Use git's default colors",
            "",
            "SETTINGS:",
            "  `:set number` or `:set nu`     - Show line numbers",
            "  `:set nonumber` or `:set nonu` - Hide line numbers",
            "  `:set ignorecase` or `:set ic` - Case insensitive search",
            "  `:set smartcase` or `:set scs` - Smart case search",
            "  `:set gdcenter`               - Center view after gd",
            "  `:set nogdcenter`             - Keep view steady after gd",
            "",
            "HELP:",
            "  `:help`           - Show this help",
            "  `:help <topic>`   - Show help for topic",
        };
    }
    else if(topic_lower == "git")
    {
        lines = {
            "# Git Integrations",
            "",
            "GIT BLAME:",
            "  `gb`   - Toggle git blame gutter",
            "  `gbv`  - Show commit diff for line under cursor",
            "",
            "GIT LOG:",
            "  `gl`   - Browse git log (repo)",
            "  `glf`  - Browse git log (current file)",
            "  Use `ctrl-j/k` to move, `enter` to open diff, `q` to quit",
            "",
            "GIT VIEW COLORS:",
            "  `:set disablegitdefaultcolors` - Use editor theme colors",
            "  `:set enablegitdefaultcolors`  - Use git's default colors",
            "",
            "COMMENT TOGGLE:",
            "  `:set commenttogglepartial`   - Toggle on any commented line",
            "  `:set nocommenttogglepartial` - Toggle only if all commented",
        };
    }
    else if(topic_lower == "modes")
    {
        lines = {
            "# Editor Modes",
            "",
            "NORMAL MODE:",
            "  Default mode for navigation and commands.",
            "  Press `ESC` to return to normal mode from any other mode.",
            "",
            "INSERT MODE:",
            "  `i`     - Insert before cursor",
            "  `I`     - Insert at beginning of line",
            "  `a`     - Append after cursor",
            "  `A`     - Append at end of line",
            "  `o`     - Open new line below",
            "  `O`     - Open new line above",
            "",
            "VISUAL MODE:",
            "  `v`     - Character-wise visual selection",
            "  `V`     - Line-wise visual selection",
            "  `Ctrl-v` - Block visual selection",
            "  `y`     - Yank (copy) selection",
            "  `d`     - Delete selection",
            "  `c`     - Change selection",
            "",
            "COMMAND MODE:",
            "  `:`     - Enter command mode",
            "  Type commands and press Enter to execute.",
            "  `Ctrl-k` - Previous command in history",
            "  `Ctrl-j` - Next command in history",
            "  `Ctrl-f` - Fuzzy history search",
            "  `:/...` - Search forward (regex)",
            "  `:?...` - Search backward (regex)",
            "  Command list shows and filters while you type",
            "",
            "SEARCH MODE:",
            "  `/`     - Search forward",
            "  `?`     - Search backward",
            "  `n`     - Next match",
            "  `N`     - Previous match",
            "",
            "FILE BROWSER MODE:",
            "  `Space-e` - Open file browser",
            "  `j/k`     - Navigate up/down",
            "  `Enter`   - Open file/directory",
            "  `h` or `-`  - Go to parent directory",
            "  `.`       - Toggle hidden files",
            "  `i`       - Toggle gitignore",
            "  `:`       - Enter command mode in browser",
            "  `q`       - Quit file browser",
        };
    }
    else if(topic_lower == "navigation")
    {
        lines = {
            "# Navigation",
            "",
            "BASIC MOVEMENT:",
            "  `h`       - Move left",
            "  `j`       - Move down",
            "  `k`       - Move up",
            "  `l`       - Move right",
            "  `w`       - Move to next word",
            "  `b`       - Move to previous word",
            "  `e`       - Move to end of word",
            "",
            "LINE MOVEMENT:",
            "  `0`       - Move to beginning of line",
            "  `^`       - Move to first non-blank character",
            "  `$`       - Move to end of line",
            "  `gg`      - Go to first line",
            "  `G`       - Go to last line",
            "  `<n>G`    - Go to line n",
            "",
            "SCREEN MOVEMENT:",
            "  `Ctrl-f`  - Page down",
            "  `Ctrl-b`  - Page up",
            "  `Ctrl-d`  - Half page down",
            "  `Ctrl-u`  - Half page up",
            "  `zz`      - Center screen on cursor",
            "",
            "FILE NAVIGATION:",
            "  `Ctrl-p`  - Fuzzy file finder",
            "  `Space-e` - File browser",
            "  `Space-b` - Buffer browser",
            "  `Space-s` - Grep search",
            "",
            "MARKS & JUMPS:",
            "  `m{a-z}`   - Set mark",
            "  `'`/`` ` `` + {a-z} - Jump to mark",
            "  `Ctrl-o`   - Jump back",
            "  `Ctrl-i`   - Jump forward",
        };
    }
    else if(topic_lower == "editing")
    {
        lines = {
            "# Editing",
            "",
            "INSERT/APPEND:",
            "  `i`     - Insert before cursor",
            "  `I`     - Insert at line start",
            "  `a`     - Append after cursor",
            "  `A`     - Append at line end",
            "  `o`     - New line below",
            "  `O`     - New line above",
            "",
            "CHANGE/DELETE:",
            "  `x`     - Delete char at cursor",
            "  `X`     - Delete char before cursor",
            "  `s`     - Substitute char (enter insert)",
            "  `S`     - Substitute line (enter insert)",
            "  `C`     - Change to end of line",
            "  `D`     - Delete to end of line",
            "  `J`     - Join lines",
            "  `r`     - Replace single char",
            "  `R`     - Replace mode",
            "  `~`     - Toggle case",
            "",
            "YANK/PASTE:",
            "  `y`     - Yank selection (visual)",
            "  `Y`     - Yank line",
            "  `p`     - Paste after cursor",
            "  `P`     - Paste before cursor",
            "",
            "UNDO/REDO:",
            "  `u`       - Undo",
            "  `Ctrl-r`  - Redo",
            "",
            "REPEAT:",
            "  `.`     - Repeat last change",
        };
    }
    else if(topic_lower == "files")
    {
        lines = {
            "# Files",
            "",
            "COMMANDS:",
            "  `:w`        - Save file",
            "  `:w <file>` - Save as",
            "  `:q`        - Quit (fails if unsaved)",
            "  `:q!`       - Force quit",
            "  `:wq`/`:x`  - Save and quit",
            "",
            "NORMAL MODE:",
            "  `:Ex`/`:Explore` - File browser",
            "  `Space-e`        - File browser (leader)",
            "  `gf`             - Go to file under cursor",
            "  `Space-h`        - Alternate file (header/source)",
            "",
            "FILE BROWSER:",
            "  `Enter` - Open file/directory",
            "  `h`/`-` - Parent directory",
            "  `.`     - Toggle hidden files",
            "  `i`     - Toggle gitignore",
            "  `:`     - Command mode",
            "  `q`     - Quit browser",
        };
    }
    else if(topic_lower == "buffers")
    {
        lines = {
            "# Buffers",
            "",
            "COMMANDS:",
            "  `:ls`/`:buffers` - List buffers",
            "  `:b <n>`         - Switch to buffer n",
            "  `:bn`/`:bnext`   - Next buffer",
            "  `:bp`/`:bprev`   - Previous buffer",
            "  `:bd`/`:bdelete` - Delete buffer",
            "  `:bd!`           - Force delete buffer",
            "  `:enew`          - New buffer",
            "",
            "NORMAL MODE:",
            "  `Space-b`     - Buffer browser",
            "  `Ctrl-m h/k`  - Previous buffer",
            "  `Ctrl-m l/j`  - Next buffer",
            "  `Ctrl-^`      - Alternate buffer",
            "",
            "BUFFER BROWSER:",
            "  `j/k`    - Navigate",
            "  `Enter`  - Open",
            "  `q`      - Quit",
        };
    }
    else if(topic_lower == "search")
    {
        lines = {
            "# Search",
            "",
            "IN-BUFFER:",
            "  `/`      - Search forward (regex)",
            "  `?`      - Search backward (regex)",
            "  `n`      - Next match",
            "  `N`      - Previous match",
            "  `*`      - Search word under cursor (forward)",
            "  `#`      - Search word under cursor (backward)",
            "  `Space-n` - Clear search highlights",
            "",
            "PROJECT:",
            "  `Space-s` - Grep search",
            "  `/` in normal mode opens grep search mode",
            "  `:/...`   - Regex search forward (command mode)",
            "  `:? ...`  - Regex search backward (command mode)",
        };
    }
    else if(topic_lower == "clipboard")
    {
        lines = {
            "# Clipboard Operations",
            "",
            "YANK (COPY):",
            "  `yy`      - Yank current line",
            "  `<n>yy`   - Yank n lines",
            "  `yw`      - Yank word",
            "  `y$`      - Yank to end of line",
            "  `v<move>y` - Yank visual selection",
            "  `V<move>y` - Yank visual line selection",
            "",
            "PASTE:",
            "  `p`       - Paste after cursor",
            "  `P`       - Paste before cursor",
            "",
            "SYSTEM CLIPBOARD:",
            "  By default, `useSystemClipboard` is enabled.",
            "  All yank operations automatically copy to system clipboard.",
            "  Paste operations fall back to system clipboard if internal",
            "  buffer is empty.",
            "",
            "  This allows seamless integration with other applications:",
            "  - Yank in uvim → Paste in terminal or other apps",
            "  - Copy in other apps → Paste in uvim",
            "",
            "DELETE (CUT):",
            "  `dd`      - Delete (cut) current line",
            "  `<n>dd`   - Delete n lines",
            "  `dw`      - Delete word",
            "  `d$`      - Delete to end of line",
        };
    }
    else
    {
        // Unknown topic - show available topics
        lines = {
            "# Unknown topic: " + helpTopic,
            "",
            "Available help topics:",
            "  `:help commands`",
            "  `:help modes`",
            "  `:help navigation`",
            "  `:help editing`",
            "  `:help files`",
            "  `:help buffers`",
            "  `:help search`",
            "  `:help clipboard`",
            "  `:help git`",
            "",
            "Type `:help` to see the main help page.",
        };
    }
}

std::optional<ModeState> HelpMode::executeCommand(ModeContext& ctx,
                                                  std::string_view commandLine)
{
    return dispatchCommandLine(
        ctx, commandLine,
        [&](ModeContext& ctx, const ParsedCommand& command,
            std::optional<ModeState>& nextState) -> bool
        {
            // Handle :help command to navigate to different topics
            if(command.cmd == "help" || command.cmd == "h")
            {
                std::string newTopic = command.args.empty() ? "" : command.args;
                topic = newTopic;
                scrollOffset = 0;
                loadHelpContent(topic);
                ctx.setStatusMessage("Help: " +
                                     (topic.empty() ? "index" : topic));
                ctx.requestFullRedraw();
                return true;
            }

            // Handle :q to exit help
            if(command.cmd == "q" || command.cmd == "q!")
            {
                if(!previousFile.empty())
                {
                    ctx.openFile(std::string_view(previousFile));
                    nextState = NormalMode{};
                }
                else
                {
                    nextState = WelcomeMode{};
                }
                return true;
            }

            // Unknown command
            ctx.setStatusMessage("Unknown command: :" + command.cmd);
            return true;
        });
}
