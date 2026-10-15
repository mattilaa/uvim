#include "widgets/status_bar.h"

#include "text_utils.h"
#include "theme.h"
#include <algorithm>
#include <cstdio>

namespace widgets
{
namespace
{
// 1-based column of the progress slot: where the progress was drawn, or
// where it would go inside the padding (`reserve` columns + a space).
// -1: hosted here but no room.
int progressSlotColumn(int screenCols, int rightLen, int padding,
                       bool drawn, int reserve)
{
    if(drawn)
        return padding >= 0 ? screenCols - rightLen + 1 : -1;
    if(reserve > 0 && padding >= reserve + 1)
        return screenCols - rightLen - (reserve + 1) + 1;
    return -1;
}
} // namespace

void appendStatusBar(std::string& output, const StatusBarView& view)
{
    output += view.theme.statusBar();

    std::string status = " " + std::string(view.modeLabel) + " | ";
    if(view.bufferCount > 1)
    {
        status += "[" + std::to_string(view.currentBufferIndex + 1) + "/" +
                  std::to_string(view.bufferCount) + "] ";
    }

    int lineCount = std::max(1, view.lineCount);
    int lineNumber = std::clamp(view.cursorY + 1, 1, lineCount);
    int progress = std::clamp((lineNumber * 100) / lineCount, 0, 100);

    char rightStatusBuf[48];
    snprintf(rightStatusBuf, sizeof(rightStatusBuf), " %d%%/%d/%d ", progress,
             lineNumber, view.cursorX + 1);
    std::string rightStatus = rightStatusBuf;
    const int rightFieldWidth = 12;
    int rightStatusWidth = text_utils::displayWidth(rightStatus);
    if(rightStatusWidth < rightFieldWidth)
    {
        rightStatus.insert(0, rightFieldWidth - rightStatusWidth, ' ');
    }

    std::string searchInfo;
    if(!view.searchQuery.empty())
    {
        if(view.searchMatchCount > 0)
        {
            searchInfo = " [" + std::to_string(view.searchMatchIndex + 1) +
                         "/" + std::to_string(view.searchMatchCount) + "]";
        }
        else
        {
            searchInfo = " [No matches]";
        }
    }

    std::string lspInfo;
    if(!view.lspLabel.empty())
    {
        lspInfo = " " + view.theme.uiInfo() + "[" + std::string(view.lspLabel) +
                  "]" + view.theme.statusBar();
    }

    std::string rightBlock;
    if(!view.progress.empty())
        rightBlock = std::string(view.progress) + " ";
    rightBlock += searchInfo + lspInfo;
    if(!lspInfo.empty())
        rightBlock.append(std::max(0, view.lspGap), ' ');
    rightBlock += rightStatus;

    int rightLen = text_utils::displayWidth(rightBlock);
    int availableForFile = view.screenCols - status.length() - rightLen - 1;

    std::string displayName =
        view.filename.empty() ? "[No Name]" : std::string(view.filename);
    if(view.dirty)
        displayName += " [+]";

    if((int)displayName.length() > availableForFile && availableForFile > 4)
    {
        displayName = "..." + displayName.substr(displayName.length() -
                                                 availableForFile + 3);
    }

    status += displayName;
    output += status;

    int padding = view.screenCols - status.length() - rightLen;
    if(padding > 0)
        output.append(padding, ' ');
    if(view.progressColumn)
        *view.progressColumn = progressSlotColumn(
            view.screenCols, rightLen, padding, !view.progress.empty(),
            view.progressReserve);
    output += rightBlock;
    output += view.theme.reset();
}

void appendViewStatusBar(std::string& output, const ViewStatusBarView& view)
{
    output += view.theme.statusBar();

    std::string left = " " + std::string(view.modeLabel) + " | ";
    std::string context(view.context);

    std::string state;
    if(!view.state.empty())
    {
        state = " " + view.theme.uiWarning() + std::string(view.state) +
                view.theme.statusBar();
    }

    std::string rightBlock;
    if(!view.progress.empty())
        rightBlock = std::string(view.progress) + " ";
    for(const auto& flag : view.flags)
        rightBlock += view.theme.uiDim() + "[" + flag + "]" +
                      view.theme.statusBar() + " ";
    if(!view.toolLabel.empty())
    {
        rightBlock += view.theme.uiInfo() + "[" + std::string(view.toolLabel) +
                      "]" + view.theme.statusBar();
        rightBlock.append(std::max(0, view.toolGap), ' ');
    }
    if(view.count >= 0)
    {
        const int count = std::max(0, view.count);
        const int index = count == 0 ? 0 : std::clamp(view.index, 1, count);
        const int percent = count == 0 ? 0 : (index * 100) / count;
        std::string ruler = " " + std::to_string(percent) + "%/" +
                            std::to_string(index) + "/" +
                            std::to_string(count) + " ";
        const int rulerWidth = 12;
        const int width = text_utils::displayWidth(ruler);
        if(width < rulerWidth)
            ruler.insert(0, rulerWidth - width, ' ');
        rightBlock += ruler;
    }
    else
    {
        rightBlock += " ";
    }

    const int rightLen = text_utils::displayWidth(rightBlock);
    const int leftLen = text_utils::displayWidth(left);
    const int stateLen = text_utils::displayWidth(state);
    const int available = view.screenCols - leftLen - stateLen - rightLen - 1;
    if(text_utils::displayWidth(context) > available && available > 4)
        context = "..." + context.substr(context.size() - (available - 3));
    else if(available <= 4)
        context.clear();

    std::string status = left + context + state;
    output += status;
    const int padding =
        view.screenCols - text_utils::displayWidth(status) - rightLen;
    if(padding > 0)
        output.append(padding, ' ');
    if(view.progressColumn)
        *view.progressColumn = progressSlotColumn(
            view.screenCols, rightLen, padding, !view.progress.empty(),
            view.progressReserve);
    output += rightBlock;
    output += view.theme.reset();
}

void appendMessageBar(std::string& output, const MessageBarView& view)
{
    if(view.currentMode == COMMAND || view.currentMode == SEARCH_FORWARD ||
       view.currentMode == SEARCH_BACKWARD)
    {
        std::string prompt = std::string(view.commandBuffer);
        if(view.currentMode == SEARCH_FORWARD && prompt.empty())
            prompt = "/";
        else if(view.currentMode == SEARCH_BACKWARD && prompt.empty())
            prompt = "?";
        else if(view.currentMode == COMMAND && prompt.empty())
            prompt = ":";
        else if(view.currentMode == COMMAND && prompt.front() != ':')
            prompt.insert(prompt.begin(), ':');
        output += prompt;
        return;
    }

    if(view.showGitBlame && view.showGitBlameInfo && !view.blameLine.empty())
    {
        if(view.commandLineMessagePrefix)
            output += ": ";
        output += "blame: " + std::string(view.blameLine);
        return;
    }

    if(!view.statusMessage.empty())
    {
        if(view.commandLineMessagePrefix)
            output += ": ";
        int msglen =
            std::min((int)view.statusMessage.length(),
                     std::max(0, view.screenCols -
                                      (view.commandLineMessagePrefix ? 2 : 0)));
        output.append(view.statusMessage, 0, msglen);
        return;
    }

    if(!view.locMessage.empty())
    {
        if(view.commandLineMessagePrefix)
            output += ": ";
        int msglen =
            std::min((int)view.locMessage.length(),
                     std::max(0, view.screenCols -
                                      (view.commandLineMessagePrefix ? 2 : 0)));
        output.append(view.locMessage, 0, msglen);
        return;
    }

    if(view.commandLineMessagePrefix)
        output += ":";
}
} // namespace widgets
