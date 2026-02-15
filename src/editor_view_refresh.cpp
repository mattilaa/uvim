#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif

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

    if(currentMode == WELCOME)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<WelcomeMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == FILE_BROWSER)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<FileBrowserMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == FUZZY_FIND)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<FuzzyFindMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == BUFFER_BROWSER)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<BufferBrowserMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == GREP_SEARCH)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<GrepSearchMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == LOC_LIST)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<LocListMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == REFERENCES)
    {
        drawReferences();
        return;
    }
    if(currentMode == LSP_INFO)
    {
        drawLspInfo();
        return;
    }

    if(currentMode == HELP)
    {
        if(!needsFullRedraw)
            return;
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<HelpMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == GIT_SHOW)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<GitShowCommitMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == GIT_LOG)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<GitLogMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == GIT_STAGE)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<GitStageMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == GIT_COMMIT)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<GitCommitMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == GIT_FIXUP)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<GitFixupMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == GIT_PATCH)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<GitPatchMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(currentMode != INSERT && !showGitBlame)
    {
        if(currentBuffer && isClangdLspEnabled() &&
           isFileType<FileType::Cpp>() && !isFileType<FileType::Mla>() &&
           lspClient && !currentBuffer->filename.empty())
        {
            size_t revision =
                lspClient->diagnosticsRevision(currentBuffer->filename);
            if(!currentBuffer->lspDiagnosticsSeenValid ||
               revision != currentBuffer->lspDiagnosticsSeenRevision)
            {
                currentBuffer->lspDiagnosticsSeenRevision = revision;
                currentBuffer->lspDiagnosticsSeenValid = true;
                needsFullRedraw = true;
            }
        }
        syncClangdDiagnosticsIfNeeded(false);
    }
#endif

    static int lastOffsetY = -1;
    static int lastOffsetX = -1;
    static Mode lastMode = NORMAL;
    static int lastVisualStartY = -1;
    static int lastVisualEndY = -1;
    static int lastCursorY = -1;

    int prevOffsetY = lastOffsetY;
    adjustViewport();

    if(splitActive)
    {
        drawFullScreen();
        lastOffsetY = *offsetY;
        lastOffsetX = *offsetX;
        lastMode = currentMode;
        lastCursorY = *cursorY;
        needsFullRedraw = false;
        return;
    }

    bool scrolled = (*offsetY != lastOffsetY || *offsetX != lastOffsetX);
    bool modeChanged = (currentMode != lastMode);
    int scrollDelta = *offsetY - lastOffsetY;
    bool cursorMoved = (*cursorY != lastCursorY);

    if(showGitBlame && currentBuffer && !currentBuffer->blameValid)
        updateGitBlameForVisibleRange();

    bool visualChanged = false;
    if(currentMode == VISUAL || currentMode == VISUAL_LINE ||
       currentMode == VISUAL_BLOCK)
    {
        visualChanged = (currentBuffer->visualStartY != lastVisualStartY ||
                         currentBuffer->visualEndY != lastVisualEndY);
        lastVisualStartY = currentBuffer->visualStartY;
        lastVisualEndY = currentBuffer->visualEndY;
    }
    else
    {
        lastVisualStartY = -1;
        lastVisualEndY = -1;
    }

    bool isBufferEditingMode =
        (currentMode == INSERT || currentMode == REPLACE);
    bool isCommandLikeMode =
        (currentMode == COMMAND || currentMode == SEARCH_FORWARD ||
         currentMode == SEARCH_BACKWARD);
    bool commandOverlayStable = isCommandLikeMode && !modeChanged &&
                                scrollDelta == 0 && *offsetX == lastOffsetX &&
                                !visualChanged;

    if(modeChanged || (needsFullRedraw && !commandOverlayStable) ||
       *offsetX != lastOffsetX || abs(scrollDelta) > screenRows / 2 ||
       visualChanged ||
       (currentMode == VISUAL || currentMode == VISUAL_LINE ||
        currentMode == VISUAL_BLOCK) ||
       isBufferEditingMode)
    {
        drawFullScreen();
    }
    else if(scrollDelta == 0 && isCommandLikeMode)
    {
        // Command/search editing only affects overlays (message line, popups,
        // cursor). Keep buffer rows stable to avoid tmux flicker.
        const bool syncOutput = Terminal::useSynchronizedOutput();
        if(syncOutput)
            Terminal::write(Terminal::ESC_SYNC_UPDATE_BEGIN);
        Terminal::write(Terminal::ESC_HIDE_CURSOR);
        drawStatusBarQuick();
        drawMessageBarQuick();
        updateCursorPosition(false);
        if(syncOutput)
            Terminal::write(Terminal::ESC_SYNC_UPDATE_END);
        Terminal::flush();
    }
    else if(scrollDelta != 0 && abs(scrollDelta) <= 5 &&
            currentMode == NORMAL && !Terminal::isTmux())
    {
        drawScrollUpdate(scrollDelta);
    }
    else if(scrollDelta == 0 && currentMode == NORMAL)
    {
        const bool syncOutput = Terminal::useSynchronizedOutput();
        if(syncOutput)
            Terminal::write(Terminal::ESC_SYNC_UPDATE_BEGIN);
        Terminal::write(Terminal::ESC_HIDE_CURSOR);
        if(cursorMoved && lineNumberWidth() > 0)
            drawGutterQuick();
        drawStatusBarQuick();
        drawMessageBarQuick(); // Add this
        updateCursorPosition(false);
        if(syncOutput)
            Terminal::write(Terminal::ESC_SYNC_UPDATE_END);
        Terminal::flush();
    }
    else
    {
        drawFullScreen();
    }

    lastOffsetY = *offsetY;
    lastOffsetX = *offsetX;
    lastMode = currentMode;
    lastCursorY = *cursorY;
    needsFullRedraw = false;
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
