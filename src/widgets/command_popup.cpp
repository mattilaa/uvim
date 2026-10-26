#include "widgets/command_popup.h"
#include "ascii.h"

#include "terminal.h"
#include "text_utils.h"
#include "theme.h"
#include "token_type.h"
#include <algorithm>
#include <string_view>

namespace widgets
{
std::string_view commandDocumentation(std::string_view cmd)
{
    while(!cmd.empty() && (cmd.back() == ' ' || cmd.back() == '\t'))
        cmd.remove_suffix(1);

    if(cmd == "--split")
        return "Browse source and assembly side by side with linked highlights";

    if(cmd == "-O2")
        return "Optimize for speed (recommended starting point)";
    if(cmd == "%%s/")
        return "Replace across project files; stage buffers, then :wa to save";
    if(cmd == "s/" || cmd == "%s/" || cmd == "substitute/")
        return "Replace text: [range]s/pattern/replacement/[gcpineI]";
    if(cmd == "-O0")
        return "Disable optimization for easier source comparison";
    if(cmd == "-O1")
        return "Basic optimization";
    if(cmd == "-O3")
        return "More aggressive speed optimization";
    if(cmd == "-Os")
        return "Optimize for code size";
    if(cmd == "-Og")
        return "Optimize while keeping debugging practical";
    if(cmd == "-g")
        return "Include debug and source-line information";
    if(cmd == "-fno-inline")
        return "Keep function calls instead of inlining";
    if(cmd == "-fverbose-asm")
        return "Add compiler comments to assembly";
    if(cmd == "-masm=intel")
        return "Intel assembly syntax (x86 targets only)";
    if(cmd == "-masm=att")
        return "AT&T assembly syntax (x86 targets only)";
    if(cmd == "-march=native")
        return "Optimize for this machine's CPU";
    if(cmd == "-Wall")
        return "Enable common warnings";
    if(cmd == "-Wextra")
        return "Enable extra warnings";
    if(cmd == "-std=c++20")
        return "Use the C++20 language standard";
    if(cmd == "-std=c++23")
        return "Use the C++23 language standard";
    if(cmd == "-std=c11")
        return "Use the C11 language standard";
    if(cmd == "-I")
        return "Add include directory (type its path next)";
    if(cmd == "-D")
        return "Define a macro (type NAME=value next)";
    if(cmd == "--compiler clang")
        return "Use Clang to generate assembly";
    if(cmd == "--compiler mlang")
        return "Use the Mlang compiler to generate assembly from .mla source";
    if(cmd == "-Oz")
        return "Optimize aggressively for small code size";
    if(cmd.starts_with("--target-arch "))
        return "Select the Mlang target architecture (x86, x64, or aarch64)";
    if(cmd == "-Wno-unwrap")
        return "Suppress Mlang result.unwrap() warnings";
    if(cmd == "-Wno-colon-if" || cmd == "-Wno-colon-while")
        return "Suppress Mlang plain-colon control-flow warnings";
    if(cmd == "--compiler gcc")
        return "Use GCC to generate assembly";
    if(cmd == "--raw")
        return "Keep directives and full compiler output";

#ifdef UVIM_ENABLE_DEBUGGER
    if(cmd == "dbgrun")
        return "Debug an executable: dbgrun [--backend lldb|gdb|mladbg] BINARY "
               "[ARGS]";
    if(cmd == "dbgattach")
        return "Attach debugger: dbgattach [--backend lldb|gdb|mladbg] PID";
#endif
    if(cmd == "palette")
        return "Command palette: search and run commands";
    if(cmd == "functions")
        return "Browse function signatures in the current buffer";
    if(cmd == "q" || cmd == "quit")
        return "Quit uVim";
    if(cmd == "q!" || cmd == "qa!" || cmd == "qall!")
        return "Force quit without saving";
    if(cmd == "qa" || cmd == "qall")
        return "Quit all windows";
    if(cmd == "w" || cmd == "write")
        return "Write current buffer";
    if(cmd == "wq" || cmd == "x" || cmd == "qw")
        return "Write and quit";
    if(cmd == "qw!" || cmd == "wqa!" || cmd == "wqall!")
        return "Force write all buffers and quit";
    if(cmd == "wa" || cmd == "wall")
        return "Write all modified buffers";
    if(cmd == "wa!")
        return "Force write all modified buffers";
    if(cmd == "wqa" || cmd == "wqall" || cmd == "xa")
        return "Write all buffers and quit";
    if(cmd == "e" || cmd == "edit")
        return "Open file";
    if(cmd == "e%" || cmd == "edit%")
        return "Reload current file";
    if(cmd == "e!%" || cmd == "edit!%")
        return "Force reload current file";
    if(cmd == "enew")
        return "Create empty buffer";
    if(cmd == "vnew")
        return "Open vertical split";
    if(cmd == "bn" || cmd == "bnext")
        return "Next buffer";
    if(cmd == "bp" || cmd == "bprev" || cmd == "bprevious")
        return "Previous buffer";
    if(cmd == "bd" || cmd == "bdelete")
        return "Close current buffer";
    if(cmd == "bd!")
        return "Force close current buffer";
    if(cmd == "b" || cmd == "buffer" || cmd.rfind("b ", 0) == 0 ||
       cmd.rfind("buffer ", 0) == 0)
        return "Switch to buffer";
    if(cmd == "ls" || cmd == "buffers")
        return "List open buffers";
    if(cmd == "hs")
        return "Split horizontally";
    if(cmd == "vs")
        return "Split vertically";
    if(cmd == "se")
        return "Split file browser horizontally";
    if(cmd == "ve")
        return "Split file browser vertically";
    if(cmd == "sc")
        return "Close active split pane";
    if(cmd == "only")
        return "Close other splits";
    if(cmd == "tabnew")
        return "Open new tab";
    if(cmd == "tabe")
        return "Open file in new tab";
    if(cmd == "tabn" || cmd == "tabnext")
        return "Next tab";
    if(cmd == "tabp" || cmd == "tabprev")
        return "Previous tab";
    if(cmd == "tabc" || cmd == "tabclose")
        return "Close current tab";
    if(cmd == "set")
        return "Configure editor options";
    if(cmd.rfind("set ", 0) == 0)
        return "Set option value";
    if(cmd == "format" || cmd == "fmt")
        return "Format current buffer";
    if(cmd == "syntax")
        return "Syntax highlighting options";
    if(cmd == "noh" || cmd == "nohlsearch")
        return "Clear search highlights";
    if(cmd == "lspinfo")
        return "Show LSP status and clients";
    if(cmd == "showchanges")
        return "Show unsaved changes of the current buffer as a diff";
    if(cmd == "toolinfo")
        return "Show external tool status";
    if(cmd == "tasks")
        return "Show background task queue and progress";
    if(cmd == "asm" || cmd == "asm .")
        return "Browse assembly of current C/C++ buffer (Clang/GCC)";
    if(cmd == "emitasm")
        return "Emit compact assembly for current C/C++ buffer";
    if(cmd == "emitasm --raw")
        return "Emit clang's full assembler output";
    if(cmd == "emoji" || cmd == "em")
        return "Open emoji picker";
    if(cmd == "glyphselect")
        return "Open glyph selector";
#ifdef UVIM_ENABLE_COLOR_TOOLS
    if(cmd == "ansitools")
        return "Insert common ANSI control escape code";
    if(cmd == "colorpicker")
        return "Insert ANSI color escape code";
    if(cmd == "colorselect")
        return "Select or edit RGB ANSI color style";
#endif
    if(cmd == "help" || cmd == "h")
        return "Open help";
    if(cmd.rfind("help ", 0) == 0 || cmd.rfind("h ", 0) == 0)
        return "Open help topic";
    if(cmd == "cd")
        return "Change working directory";
    if(cmd == "cdr")
        return "Change directory to project root";
    if(cmd == "pwd")
        return "Show working directory";
    if(cmd == "rfs")
        return "Refresh fuzzy find and grep file cache";
    if(cmd == "loc" || cmd == "loc!" || cmd == "loc%" || cmd == "loctotal")
        return "Count lines of code";
    if(cmd == "git stage")
        return "Open interactive git stage";
    if(cmd == "git add")
        return "Stage current buffer";
    if(cmd == "git blame")
        return "Toggle git blame gutter";
    if(cmd == "git log")
        return "Browse commit log";
    if(cmd == "git prettylog")
        return "Browse tig-like split commit log";
    if(cmd == "git diff")
        return "Show repository diff";
    if(cmd == "git commit")
        return "Commit staged files";
    if(cmd == "git fixup")
        return "Create fixup commit from staged files";
    if(cmd == "git stash")
        return "Stash local changes";
    if(cmd == "git stash pop")
        return "Restore latest stash";
    if(cmd == "delete" || cmd == "d" || cmd == "rm")
        return "Delete selected file or directory";
    if(cmd == "rename" || cmd == "r" || cmd == "mv")
        return "Rename selected file or directory";
    if(cmd == "mkdir" || cmd == "md")
        return "Create directory";
    if(cmd == "touch" || cmd == "new")
        return "Create file";
    if(cmd == "run")
        return "Run shell command in browser directory";
    if(cmd == "?")
        return "Show file browser help";
    return "";
}

namespace
{

static std::string truncate_to_width(std::string text, int width)
{
    while(text_utils::displayWidth(text) > width && !text.empty())
        text.resize(
            text_utils::prevUtf8CharStart(text, static_cast<int>(text.size())));
    return text;
}

static std::string format_command_line(std::string_view cmd, int cmdColWidth)
{
    std::string line(cmd);
    std::string_view doc = commandDocumentation(cmd);
    if(doc.empty())
        return line;

    int currentW = text_utils::displayWidth(line);
    int pad = std::max(3, cmdColWidth - currentW + 3);
    line.append((size_t)pad, ' ');
    line.append(doc.data(), doc.size());
    return line;
}

static void build_command_line_parts(std::string_view cmdText, int cmdColWidth,
                                     int innerW, std::string& cmdPart,
                                     std::string& gapPart, std::string& docPart)
{
    cmdPart.assign(cmdText.data(), cmdText.size());
    gapPart.clear();
    docPart.clear();

    std::string_view doc = commandDocumentation(cmdText);
    if(doc.empty())
    {
        if(text_utils::displayWidth(cmdPart) > innerW)
        {
            cmdPart = truncate_to_width(cmdPart, std::max(1, innerW - 3));
            cmdPart += "...";
        }
        return;
    }

    int cmdW = text_utils::displayWidth(cmdPart);
    int gapW = std::max(3, cmdColWidth - cmdW + 3);
    if(cmdW + gapW >= innerW)
    {
        cmdPart = truncate_to_width(cmdPart, std::max(1, innerW - 3));
        cmdPart += "...";
        return;
    }

    gapPart.assign((size_t)gapW, ' ');
    docPart.assign(doc.data(), doc.size());

    int docMax = innerW - cmdW - gapW;
    if(text_utils::displayWidth(docPart) > docMax)
    {
        docPart = truncate_to_width(docPart, std::max(1, docMax - 3));
        docPart += "...";
    }
}
} // namespace

void drawCommandPopup(std::string& output, const CommandPopupView& view)
{
    const PopupFrameView& frame = view.frame;
    output += frame.theme.baseFg();

    int rows =
        std::min(std::max(1, view.maxRows), std::max(1, frame.screenRows - 2));
    if(rows <= 0)
        return;

    int commandColWidth = 0;
    int maxContent = 0;
    if(!view.entries.empty())
    {
        for(const auto& entry : view.entries)
            commandColWidth =
                std::max(commandColWidth, text_utils::displayWidth(entry));
        commandColWidth = std::clamp(commandColWidth, 8, 24);
        for(const auto& entry : view.entries)
        {
            std::string line = format_command_line(entry, commandColWidth);
            maxContent = std::max(maxContent, text_utils::displayWidth(line));
        }
    }
    else
    {
        maxContent = text_utils::displayWidth(view.emptyMessage);
    }
    if(maxContent <= 0)
        maxContent = text_utils::displayWidth("No matches");

    maxContent = std::max(maxContent, text_utils::displayWidth(view.title));
    int innerW = std::max(48, maxContent);
    int totalW = innerW + 4;
    if(totalW > frame.screenCols)
    {
        totalW = frame.screenCols;
        innerW = std::max(4, totalW - 4);
    }

    int totalH = rows + 2;
    const PopupPlacement placement = placeBottomLeftPopup(
        frame.screenRows, frame.screenCols, totalW, totalH);
    int top = placement.top;
    int left = placement.left;

    auto moveTo = [&](int r, int c) { output += Terminal::cursorPos(r, c); };

    moveTo(top, left);
    text_utils::appendU8(output, ascii::BOX_ROUNDED_TOP_LEFT);
    const auto title =
        view.title.empty()
            ? std::string{}
            : truncate_to_width(" " + std::string(view.title) + " ",
                                innerW + 2);
    output += title;
    text_utils::appendUtf8Repeat(
        output, ascii::BOX_LIGHT_HORIZONTAL,
        std::max(0, innerW + 2 - text_utils::displayWidth(title)));
    text_utils::appendU8(output, ascii::BOX_ROUNDED_TOP_RIGHT);

    for(int i = 0; i < rows; ++i)
    {
        moveTo(top + 1 + i, left);
        text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL_RIGHT_PAD);

        std::string line;
        std::string cmdPart;
        std::string gapPart;
        std::string docPart;
        if(view.filtered.empty() && i == 0)
        {
            line = std::string(view.emptyMessage);
        }
        else if(!view.filtered.empty())
        {
            int visibleIndex = i + view.offset;
            if(visibleIndex >= 0 && visibleIndex < (int)view.filtered.size())
            {
                int idx = view.filtered[visibleIndex];
                if(idx >= 0 && idx < (int)view.entries.size())
                {
                    if(idx < static_cast<int>(view.rowSpans.size()) &&
                       !view.rowSpans[idx].empty())
                        line = view.entries[idx];
                    else
                    {
                        build_command_line_parts(view.entries[idx],
                                                 commandColWidth, innerW,
                                                 cmdPart, gapPart, docPart);
                        line = cmdPart + gapPart + docPart;
                    }
                }
            }
        }

        if(text_utils::displayWidth(line) > innerW)
        {
            line = truncate_to_width(line, std::max(1, innerW - 3));
            line += "...";
        }

        const int visibleIndex = i + view.offset;
        const int entryIndex =
            visibleIndex >= 0 &&
                    visibleIndex < static_cast<int>(view.filtered.size())
                ? view.filtered[visibleIndex]
                : -1;
        const auto rowColor =
            entryIndex >= 0 &&
                    entryIndex < static_cast<int>(view.rowColors.size())
                ? std::string_view(view.rowColors[entryIndex])
                : std::string_view{};
        const bool current = !view.filtered.empty() &&
                             (i + view.offset) < (int)view.filtered.size() &&
                             (i + view.offset) == view.cursor;
        const auto appendStyled = [&]()
        {
            size_t position = 0;
            if(entryIndex >= 0 &&
               entryIndex < static_cast<int>(view.rowSpans.size()))
                for(const auto& span : view.rowSpans[entryIndex])
                {
                    const auto start = std::min(line.size(), span.start);
                    const auto end =
                        start + std::min(line.size() - start, span.length);
                    if(start < position || end <= start)
                        continue;
                    output.append(line, position, start - position);
                    output += span.color;
                    output.append(line, start, end - start);
                    output += rowColor.empty() ? frame.theme.baseFg()
                                               : std::string(rowColor);
                    position = end;
                }
            output.append(line, position, line.size() - position);
        };
        if(current)
        {
            output += frame.theme.selection();
            output += rowColor;
            appendStyled();
            output += frame.theme.reset();
        }
        else
        {
            if(!rowColor.empty() ||
               (entryIndex >= 0 &&
                entryIndex < static_cast<int>(view.rowSpans.size()) &&
                !view.rowSpans[entryIndex].empty()))
            {
                output += rowColor;
                appendStyled();
            }
            else if(!cmdPart.empty())
            {
                output += frame.theme.uiAccent();
                output += cmdPart;
                output += frame.theme.baseFg();
                output += gapPart;
                if(!docPart.empty())
                {
                    output += frame.theme.uiDim();
                    output += docPart;
                }
            }
            else
            {
                output += frame.theme.syntax(TOKEN_FUNCTION);
                output += line;
            }
            output += frame.theme.reset();
        }

        int pad = innerW - text_utils::displayWidth(line);
        if(current && !rowColor.empty())
            output += frame.theme.selection();
        if(pad > 0)
            output.append(pad, ' ');
        if(current && !rowColor.empty())
            output += frame.theme.reset();
        text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL_LEFT_PAD);
    }

    moveTo(top + totalH - 1, left);
    text_utils::appendU8(output, ascii::BOX_ROUNDED_BOTTOM_LEFT);
    text_utils::appendUtf8Repeat(output, ascii::BOX_LIGHT_HORIZONTAL,
                                 innerW + 2);
    text_utils::appendU8(output, ascii::BOX_ROUNDED_BOTTOM_RIGHT);
}
} // namespace widgets
