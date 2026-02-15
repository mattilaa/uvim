#include "editor.h"
#include "editor_view.h"
#include "terminal.h"
#include "text_utils.h"

void Editor::refreshScreen()
{
    syncModeFromStateMachine();

    if(diagnosticPopupActive && (*cursorY != diagnosticPopupCursorY ||
                                 *cursorX != diagnosticPopupCursorX))
    {
        closeDiagnosticPopup();
    }
    if(symbolPopupActive &&
       (*cursorY != symbolPopupCursorY || *cursorX != symbolPopupCursorX))
    {
        closeSymbolPopup();
    }

    switch(currentMode)
    {
        case WELCOME:
            if(viewWelcome)
                viewWelcome->draw(*this);
            return;
        case NORMAL:
            if(viewNormal)
                viewNormal->draw(*this);
            return;
        case INSERT:
            if(viewInsert)
                viewInsert->draw(*this);
            return;
        case REPLACE:
            if(viewReplace)
                viewReplace->draw(*this);
            return;
        case VISUAL:
            if(viewVisual)
                viewVisual->draw(*this);
            return;
        case VISUAL_LINE:
            if(viewVisualLine)
                viewVisualLine->draw(*this);
            return;
        case VISUAL_BLOCK:
            if(viewVisualBlock)
                viewVisualBlock->draw(*this);
            return;
        case COMMAND:
            if(viewCommand)
                viewCommand->draw(*this);
            return;
        case SEARCH_FORWARD:
            if(viewSearchForward)
                viewSearchForward->draw(*this);
            return;
        case SEARCH_BACKWARD:
            if(viewSearchBackward)
                viewSearchBackward->draw(*this);
            return;
        case FILE_BROWSER:
            if(viewFileBrowser)
                viewFileBrowser->draw(*this);
            return;
        case FUZZY_FIND:
            if(viewFuzzyFind)
                viewFuzzyFind->draw(*this);
            return;
        case BUFFER_BROWSER:
            if(viewBufferBrowser)
                viewBufferBrowser->draw(*this);
            return;
        case GREP_SEARCH:
            if(viewGrepSearch)
                viewGrepSearch->draw(*this);
            return;
        case OP_PENDING:
            if(viewOperatorPending)
                viewOperatorPending->draw(*this);
            return;
        case REFERENCES:
            if(viewReferences)
                viewReferences->draw(*this);
            return;
        case LSP_INFO:
            if(viewLspInfo)
                viewLspInfo->draw(*this);
            return;
        case LOC_LIST:
            if(viewLocList)
                viewLocList->draw(*this);
            return;
        case HELP:
            if(viewHelp)
                viewHelp->draw(*this);
            return;
        case GIT_SHOW:
            if(viewGitShow)
                viewGitShow->draw(*this);
            return;
        case GIT_LOG:
            if(viewGitLog)
                viewGitLog->draw(*this);
            return;
        case GIT_STAGE:
            if(viewGitStage)
                viewGitStage->draw(*this);
            return;
        case GIT_COMMIT:
            if(viewGitCommit)
                viewGitCommit->draw(*this);
            return;
        case GIT_FIXUP:
            if(viewGitFixup)
                viewGitFixup->draw(*this);
            return;
        case GIT_PATCH:
            if(viewGitPatch)
                viewGitPatch->draw(*this);
            return;
        default:
            return;
    }
}

void Editor::updateCursorPosition(bool flushNow)
{
    int cursorRow, cursorCol;

    if(currentMode == COMMAND || currentMode == SEARCH_FORWARD ||
       currentMode == SEARCH_BACKWARD)
    {
        cursorRow = screenRows + 2;
        int promptLen = (commandBuffer.empty() || commandBuffer[0] != ':')
                            ? (int)commandBuffer.length() + 1
                            : (int)commandBuffer.length();
        cursorCol = promptLen + 1;
    }
    else
    {
        PaneLayout layout = getPaneLayout(activePane);
        cursorRow = layout.y + (*cursorY - *offsetY) + 1 + tabBarRows();
        if(utf8Mode && *cursorY >= 0 && *cursorY < (int)lines->size())
        {
            const std::string& line = (*lines)[*cursorY];
            int start = std::clamp(*offsetX, 0, (int)line.size());
            int end = std::clamp(*cursorX, 0, (int)line.size());
            if(end < start)
                std::swap(start, end);
            cursorCol = text_utils::utf8DisplayWidth(
                            std::string_view(line).substr(start, end - start)) +
                        1 + gutterWidth() + layout.x;
        }
        else
        {
            cursorCol = layout.x + (*cursorX - *offsetX) + 1 + gutterWidth();
        }
    }

    Terminal::write(Terminal::cursorPos(cursorRow, cursorCol));
    bool hideCursor = (currentMode == VISUAL || currentMode == VISUAL_LINE ||
                       currentMode == VISUAL_BLOCK);
    Terminal::write(hideCursor ? Terminal::ESC_HIDE_CURSOR
                               : Terminal::ESC_SHOW_CURSOR);
    if(flushNow)
        Terminal::flush();

    lastCursorScreenY = cursorRow;
    lastCursorScreenX = cursorCol;
}

void Editor::draw()
{
    refreshScreen();
    // Some mode-specific draw paths return early from refreshScreen() and
    // don't clear this flag. Clear it here after a completed frame.
    needsFullRedraw = false;
}
