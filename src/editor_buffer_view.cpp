#include "editor_buffer_view.h"
#include "editor.h"
#include "terminal.h"

#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif

#include <algorithm>
#include <cstdlib>

void EditorBufferView::draw()
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    if(editor.currentMode != INSERT && !editor.showGitBlame &&
       editor.emitLspDiagnostics)
    {
        LspClient* diagnosticsClient = nullptr;
        if(editor.currentBuffer && editor.isFileType<FileType::Cpp>() &&
           editor.isClangdLspEnabled() && editor.lspClient)
            diagnosticsClient = editor.lspClient.get();
        else if(editor.currentBuffer && editor.isFileType<FileType::Mla>() &&
                editor.isMlangLspEnabled() && editor.mlangLspClient)
            diagnosticsClient = editor.mlangLspClient.get();

        if(diagnosticsClient && !editor.currentBuffer->filename.empty())
        {
            size_t revision = diagnosticsClient->diagnosticsRevision(
                editor.currentBuffer->filename);
            if(!editor.currentBuffer->lspDiagnosticsSeenValid ||
               revision != editor.currentBuffer->lspDiagnosticsSeenRevision)
            {
                editor.currentBuffer->lspDiagnosticsSeenRevision = revision;
                editor.currentBuffer->lspDiagnosticsSeenValid = true;
                editor.needsFullRedraw = true;
            }
        }
        editor.syncClangdDiagnosticsIfNeeded(false);
        editor.syncMlangSemanticTokensIfNeeded(false);
    }
#endif

    editor.adjustViewport();

    if(editor.splitActive)
    {
        editor.drawFullScreen();
        lastOffsetY = *editor.offsetY;
        lastOffsetX = *editor.offsetX;
        lastMode = editor.currentMode;
        lastCursorY = *editor.cursorY;
        editor.needsFullRedraw = false;
        return;
    }

    bool modeChanged = (editor.currentMode != lastMode);
    int scrollDelta = *editor.offsetY - lastOffsetY;
    bool cursorMoved = (*editor.cursorY != lastCursorY);

    if(editor.showGitBlame && editor.currentBuffer &&
       !editor.currentBuffer->blameValid)
    {
        editor.updateGitBlameForVisibleRange();
    }

    bool visualChanged = false;
    if(editor.currentMode == VISUAL || editor.currentMode == VISUAL_LINE ||
       editor.currentMode == VISUAL_BLOCK)
    {
        visualChanged =
            (editor.currentBuffer->visualStartY != lastVisualStartY ||
             editor.currentBuffer->visualEndY != lastVisualEndY);
        lastVisualStartY = editor.currentBuffer->visualStartY;
        lastVisualEndY = editor.currentBuffer->visualEndY;
    }
    else
    {
        lastVisualStartY = -1;
        lastVisualEndY = -1;
    }

    bool isBufferEditingMode =
        (editor.currentMode == INSERT || editor.currentMode == REPLACE);
    bool isCommandLikeMode =
        (editor.currentMode == COMMAND ||
         editor.currentMode == SEARCH_FORWARD ||
         editor.currentMode == SEARCH_BACKWARD);
    bool isLiveSearchMode =
        (editor.currentMode == SEARCH_FORWARD ||
         editor.currentMode == SEARCH_BACKWARD);
    bool commandOverlayStable =
        isCommandLikeMode && !isLiveSearchMode && !modeChanged &&
        scrollDelta == 0 && *editor.offsetX == lastOffsetX && !visualChanged;

    if(modeChanged || (editor.needsFullRedraw && !commandOverlayStable) ||
       *editor.offsetX != lastOffsetX ||
       std::abs(scrollDelta) > editor.screenRows / 2 || visualChanged ||
       (editor.currentMode == VISUAL || editor.currentMode == VISUAL_LINE ||
        editor.currentMode == VISUAL_BLOCK) ||
       isBufferEditingMode)
    {
        editor.drawFullScreen();
    }
    else if(scrollDelta == 0 && isCommandLikeMode)
    {
        const bool syncOutput = Terminal::useSynchronizedOutput();
        if(syncOutput)
            Terminal::write(Terminal::ESC_SYNC_UPDATE_BEGIN);
        Terminal::write(Terminal::ESC_HIDE_CURSOR);
        editor.drawStatusBarQuick();
        editor.drawMessageBarQuick();
        editor.updateCursorPosition(false);
        if(syncOutput)
            Terminal::write(Terminal::ESC_SYNC_UPDATE_END);
        Terminal::flush();
    }
    else if(scrollDelta != 0 && std::abs(scrollDelta) <= 5 &&
            editor.currentMode == NORMAL && !Terminal::isTmux())
    {
        editor.drawScrollUpdate(scrollDelta);
    }
    else if(scrollDelta == 0 && editor.currentMode == NORMAL)
    {
        const bool syncOutput = Terminal::useSynchronizedOutput();
        if(syncOutput)
            Terminal::write(Terminal::ESC_SYNC_UPDATE_BEGIN);
        Terminal::write(Terminal::ESC_HIDE_CURSOR);
        if(cursorMoved && editor.lineNumberWidth() > 0)
            editor.drawGutterQuick();
        editor.drawStatusBarQuick();
        editor.drawMessageBarQuick();
        editor.updateCursorPosition(false);
        if(syncOutput)
            Terminal::write(Terminal::ESC_SYNC_UPDATE_END);
        Terminal::flush();
    }
    else
    {
        editor.drawFullScreen();
    }

    lastOffsetY = *editor.offsetY;
    lastOffsetX = *editor.offsetX;
    lastMode = editor.currentMode;
    lastCursorY = *editor.cursorY;
    editor.needsFullRedraw = false;
}
