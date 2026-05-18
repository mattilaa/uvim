#include "ascii.h"
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
    commandPrompt = ctx.commandPrompt();
    if(previousFile.empty() && ctx.hasCurrentBuffer() && ctx.hasFilename())
    {
        previousFile = std::string(ctx.currentFilename());
    }

    loadHelpContent(topic);
    ctx.requestFullRedraw();
}

void HelpMode::on_exit(ModeContext& /* ctx */) {}

std::optional<ModeState> HelpMode::handle(ModeContext& ctx,
                                          int key)
{
    int c = keyCode(key);
    bool needsRedraw = false;

    std::optional<ModeState> nextState;
    if(commandPrompt && commandPrompt->handle(
           ctx, c, [&](std::string_view commandLine)
           { return executeCommand(ctx, commandLine); }, nextState))
    {
        return nextState;
    }

    // ========================================================================
    // Exit
    // ========================================================================

    if(c == keyCode(control::ControlKey::ESC) || c == keyCode(typed::TypedKey::KEY_Q))
    {
        if(c == keyCode(control::ControlKey::ESC))
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

    if(c == keyCode(typed::TypedKey::KEY_J) || c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        int maxScroll = std::max(0, (int)lines.size() - (ctx.screenRows() - 3));
        if(scrollOffset < maxScroll)
        {
            scrollOffset++;
            needsRedraw = true;
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_K) || c == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        if(scrollOffset > 0)
        {
            scrollOffset--;
            needsRedraw = true;
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_CAP_G))
    {
        int newOffset =
            std::max(0, (int)lines.size() - (ctx.screenRows() - 3));
        if(newOffset != scrollOffset)
        {
            scrollOffset = newOffset;
            needsRedraw = true;
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_G))
    {
        int nextChar = Terminal::readKey();
        if(nextChar == keyCode(typed::TypedKey::KEY_G))
        {
            if(scrollOffset != 0)
            {
                scrollOffset = 0;
                needsRedraw = true;
            }
        }
    }
    else if(c == keyCode(control::ControlKey::CTRL_D))
    {
        int half = (ctx.screenRows() - 3) / 2;
        int oldOffset = scrollOffset;
        scrollOffset += half;
        int maxScroll = std::max(0, (int)lines.size() - (ctx.screenRows() - 3));
        if(scrollOffset > maxScroll)
            scrollOffset = maxScroll;
        if(scrollOffset != oldOffset)
            needsRedraw = true;
    }
    else if(c == keyCode(control::ControlKey::CTRL_U))
    {
        int half = (ctx.screenRows() - 3) / 2;
        int oldOffset = scrollOffset;
        scrollOffset -= half;
        if(scrollOffset < 0)
            scrollOffset = 0;
        if(scrollOffset != oldOffset)
            needsRedraw = true;
    }

    if(needsRedraw)
        ctx.requestFullRedraw();
    return std::nullopt;
}

void HelpMode::draw(Editor& editor) const
{
    const bool syncOutput = Terminal::useSynchronizedOutput();
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_BEGIN);

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
        if(!line.empty() && line[0] == keyCode(command::CommandKey::KEY_HASH))
        {
            // Topic title
            output += editor.theme.syntax(TOKEN_KEYWORD);
            output += Terminal::ESC_BOLD;
            output += line.substr(1); // Skip #
            output += editor.theme.reset();
        }
        else if(!line.empty() && std::isupper(line[0]) &&
                line.find(keyCode(command::CommandKey::KEY_COLON)) != std::string::npos && line.find(keyCode(command::CommandKey::KEY_COLON)) < 30)
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
                if(line[pos] == keyCode(command::CommandKey::KEY_COLON))
                {
                    // Find end of command
                    size_t end = pos + 1;
                    while(end < line.length() &&
                          (std::isalnum(line[end]) || line[end] == keyCode(command::CommandKey::KEY_EXCLAMATION) ||
                           line[end] == keyCode(command::CommandKey::KEY_QUESTION)))
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
                else if(line[pos] == keyCode(command::CommandKey::KEY_BACKTICK))
                {
                    // Code/command in backticks
                    size_t end = line.find(keyCode(command::CommandKey::KEY_BACKTICK), pos + 1);
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
        output.append(padding, keyCode(control::ControlKey::SPACE));
    }
    output += right;
    output += editor.theme.reset();

    // Message line
    output += Terminal::NEWLINE_CLEAR;
    if(commandPrompt && commandPrompt->isActive())
    {
        output += ":";
        output += commandPrompt->getInput();
    }
    else if(!editor.statusMessage.empty())
    {
        output += editor.statusMessage.substr(
            0,
            std::min((size_t)editor.screenCols, editor.statusMessage.length()));
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
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_END);
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
            "  `:help commands`    - List of all commands",
            "  `:help modes`       - Editor modes",
            "  `:help navigation`  - Moving around",
            "  `:help editing`     - Editing text",
            "  `:help files`       - File operations",
            "  `:help filebrowser` - File browser keys and commands",
            "  `:help run`         - :run command and output view",
            "  `:help windows`     - Splits and tabs",
            "  `:help buffers`     - Buffer management and tab bar",
            "  `:help search`      - Searching and replacing",
            "  `:help clipboard`   - Clipboard operations",
            "  `:help git`         - Git integrations",
            "  `:help lsp`         - LSP setup and troubleshooting",
            "  `:help diagnostics` - Diagnostic env vars",
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
            "  `Ctrl-x`   - Regex search view",
            "  `Space-e`  - File browser",
            "  `Ctrl-j/k` - Switch split pane (when split is open)",
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
            "  `:cdr`                - Change to project root",
            "  `:pwd`                - Print working directory",
            "  `:rfs`                - Refresh fuzzy find and grep file cache",
            "  `uvim .`              - Start in file browser (defers auto-LSP)",
            "",
            "WINDOWS / TABS:",
            "  `:sp`/`:split`/`:hs`/`:hsplit` - Horizontal split",
            "  `:vs`/`:vsplit`/`:vh`          - Vertical split",
            "  `:only`                        - Close other splits",
            "  `:tabnew`/`:tabe <file>`       - New tab / open in tab",
            "  `:tabc`/`:tabclose`            - Close current tab",
            "  `:tabn`/`:tabnext`             - Next tab",
            "  `:tabp`/`:tabprev`             - Previous tab",
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
            "  `:cdr`                - Change to project root",
            "  `:mkdir <name>`       - Create directory",
            "  `:touch <name>`       - Create file",
            "  `:delete` or `:d`     - Delete selected file",
            "  `:rename <name>`      - Rename selected file",
            "",
            "GIT:",
            "  `ga`      - Open git stage view",
            "  `gb`      - Toggle git blame gutter",
            "  `gj`      - Show commit diff for line under cursor in blame mode",
            "  `gbv`     - Show commit diff for line under cursor",
            "  `:git blame` - Toggle git blame gutter from command mode",
            "  `gl`      - Show git log (repo)",
            "  `glf`     - Show git log (current file)",
            "  `:git stage` - Open git staging view",
            "  `:git diff`  - Show repository diff",
            "  `:git commit` - Commit staged files",
            "  `:git fixup` - Fixup staged files into selected commit",
            "  Git stage: `j/k` move, `h/l` pan list, `d` toggle diff split",
            "  Git stage: `space` stage/unstage, `ctrl-j/k` scroll diff",
            "  Git stage: `ctrl-h/l` pan diff, `m` mark fixup, `f` fixup",
            "  Git fixup: `space` select commit, `y/n/p` confirm or patch",
            "  Git patch: `y` stage hunk, `n` skip hunk, `ctrl-j/k` next/prev",
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
            "  `:set nocommandline.messageprefix` - Hide keyCode(command::CommandKey::KEY_COLON) prefix for messages",
            "",
            "STARTUP FLAGS:",
            "  `--no-git-index`      - Disable git-backed fuzzy/grep indexing",
            "  `--no-gitignore`      - Disable .gitignore filtering",
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
            "  `gj`   - Show commit diff for line under cursor in blame mode",
            "  `gbv`  - Show commit diff for line under cursor",
            "  `:git blame` - Toggle git blame gutter from command mode",
            "",
            "GIT LOG:",
            "  `gl`   - Browse git log (repo)",
            "  `glf`  - Browse git log (current file)",
            "  Use `ctrl-j/k` to move, `enter` to open diff, `q` to quit",
            "",
            "GIT STAGE:",
            "  `ga`          - Open git stage view from normal or file browser",
            "  `:git stage` - Browse status, stage/unstage with `space`",
            "  `:git diff`  - View repo diff",
            "  `:git commit` - Commit staged files",
            "  `:git fixup` - Fixup staged files into selected commit",
            "  `j/k`        - Move between file rows",
            "  `h/l`        - Scroll the left status pane horizontally",
            "  `d`          - Toggle split diff preview",
            "  `ctrl-j/k`   - Scroll split diff vertically",
            "  `ctrl-h/l`   - Scroll split diff horizontally",
            "  `m`          - Mark/unmark file for fixup",
            "  `f`          - Fixup staged/marked files to a commit",
            "  `enter`      - Open file",
            "",
            "GIT FIXUP:",
            "  `space/enter` - Select commit to fixup",
            "  `y/n/p`   - Confirm (y), cancel (n), or patch mode (p)",
            "",
            "GIT PATCH:",
            "  `y`       - Stage current hunk",
            "  `n`       - Skip current hunk",
            "  `ctrl-j/k` - Next/prev hunk",
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
    else if(topic_lower == "gb")
    {
        lines = {
            "# gb",
            "",
            "`gb` toggles the git blame gutter for the current file.",
            "",
            "Related:",
            "  `gj`         - Open commit diff for the line under cursor",
            "  `gbv`        - Open commit diff for the line under cursor",
            "  `:git blame` - Toggle git blame gutter from command mode",
        };
    }
    else if(topic_lower == "ga")
    {
        lines = {
            "# ga",
            "",
            "`ga` opens the git stage view from normal mode or the file browser.",
            "",
            "Inside the git stage view:",
            "  `j/k`       - Move between staged/unstaged/untracked file rows",
            "  `h/l`       - Scroll the left status pane horizontally",
            "  `d`         - Toggle split diff preview on the right",
            "  `ctrl-j/k`  - Scroll the diff preview vertically",
            "  `ctrl-h/l`  - Scroll the diff preview horizontally",
            "  `space`     - Stage or unstage the selected file",
            "  `enter`     - Open the selected file",
            "  `m`         - Mark file for fixup",
            "  `f`         - Start fixup for staged/marked files",
            "  `q`         - Close the git stage view",
            "",
            "Related:",
            "  `:git stage` - Open the same view from command mode",
            "  `:git fixup` - Start fixup from staged files",
            "  `:help git`  - All git integrations",
        };
    }
    else if(topic_lower == "gj")
    {
        lines = {
            "# gj",
            "",
            "`gj` opens the commit diff for the line under cursor when the",
            "git blame gutter is visible.",
            "",
            "Inside the commit view:",
            "  `j/k`   - Scroll",
            "  `/` `?` - Search",
            "  `q`     - Quit",
            "",
            "Related:",
            "  `gb`  - Toggle git blame gutter",
            "  `gbv` - Open commit diff for the line under cursor",
        };
    }
    else if(topic_lower == "gbv")
    {
        lines = {
            "# gbv",
            "",
            "`gbv` opens the commit diff for the line under cursor.",
            "",
            "Related:",
            "  `gb`         - Toggle git blame gutter",
            "  `gj`         - Open commit diff in blame mode",
            "  `:git blame` - Toggle git blame gutter from command mode",
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
            "  `ga`      - Open git stage view",
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
            "  `Ctrl-x`  - Regex search view",
            "  `Space-e` - File browser",
            "  `Space-b` - Buffer browser",
            "  `Ctrl-s`  - Grep search",
            "  `Space-/` - Grep search",
            "",
            "MARKS & JUMPS:",
            "  `m{a-z}`   - Set mark",
            "  `'`/`` ` `` + {a-z} - Jump to mark",
            "  `Ctrl-o`   - Jump back",
            "  `Ctrl-i`   - Jump forward",
            "",
            "CODE NAVIGATION:",
            "  `gd`       - Go to definition",
            "  `gr`       - Find references",
            "  `gf`       - Open file under cursor",
            "  `ga`       - Open git stage view",
            "",
            "SPLITS / WINDOWS:",
            "  `Ctrl-j`/`Ctrl-k` - Switch active split pane",
            "  `Ctrl-h`/`Ctrl-l` - Prev/next buffer (split-aware)",
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
            "  `rn`    - Rename word under cursor (replace in file)",
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
            "  `gh`             - Alternate file (header/source)",
            "  `:vs`/`:split`   - Open split view",
            "  `Ctrl-j/k`       - Switch active split pane",
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
            "# Buffers And Tab Bar",
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
            "  `Ctrl-w`          - Buffer browser",
            "  `bd`              - Close current buffer",
            "  `Ctrl-h`/`Ctrl-l` - Prev / next buffer",
            "  `Space-h`         - Move current buffer LEFT in tab bar",
            "  `Space-l`         - Move current buffer RIGHT in tab bar",
            "  `Ctrl-Shift-H`    - Same as Space-h (modifyOtherKeys-aware",
            "                     terminals only; falls back to Ctrl-h)",
            "  `Ctrl-Shift-L`    - Same as Space-l",
            "  `Ctrl-^`          - Alternate buffer",
            "  `Ctrl-j`/`Ctrl-k` - Switch split pane (when split is open)",
            "",
            "TAB BAR:",
            "  Tabs show `N:filename` (N = buffer index, 1-based).",
            "  `:set tabnumbers`     - Show numbers (default)",
            "  `:set notabnumbers`   - Hide numbers",
            "  `:set tabnumbers?`    - Print current value",
            "  `:set tabnumbers=on`  - Same as `:set tabnumbers`",
            "  `:set tabnumbers=off` - Same as `:set notabnumbers`",
            "  `:set showtabs`/`noshowtabs` - Show/hide the tab bar",
            "  Config key: `editor.tabnumbers` / `editor.showtabs`",
            "",
            "BUFFER BROWSER:",
            "  `j/k`      - Navigate",
            "  `Enter`    - Open",
            "  `Ctrl-x`   - Close selected buffer",
            "  `Ctrl-Shift-x` - Close searched buffers",
            "  `q`        - Quit",
        };
    }
    else if(topic_lower == "windows" || topic_lower == "splits")
    {
        lines = {
            "# Windows And Splits",
            "",
            "COMMANDS:",
            "  `:sp`/`:split`/`:hs`/`:hsplit` - Horizontal split",
            "  `:vs`/`:vsplit`/`:vh`          - Vertical split",
            "  `:only`                        - Close all other splits",
            "",
            "PANE NAVIGATION (NORMAL MODE):",
            "  `Ctrl-j` or `Ctrl-k` - Switch to the other split pane",
            "  `Ctrl-h` or `Ctrl-l` - Previous/next buffer (pane-aware)",
            "",
            "TABS:",
            "  `:tabnew`            - New tab",
            "  `:tabe <file>`       - Open file in new tab",
            "  `:tabc`/`:tabclose`  - Close current tab",
            "  `:tabn`/`:tabnext`   - Next tab",
            "  `:tabp`/`:tabprev`   - Previous tab",
            "",
            "TIP:",
            "  In a vertical split, use `Ctrl-j`/`Ctrl-k` to jump to the",
            "  other side quickly.",
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
            "  `Ctrl-x`  - Regex search view for the current buffer",
            "  Regex view: `Ctrl-s` toggles current buffer / project files",
            "  Regex view: `Enter` opens the selected match",
            "  `Ctrl-s`  - Grep search",
            "  `Space-/` - Grep search",
            "  `:/...`   - Regex search forward (command mode)",
            "  `:? ...`  - Regex search backward (command mode)",
        };
    }
    else if(topic_lower == "regex")
    {
        lines = {
            "# Regex Search View",
            "",
            "NORMAL MODE:",
            "  `Ctrl-x` - Open regex search view",
            "",
            "PROMPT:",
            "  Type a regex pattern to list matching lines.",
            "  Matching text is highlighted in the result list.",
            "",
            "SCOPE:",
            "  Starts by searching the current buffer.",
            "  `Ctrl-s` toggles between current buffer and all project files.",
            "  Project-file search respects `.gitignore` when enabled.",
            "",
            "KEYS:",
            "  `Enter`    - Open selected match",
            "  `Esc`      - Cancel",
            "  `Ctrl-j/k` - Move through results",
            "  `Ctrl-d/u` - Half-page down/up",
            "  `Ctrl-w`   - Delete previous word in the prompt",
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
            std::string("  - Yank in uvim ") + ascii::utf8(ascii::RIGHT_ARROW) +
                " Paste in terminal or other apps",
            std::string("  - Copy in other apps ") + ascii::utf8(ascii::RIGHT_ARROW) +
                " Paste in uvim",
            "",
            "DELETE (CUT):",
            "  `dd`      - Delete (cut) current line",
            "  `<n>dd`   - Delete n lines",
            "  `dw`      - Delete word",
            "  `d$`      - Delete to end of line",
        };
    }
    else if(topic_lower == "filebrowser" || topic_lower == "browser" ||
            topic_lower == "explore" || topic_lower == "ex")
    {
        lines = {
            "# File Browser",
            "",
            "OPEN:",
            "  `Space-e`              - Open file browser at file's dir",
            "  `:Ex` / `:Explore`     - Open file browser",
            "  `uvim .`               - Start in browser (defers auto-LSP)",
            "",
            "NAVIGATION:",
            "  `j` / `k`              - Move down / up",
            "  `Enter` / `l` / right  - Open file or descend into dir",
            "  `h` / left / `-`       - Parent directory",
            "  `Ctrl-d` / `Ctrl-u`    - Half-page down / up",
            "  `gg` / `G`             - Top / bottom",
            "  `Ctrl-o` / `Tab`       - History back / forward",
            "  `cd`                   - chdir to current directory",
            "  `.`                    - Toggle hidden files",
            "  `Ctrl-g`               - Toggle .gitignore filtering",
            "  `r` / `Ctrl-l`         - Refresh",
            "  `q`                    - Quit browser",
            "",
            "SELECTION (multi-file):",
            "  `Space`                - Toggle current row's selection",
            "  `Shift-V`              - Start/stop visual-line selection.",
            "                           Exiting with V keeps the range; V",
            "                           again extends with another segment.",
            "  `Esc` (visual)         - Cancel current segment",
            "  `Esc Esc`              - Clear all selections + cut buffer",
            "  `Enter` (with sel.)    - Open every selected file as buffer",
            "                           (dirs skipped, alphabetical order)",
            "",
            "FILE OPERATIONS:",
            "  `n`                    - Create new file (nested paths OK)",
            "  `Shift-D`              - Create new directory (mkdir -p)",
            "  `d`                    - Delete selection (or cursor entry)",
            "  `y`                    - Yank selection into paste buffer",
            "  `m`                    - Cut selection into paste buffer",
            "  `p`                    - Paste buffer into current dir",
            "  `u` / `Ctrl-r`         - Undo / redo last file op",
            "  `Shift-R`              - Rename cursor entry",
            "",
            "COMMAND-MODE:",
            "  `:q`                   - Exit file browser",
            "  `:cd <path>`           - Change directory",
            "  `:cdr`                 - Jump to project root",
            "  `:pwd`                 - Print working directory",
            "  `:mkdir <name>`/`:md`  - Create directory",
            "  `:new <name>`/`:touch` - Create file (open/replace prompt",
            "                            if it already exists)",
            "  `:delete`/`:d`/`:rm`   - Delete cursor entry",
            "  `:rename <name>`       - Rename (also `:r` / `:mv`)",
            "  `:/<regex>`/`:?<re>`   - Regex match in listing.",
            "                           Ctrl-J/K cycle matches; Ctrl-Space",
            "                           or Ctrl-N toggles selection on all",
            "                           matches.",
            "  `:run <cmd>`           - Run shell cmd in CWD; opens output",
            "                           view. Tab after `run ` appends the",
            "                           selected files (sorted, quoted).",
            "                           See `:help run`.",
        };
    }
    else if(topic_lower == "run")
    {
        lines = {
            "# :run Command And Output View",
            "",
            "INVOKE FROM FILE BROWSER:",
            "  `:run <shell command>` - executes the command in the file",
            "    browser's current directory and opens its combined",
            "    stdout+stderr in a scrollable view.",
            "",
            "  Tab completion: with files selected in the browser, pressing",
            "  Tab on a `run ...` prompt appends the selected paths,",
            "  alphabetical and shell-quoted. So `:run zip a.zip` then Tab",
            "  becomes `:run zip a.zip 'foo bar.txt' baz.txt ...`.",
            "",
            "INSIDE THE OUTPUT VIEW:",
            "  `j` / `k`              - Cursor down / up",
            "  `Ctrl-d` / `Ctrl-u`    - Half-page down / up",
            "  `gg` / `G`             - Top / bottom",
            "  `Space`                - Toggle row selection",
            "  `Shift-V`              - Start/stop visual-line selection.",
            "                           Exiting with V keeps the range; V",
            "                           again extends. Same model as the",
            "                           file browser.",
            "  `Space` while visual   - Commit visual range and exit",
            "  `Esc` while visual     - Cancel current segment",
            "  `Esc Esc`              - Clear all selection",
            "  `y`                    - Yank selected rows (or cursor row",
            "                           if none) to system clipboard +",
            "                           yank buffer; selection preserved.",
            "  `/`                    - Incremental search; matches show",
            "                           with grey/black highlight.",
            "  `n` / `Shift-N`        - Next / previous match",
            "  `q`                    - Clear search highlight if active,",
            "                           else return to file browser.",
            "  `Esc`                  - Same as q at rest.",
            "",
            "Long lines wrap on screen but count as a single logical row",
            "for cursor movement and selection.",
        };
    }
    else if(topic_lower == "diagnostics" || topic_lower == "diag" ||
            topic_lower == "keylog")
    {
        lines = {
            "# Diagnostics",
            "",
            "ENVIRONMENT VARIABLES:",
            "  `UVIM_KEYLOG=<path>`   - Log every byte read from stdin to",
            "                           `<path>` (append mode). Useful to",
            "                           see what your terminal sends for a",
            "                           given key combination.",
            "                           ESC bytes are written as `\\e`,",
            "                           printable bytes as themselves,",
            "                           anything else as `<HH>`.",
            "  `UVIM_DISABLE_SYNC_OUTPUT` - When set/non-empty, disables",
            "                           the synchronized-update terminal",
            "                           protocol (default-on inside tmux).",
            "",
            "EXAMPLE:",
            "  `UVIM_KEYLOG=/tmp/uvim_keys.log uvim somefile`",
            "  Press the key, quit, then `cat /tmp/uvim_keys.log`.",
        };
    }
    else if(topic_lower == "lsp" || topic_lower == "lsp-install" ||
            topic_lower == "lspinfo")
    {
        lines = {
            "# LSP Setup And Troubleshooting",
            "",
            "CHECK STATUS:",
            "  `:lspinfo` - Shows ACTIVE/ON/OFF and missing runtime/binary",
            "               details (for example missing `node`)",
            "",
            "REQUIRED BINARIES:",
            "  C/C++: `clangd`",
            "  Python: `pyright-langserver` (or `pylsp`)",
            "  Mlang: `mlangd-mla` (or `mlangd` fallback)",
            "  HTML: `vscode-html-language-server` + `node`",
            "  CSS: `vscode-css-language-server` + `node`",
            "  JSON: `vscode-json-language-server` + `node`",
            "  TS/JS: `typescript-language-server` + `node`",
            "",
            "INSTALL ON MACOS:",
            "  `brew install llvm node pyright`",
            "  `npm install -g vscode-langservers-extracted "
            "typescript typescript-language-server`",
            "  `pip install 'python-lsp-server[all]'`",
            "",
            "INSTALL ON LINUX (APT EXAMPLE):",
            "  `sudo apt install clangd nodejs npm`",
            "  `npm install -g vscode-langservers-extracted "
            "typescript typescript-language-server pyright`",
            "  `pip install 'python-lsp-server[all]'`",
            "",
            "INSTALL ON WINDOWS:",
            "  `winget install OpenJS.NodeJS`",
            "  `winget install LLVM.LLVM`",
            "  `npm install -g vscode-langservers-extracted "
            "typescript typescript-language-server pyright`",
            "  `pip install \"python-lsp-server[all]\"`",
            "",
            "NOTES:",
            "  - If npm global binaries are not found, add npm's bin directory",
            "    to PATH and restart your shell/editor.",
            "  - In uvim config, set explicit `*LspPath` values if needed.",
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
            "  `:help filebrowser`",
            "  `:help run`",
            "  `:help windows`",
            "  `:help buffers`",
            "  `:help search`",
            "  `:help clipboard`",
            "  `:help git`",
            "  `:help ga`",
            "  `:help gb`",
            "  `:help gj`",
            "  `:help gbv`",
            "  `:help lsp`",
            "  `:help diagnostics`",
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
