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
namespace
{
static std::string_view command_doc(std::string_view cmd)
{
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
    if(cmd == "wa" || cmd == "wall")
        return "Write all modified buffers";
    if(cmd == "wqa" || cmd == "wqall" || cmd == "xa")
        return "Write all buffers and quit";
    if(cmd == "e" || cmd == "edit")
        return "Open file";
    if(cmd == "e%" || cmd == "edit%")
        return "Reload current file";
    if(cmd == "new")
        return "Open horizontal split";
    if(cmd == "vnew")
        return "Open vertical split";
    if(cmd == "bn" || cmd == "bnext")
        return "Next buffer";
    if(cmd == "bp" || cmd == "bprev")
        return "Previous buffer";
    if(cmd == "bd" || cmd == "bdelete")
        return "Close current buffer";
    if(cmd == "ls" || cmd == "buffers")
        return "List open buffers";
    if(cmd == "sp" || cmd == "split" || cmd == "hs" || cmd == "hsplit")
        return "Split horizontally";
    if(cmd == "vs" || cmd == "vsplit" || cmd == "vh")
        return "Split vertically";
    if(cmd == "only")
        return "Close other splits";
    if(cmd == "tabnew")
        return "Open new tab";
    if(cmd == "tabc" || cmd == "tabclose")
        return "Close current tab";
    if(cmd == "set")
        return "Configure editor options";
    if(cmd.rfind("set ", 0) == 0)
        return "Set option value";
    if(cmd == "syntax")
        return "Syntax highlighting options";
    if(cmd == "noh" || cmd == "nohlsearch")
        return "Clear search highlights";
    if(cmd == "lspinfo")
        return "Show LSP status and clients";
    if(cmd == "emitasm")
        return "Emit assembly for current C/C++ buffer";
    if(cmd == "emoji" || cmd == "em")
        return "Open emoji picker";
    if(cmd == "colorpicker")
        return "Insert ANSI color escape code";
    if(cmd == "colorpicker bg")
        return "Insert ANSI background color escape code";
    if(cmd == "colorselector")
        return "Select RGB ANSI color escape code";
    if(cmd == "colorselector bg")
        return "Select RGB ANSI background escape code";
    if(cmd == "help" || cmd == "h")
        return "Open help";
    if(cmd.rfind("help ", 0) == 0 || cmd.rfind("h ", 0) == 0)
        return "Open help topic";
    if(cmd == "cd")
        return "Change working directory";
    if(cmd == "cdr")
        return "Change directory to project root";
    if(cmd == "rfs")
        return "Refresh fuzzy find and grep file cache";
    if(cmd == "loc" || cmd == "loc!" || cmd == "loc%" || cmd == "loctotal")
        return "Count lines of code";
    if(cmd == "git stage")
        return "Open interactive git stage";
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
    return "";
}

static std::string truncate_to_width(std::string text, int width)
{
    while(text_utils::displayWidth(text) > width && !text.empty())
        text.pop_back();
    return text;
}

static std::string format_command_line(std::string_view cmd, int cmdColWidth)
{
    std::string line(cmd);
    std::string_view doc = command_doc(cmd);
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

    std::string_view doc = command_doc(cmdText);
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
    output += view.theme.baseFg();

    int rows = std::min(8, std::max(1, view.screenRows - 2));
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
        maxContent = text_utils::displayWidth("No matches");
    }
    if(maxContent <= 0)
        maxContent = text_utils::displayWidth("No matches");

    int innerW = std::max(48, maxContent);
    int totalW = innerW + 4;
    if(totalW > view.screenCols)
    {
        totalW = view.screenCols;
        innerW = std::max(4, totalW - 4);
    }

    int totalH = rows + 2;
    int top = view.screenRows - totalH + 1;
    if(top < 1)
        top = 1;
    int left = 2;
    if(left + totalW - 1 > view.screenCols)
        left = std::max(1, view.screenCols - totalW + 1);

    auto moveTo = [&](int r, int c) { output += Terminal::cursorPos(r, c); };

    moveTo(top, left);
    text_utils::appendU8(output, ascii::BOX_ROUNDED_TOP_LEFT);
    text_utils::appendUtf8Repeat(output, ascii::BOX_LIGHT_HORIZONTAL,
                                 innerW + 2);
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
            line = "No matches";
        }
        else if(!view.filtered.empty())
        {
            int visibleIndex = i + view.offset;
            if(visibleIndex >= 0 && visibleIndex < (int)view.filtered.size())
            {
                int idx = view.filtered[visibleIndex];
                if(idx >= 0 && idx < (int)view.entries.size())
                {
                    build_command_line_parts(view.entries[idx], commandColWidth,
                                             innerW, cmdPart, gapPart, docPart);
                    line = cmdPart + gapPart + docPart;
                }
            }
        }

        if(text_utils::displayWidth(line) > innerW)
        {
            line = truncate_to_width(line, std::max(1, innerW - 3));
            line += "...";
        }

        if(!view.filtered.empty() &&
           (i + view.offset) < (int)view.filtered.size() &&
           (i + view.offset) == view.cursor)
        {
            output += view.theme.selection();
            output.append(line);
            output += view.theme.reset();
        }
        else
        {
            if(!cmdPart.empty())
            {
                output += view.theme.uiAccent();
                output += cmdPart;
                output += view.theme.baseFg();
                output += gapPart;
                if(!docPart.empty())
                {
                    output += view.theme.uiDim();
                    output += docPart;
                }
            }
            else
            {
                output += view.theme.syntax(TOKEN_FUNCTION);
                output += line;
            }
            output += view.theme.reset();
        }

        int pad = innerW - text_utils::displayWidth(line);
        if(pad > 0)
            output.append(pad, ' ');
        text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL_LEFT_PAD);
    }

    moveTo(top + totalH - 1, left);
    text_utils::appendU8(output, ascii::BOX_ROUNDED_BOTTOM_LEFT);
    text_utils::appendUtf8Repeat(output, ascii::BOX_LIGHT_HORIZONTAL,
                                 innerW + 2);
    text_utils::appendU8(output, ascii::BOX_ROUNDED_BOTTOM_RIGHT);
}
} // namespace widgets
