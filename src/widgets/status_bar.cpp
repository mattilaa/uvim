#include "widgets/status_bar.h"

#include "text_utils.h"
#include "theme.h"
#include <algorithm>
#include <cstdio>

namespace widgets
{
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

    std::string rightBlock = searchInfo + lspInfo;
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
