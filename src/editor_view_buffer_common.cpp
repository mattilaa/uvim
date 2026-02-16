#include "editor.h"
#include "terminal.h"
#include "text_utils.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif

void Editor::drawBufferView()
{
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
        drawMessageBarQuick();
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
