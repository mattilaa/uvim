#include "editor_drawing_controller.h"
#include "editor.h"
#include "editor_mode_controller.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"

#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif

#include <algorithm>
#include <cstdlib>

EditorDrawingController::EditorDrawingController(Editor& editor)
    : editor(editor)
{
}

void EditorDrawingController::drawBufferView()
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    if(editor.currentMode != INSERT && !editor.showGitBlame)
    {
        if(editor.currentBuffer && editor.isClangdLspEnabled() &&
           editor.isFileType<FileType::Cpp>() &&
           !editor.isFileType<FileType::Mla>() && editor.lspClient &&
           !editor.currentBuffer->filename.empty())
        {
            size_t revision = editor.lspClient->diagnosticsRevision(
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
    }
#endif

    editor.adjustViewport();

    if(editor.splitActive)
    {
        editor.drawFullScreen();
        lastBufferOffsetY = *editor.offsetY;
        lastBufferOffsetX = *editor.offsetX;
        lastBufferMode = editor.currentMode;
        lastBufferCursorY = *editor.cursorY;
        editor.needsFullRedraw = false;
        return;
    }

    bool modeChanged = (editor.currentMode != lastBufferMode);
    int scrollDelta = *editor.offsetY - lastBufferOffsetY;
    bool cursorMoved = (*editor.cursorY != lastBufferCursorY);

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
            (editor.currentBuffer->visualStartY != lastBufferVisualStartY ||
             editor.currentBuffer->visualEndY != lastBufferVisualEndY);
        lastBufferVisualStartY = editor.currentBuffer->visualStartY;
        lastBufferVisualEndY = editor.currentBuffer->visualEndY;
    }
    else
    {
        lastBufferVisualStartY = -1;
        lastBufferVisualEndY = -1;
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
        scrollDelta == 0 && *editor.offsetX == lastBufferOffsetX &&
        !visualChanged;

    if(modeChanged || (editor.needsFullRedraw && !commandOverlayStable) ||
       *editor.offsetX != lastBufferOffsetX ||
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
        updateCursorPosition(false);
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
        updateCursorPosition(false);
        if(syncOutput)
            Terminal::write(Terminal::ESC_SYNC_UPDATE_END);
        Terminal::flush();
    }
    else
    {
        editor.drawFullScreen();
    }

    lastBufferOffsetY = *editor.offsetY;
    lastBufferOffsetX = *editor.offsetX;
    lastBufferMode = editor.currentMode;
    lastBufferCursorY = *editor.cursorY;
    editor.needsFullRedraw = false;
}

void EditorDrawingController::refreshScreen()
{
    editor.modeController->syncModeFromStateMachine();

    if(editor.diagnosticPopupActive &&
       (*editor.cursorY != editor.diagnosticPopupCursorY ||
        *editor.cursorX != editor.diagnosticPopupCursorX))
    {
        editor.closeDiagnosticPopup();
    }
    if(editor.symbolPopupActive &&
       (*editor.cursorY != editor.symbolPopupCursorY ||
        *editor.cursorX != editor.symbolPopupCursorX))
    {
        editor.closeSymbolPopup();
    }

    if(editor.currentMode == WELCOME)
    {
        if(editor.modeStateMachine)
        {
            if(auto* state = editor.modeStateMachine->getState<WelcomeMode>())
                state->draw(editor);
        }
        return;
    }

    if(editor.currentMode == FILE_BROWSER)
    {
        if(editor.modeStateMachine)
        {
            if(auto* state =
                   editor.modeStateMachine->getState<FileBrowserMode>())
                state->draw(editor);
        }
        return;
    }

    if(editor.currentMode == FUZZY_FIND)
    {
        if(editor.modeStateMachine)
        {
            if(auto* state = editor.modeStateMachine->getState<FuzzyFindMode>())
                state->draw(editor);
        }
        return;
    }

    if(editor.currentMode == BUFFER_BROWSER)
    {
        if(editor.modeStateMachine)
        {
            if(auto* state =
                   editor.modeStateMachine->getState<BufferBrowserMode>())
                state->draw(editor);
        }
        return;
    }

    if(editor.currentMode == GREP_SEARCH)
    {
        if(editor.modeStateMachine)
        {
            if(auto* state = editor.modeStateMachine->getState<GrepSearchMode>())
                state->draw(editor);
        }
        return;
    }

    if(editor.currentMode == REGEX_SEARCH)
    {
        if(editor.modeStateMachine)
        {
            if(auto* state =
                   editor.modeStateMachine->getState<RegexSearchMode>())
                state->draw(editor);
        }
        return;
    }

    if(editor.currentMode == LOC_LIST)
    {
        if(editor.modeStateMachine)
        {
            if(auto* state = editor.modeStateMachine->getState<LocListMode>())
                state->draw(editor);
        }
        return;
    }

    if(editor.currentMode == REFERENCES)
    {
        editor.drawReferences();
        return;
    }
    if(editor.currentMode == LSP_INFO)
    {
        editor.drawLspInfo();
        return;
    }

    if(editor.currentMode == HELP)
    {
        if(!editor.needsFullRedraw)
            return;
        if(editor.modeStateMachine)
        {
            if(auto* state = editor.modeStateMachine->getState<HelpMode>())
                state->draw(editor);
        }
        return;
    }

    if(editor.currentMode == GIT_SHOW)
    {
        if(editor.modeStateMachine)
        {
            if(auto* state =
                   editor.modeStateMachine->getState<GitShowCommitMode>())
                state->draw(editor);
        }
        return;
    }

    if(editor.currentMode == GIT_LOG)
    {
        if(editor.modeStateMachine)
        {
            if(auto* state = editor.modeStateMachine->getState<GitLogMode>())
                state->draw(editor);
        }
        return;
    }

    if(editor.currentMode == GIT_STAGE)
    {
        if(editor.modeStateMachine)
        {
            if(auto* state = editor.modeStateMachine->getState<GitStageMode>())
                state->draw(editor);
        }
        return;
    }

    if(editor.currentMode == GIT_COMMIT)
    {
        if(editor.modeStateMachine)
        {
            if(auto* state = editor.modeStateMachine->getState<GitCommitMode>())
                state->draw(editor);
        }
        return;
    }

    if(editor.currentMode == GIT_FIXUP)
    {
        if(editor.modeStateMachine)
        {
            if(auto* state = editor.modeStateMachine->getState<GitFixupMode>())
                state->draw(editor);
        }
        return;
    }

    if(editor.currentMode == GIT_PATCH)
    {
        if(editor.modeStateMachine)
        {
            if(auto* state = editor.modeStateMachine->getState<GitPatchMode>())
                state->draw(editor);
        }
        return;
    }

    if(editor.currentMode == COMMAND_OUTPUT)
    {
        if(editor.modeStateMachine)
        {
            if(auto* state =
                   editor.modeStateMachine->getState<CommandOutputMode>())
                state->draw(editor);
        }
        return;
    }

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(editor.currentMode != INSERT && !editor.showGitBlame)
    {
        if(editor.currentBuffer && editor.isClangdLspEnabled() &&
           editor.isFileType<FileType::Cpp>() &&
           !editor.isFileType<FileType::Mla>() && editor.lspClient &&
           !editor.currentBuffer->filename.empty())
        {
            size_t revision = editor.lspClient->diagnosticsRevision(
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
        lastFrameOffsetY = *editor.offsetY;
        lastFrameOffsetX = *editor.offsetX;
        lastFrameMode = editor.currentMode;
        lastFrameCursorY = *editor.cursorY;
        lastFrameCommandPopupActive = editor.commandPopupActive;
        lastFrameCommandHistoryPopupActive = editor.commandHistorySearchActive;
        editor.needsFullRedraw = false;
        return;
    }

    bool modeChanged = (editor.currentMode != lastFrameMode);
    int scrollDelta = *editor.offsetY - lastFrameOffsetY;
    bool cursorMoved = (*editor.cursorY != lastFrameCursorY);

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
            (editor.currentBuffer->visualStartY != lastFrameVisualStartY ||
             editor.currentBuffer->visualEndY != lastFrameVisualEndY);
        lastFrameVisualStartY = editor.currentBuffer->visualStartY;
        lastFrameVisualEndY = editor.currentBuffer->visualEndY;
    }
    else
    {
        lastFrameVisualStartY = -1;
        lastFrameVisualEndY = -1;
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
    bool commandPopupChanged =
        (editor.commandPopupActive != lastFrameCommandPopupActive) ||
        (editor.commandHistorySearchActive !=
         lastFrameCommandHistoryPopupActive);
    bool commandOverlayStable =
        isCommandLikeMode && !isLiveSearchMode && !modeChanged &&
        scrollDelta == 0 && *editor.offsetX == lastFrameOffsetX &&
        !visualChanged && !commandPopupChanged;

    if(modeChanged || (editor.needsFullRedraw && !commandOverlayStable) ||
       *editor.offsetX != lastFrameOffsetX ||
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
        updateCursorPosition(false);
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
        updateCursorPosition(false);
        if(syncOutput)
            Terminal::write(Terminal::ESC_SYNC_UPDATE_END);
        Terminal::flush();
    }
    else
    {
        editor.drawFullScreen();
    }

    lastFrameOffsetY = *editor.offsetY;
    lastFrameOffsetX = *editor.offsetX;
    lastFrameMode = editor.currentMode;
    lastFrameCursorY = *editor.cursorY;
    lastFrameCommandPopupActive = editor.commandPopupActive;
    lastFrameCommandHistoryPopupActive = editor.commandHistorySearchActive;
    editor.needsFullRedraw = false;
}

void EditorDrawingController::updateCursorPosition(bool flushNow)
{
    int cursorRow, cursorCol;

    if(editor.currentMode == COMMAND || editor.currentMode == SEARCH_FORWARD ||
       editor.currentMode == SEARCH_BACKWARD)
    {
        cursorRow = editor.screenRows + 2;
        int promptLen = (int)editor.commandBuffer.length();
        if(editor.commandBuffer.empty())
            promptLen = 1;
        else if(editor.currentMode == COMMAND &&
                editor.commandBuffer[0] != ':')
            promptLen += 1;
        cursorCol = promptLen + 1;
    }
    else
    {
        Editor::PaneLayout layout = editor.getPaneLayout(editor.activePane);
        cursorRow = layout.y + (*editor.cursorY - *editor.offsetY) + 1 +
                    editor.tabBarRows();
        if(editor.utf8Mode && *editor.cursorY >= 0 &&
           *editor.cursorY < (int)editor.lines->size())
        {
            const std::string& line = (*editor.lines)[*editor.cursorY];
            int start = std::clamp(*editor.offsetX, 0, (int)line.size());
            int end = std::clamp(*editor.cursorX, 0, (int)line.size());
            if(end < start)
                std::swap(start, end);
            cursorCol =
                text_utils::utf8DisplayWidth(
                    std::string_view(line).substr(start, end - start)) +
                1 + editor.gutterWidth() + layout.x;
        }
        else
        {
            cursorCol = layout.x + (*editor.cursorX - *editor.offsetX) + 1 +
                        editor.gutterWidth();
        }
    }

    Terminal::write(Terminal::cursorPos(cursorRow, cursorCol));
    bool hideCursor = (editor.currentMode == VISUAL ||
                       editor.currentMode == VISUAL_LINE ||
                       editor.currentMode == VISUAL_BLOCK);
    Terminal::write(hideCursor ? Terminal::ESC_HIDE_CURSOR
                               : Terminal::ESC_SHOW_CURSOR);
    if(flushNow)
        Terminal::flush();

    editor.lastCursorScreenY = cursorRow;
    editor.lastCursorScreenX = cursorCol;
}

void EditorDrawingController::draw()
{
    refreshScreen();
    editor.needsFullRedraw = false;
}

void EditorDrawingController::forceFullRedraw()
{
    editor.needsFullRedraw = true;
}
