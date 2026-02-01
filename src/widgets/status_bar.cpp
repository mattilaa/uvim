#include "widgets/status_bar.h"

#include "theme.h"
#include "text_utils.h"
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

    char rightStatusBuf[32];
    snprintf(rightStatusBuf, sizeof(rightStatusBuf), " %d:%d ", view.cursorY + 1,
             view.cursorX + 1);
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
        lspInfo = " " + view.theme.uiInfo() + "[" +
                  std::string(view.lspLabel) + "]" +
                  view.theme.statusBar();
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
        output += view.commandBuffer;
        if(view.currentMode == SEARCH_FORWARD ||
           view.currentMode == SEARCH_BACKWARD)
        {
            if(view.searchMatchCount > 0)
            {
                output += " [" + std::to_string(view.searchMatchIndex + 1) +
                          "/" + std::to_string(view.searchMatchCount) + "]";
            }
            else if(!view.searchQuery.empty())
            {
                output += " [No matches]";
            }
        }
        return;
    }

    if(view.showGitBlame && view.showGitBlameInfo && !view.blameLine.empty())
    {
        output += "blame: " + std::string(view.blameLine);
        return;
    }

    if(!view.statusMessage.empty())
    {
        int msglen =
            std::min((int)view.statusMessage.length(), view.screenCols);
        output.append(view.statusMessage, 0, msglen);
    }
}
} // namespace widgets
