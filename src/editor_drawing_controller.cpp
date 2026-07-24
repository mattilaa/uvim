#include "editor_drawing_controller.h"
#include "editor.h"
#include "editor_buffer_view.h"
#include "editor_message_bar_view.h"
#include "editor_mode_controller.h"
#include "editor_status_bar_view.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"

using namespace editor::statemachine;
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif

#include <algorithm>
#include <cstdlib>

EditorDrawingController::EditorDrawingController(Editor& editor)
    : editor(editor), bufferView(std::make_unique<EditorBufferView>(editor)),
      statusBarView(std::make_unique<EditorStatusBarView>(editor)),
      messageBarView(std::make_unique<EditorMessageBarView>(editor))
{
}

EditorDrawingController::~EditorDrawingController() = default;

void EditorDrawingController::drawBufferView()
{
    bufferView->draw();
}

void EditorDrawingController::drawStatusBar()
{
    statusBarView->draw();
}

void EditorDrawingController::drawStatusBarQuick()
{
    statusBarView->drawQuick();
}

void EditorDrawingController::drawMessageBar()
{
    messageBarView->draw();
}

void EditorDrawingController::drawMessageBarQuick()
{
    messageBarView->drawQuick();
}

void EditorDrawingController::appendStatusBar(std::string& output)
{
    statusBarView->append(output);
}

void EditorDrawingController::appendMessageBar(std::string& output,
                                               bool includePopups)
{
    messageBarView->append(output, includePopups);
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

    if(editor.splitActive && editor.currentMode == FILE_BROWSER)
    {
        editor.drawFullScreen();
        lastFrameOffsetY = *editor.offsetY;
        lastFrameOffsetX = *editor.offsetX;
        lastFrameMode = editor.currentMode;
        lastFrameCursorY = *editor.cursorY;
        lastFrameCommandPopupActive = editor.commandPopupActive;
        lastFrameCommandHistoryPopupActive = editor.commandHistorySearchActive;
        setNeedsFullRedraw(false);
        return;
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
#ifdef UVIM_ENABLE_BROWSER_TOOLS
        if(editor.modeStateMachine)
        {
            if(auto* state =
                   editor.modeStateMachine->getState<FileBrowserMode>())
                state->draw(editor);
        }
#endif
        return;
    }

    if(editor.currentMode == FUZZY_FIND)
    {
#ifdef UVIM_ENABLE_SEARCH_TOOLS
        if(editor.modeStateMachine)
        {
            if(auto* state = editor.modeStateMachine->getState<FuzzyFindMode>())
                state->draw(editor);
        }
#endif
        return;
    }

    if(editor.currentMode == BUFFER_BROWSER)
    {
#ifdef UVIM_ENABLE_BROWSER_TOOLS
        if(editor.modeStateMachine)
        {
            if(auto* state =
                   editor.modeStateMachine->getState<BufferBrowserMode>())
                state->draw(editor);
        }
#endif
        return;
    }

    if(editor.currentMode == GREP_SEARCH)
    {
#ifdef UVIM_ENABLE_SEARCH_TOOLS
        if(editor.modeStateMachine)
        {
            if(auto* state =
                   editor.modeStateMachine->getState<GrepSearchMode>())
                state->draw(editor);
        }
#endif
        return;
    }

    if(editor.currentMode == REGEX_SEARCH)
    {
#ifdef UVIM_ENABLE_SEARCH_TOOLS
        if(editor.modeStateMachine)
        {
            if(auto* state =
                   editor.modeStateMachine->getState<RegexSearchMode>())
                state->draw(editor);
        }
#endif
        return;
    }

    if(editor.currentMode == LOC_LIST)
    {
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
        if(editor.modeStateMachine)
        {
            if(auto* state = editor.modeStateMachine->getState<LocListMode>())
                state->draw(editor);
        }
#endif
        return;
    }

    if(editor.currentMode == MLANG_FORMAT_ERRORS)
    {
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
        if(editor.modeStateMachine)
        {
            if(auto* state =
                   editor.modeStateMachine->getState<MlangFormatErrorsMode>())
                state->draw(editor);
        }
#endif
        return;
    }

    if(editor.currentMode == REFERENCES)
    {
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
        editor.drawReferences();
#endif
        return;
    }
    if(editor.currentMode == LSP_INFO)
    {
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
        editor.drawLspInfo();
#endif
        return;
    }

    if(editor.currentMode == HELP)
    {
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
        if(!needsFullRedraw())
            return;
        if(editor.modeStateMachine)
        {
            if(auto* state = editor.modeStateMachine->getState<HelpMode>())
                state->draw(editor);
        }
#endif
        return;
    }

    if(editor.currentMode == GIT_SHOW)
    {
#ifdef UVIM_ENABLE_GIT_TOOLS
        if(editor.modeStateMachine)
        {
            if(auto* state =
                   editor.modeStateMachine->getState<GitShowCommitMode>())
                state->draw(editor);
        }
#endif
        return;
    }

    if(editor.currentMode == GIT_LOG)
    {
#ifdef UVIM_ENABLE_GIT_TOOLS
        if(editor.modeStateMachine)
        {
            if(auto* state = editor.modeStateMachine->getState<GitLogMode>())
                state->draw(editor);
        }
#endif
        return;
    }

    if(editor.currentMode == GIT_STAGE)
    {
#ifdef UVIM_ENABLE_GIT_TOOLS
        if(editor.modeStateMachine)
        {
            if(auto* state = editor.modeStateMachine->getState<GitStageMode>())
                state->draw(editor);
        }
#endif
        return;
    }

    if(editor.currentMode == GIT_COMMIT)
    {
#ifdef UVIM_ENABLE_GIT_TOOLS
        if(editor.modeStateMachine)
        {
            if(auto* state = editor.modeStateMachine->getState<GitCommitMode>())
                state->draw(editor);
        }
#endif
        return;
    }

    if(editor.currentMode == GIT_FIXUP)
    {
#ifdef UVIM_ENABLE_GIT_TOOLS
        if(editor.modeStateMachine)
        {
            if(auto* state = editor.modeStateMachine->getState<GitFixupMode>())
                state->draw(editor);
        }
#endif
        return;
    }

    if(editor.currentMode == GIT_PATCH)
    {
#ifdef UVIM_ENABLE_GIT_TOOLS
        if(editor.modeStateMachine)
        {
            if(auto* state = editor.modeStateMachine->getState<GitPatchMode>())
                state->draw(editor);
        }
#endif
        return;
    }

    if(editor.currentMode == COMMAND_OUTPUT)
    {
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
        if(editor.modeStateMachine)
        {
            if(auto* state =
                   editor.modeStateMachine->getState<CommandOutputMode>())
                state->draw(editor);
        }
#endif
        return;
    }

    if(editor.currentMode == GLYPH_SELECT)
    {
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
        if(editor.modeStateMachine)
        {
            if(auto* state =
                   editor.modeStateMachine->getState<GlyphSelectMode>())
                state->draw(editor);
        }
#endif
        return;
    }

#ifdef UVIM_ENABLE_COLOR_TOOLS
    if(editor.currentMode == ANSI_TOOLS)
    {
        if(editor.modeStateMachine)
        {
            if(auto* state =
                   editor.modeStateMachine->getState<AnsiToolsMode>())
                state->draw(editor);
        }
        return;
    }

    if(editor.currentMode == COLOR_PICKER)
    {
        if(editor.modeStateMachine)
        {
            if(auto* state =
                   editor.modeStateMachine->getState<ColorPickerMode>())
                state->draw(editor);
        }
        return;
    }

    if(editor.currentMode == COLOR_SELECTOR)
    {
        if(editor.modeStateMachine)
        {
            if(auto* state =
                   editor.modeStateMachine->getState<ColorSelectorMode>())
                state->draw(editor);
        }
        return;
    }
#endif

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
                setNeedsFullRedraw(true);
            }
        }
        editor.syncClangdDiagnosticsIfNeeded(false);
    }
    if(editor.currentMode != INSERT && !editor.showGitBlame)
        editor.syncMlangSemanticTokensIfNeeded(false);
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
        setNeedsFullRedraw(false);
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
    bool isCommandLikeMode = (editor.currentMode == COMMAND ||
                              editor.currentMode == SEARCH_FORWARD ||
                              editor.currentMode == SEARCH_BACKWARD);
    bool isLiveSearchMode = (editor.currentMode == SEARCH_FORWARD ||
                             editor.currentMode == SEARCH_BACKWARD);
    bool commandPopupChanged =
        (editor.commandPopupActive != lastFrameCommandPopupActive) ||
        (editor.commandHistorySearchActive !=
         lastFrameCommandHistoryPopupActive);
    bool commandOverlayStable = isCommandLikeMode && !isLiveSearchMode &&
                                !modeChanged && scrollDelta == 0 &&
                                *editor.offsetX == lastFrameOffsetX &&
                                !visualChanged && !commandPopupChanged;

    if(modeChanged || (needsFullRedraw() && !commandOverlayStable) ||
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
    setNeedsFullRedraw(false);
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
        else if(editor.currentMode == COMMAND && editor.commandBuffer[0] != ':')
            promptLen += 1;
        cursorCol = promptLen + 1;
    }
    else
    {
        Editor::PaneLayout layout = editor.getPaneLayout(editor.activePane);
        const bool fileBrowserState =
            editor.modeStateMachine &&
            editor.modeStateMachine->getState<FileBrowserMode>() != nullptr;
        if(editor.currentMode == FILE_BROWSER || fileBrowserState)
        {
            cursorRow = layout.y + 1;
            cursorCol = layout.x + 1;
        }
        else if(editor.cursorY && editor.cursorX && editor.offsetY &&
                editor.offsetX && editor.lines)
        {
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
        else
        {
            cursorRow = layout.y + 1;
            cursorCol = layout.x + 1;
        }
    }

    Terminal::write(Terminal::cursorPos(cursorRow, cursorCol));
    const bool fileBrowserState =
        editor.modeStateMachine &&
        editor.modeStateMachine->getState<FileBrowserMode>() != nullptr;
    bool hideCursor =
        (editor.currentMode == VISUAL || editor.currentMode == VISUAL_LINE ||
         editor.currentMode == VISUAL_BLOCK || editor.currentMode == FILE_BROWSER ||
         fileBrowserState);
    Terminal::write(hideCursor ? Terminal::ESC_HIDE_CURSOR
                               : Terminal::ESC_SHOW_CURSOR);
    if(flushNow)
        Terminal::flush();

    setLastCursorScreenPosition(cursorRow, cursorCol);
}

void EditorDrawingController::draw()
{
    refreshScreen();
    setNeedsFullRedraw(false);
}

void EditorDrawingController::forceFullRedraw()
{
    setNeedsFullRedraw(true);
}

void EditorDrawingController::setNeedsFullRedraw(bool value)
{
    needsFullRedraw_ = value;
}

bool EditorDrawingController::needsFullRedraw() const
{
    return needsFullRedraw_;
}

void EditorDrawingController::setLastCursorScreenPosition(int row, int col)
{
    lastCursorScreenY_ = row;
    lastCursorScreenX_ = col;
}

int EditorDrawingController::lastCursorScreenY() const
{
    return lastCursorScreenY_;
}

int EditorDrawingController::lastCursorScreenX() const
{
    return lastCursorScreenX_;
}
