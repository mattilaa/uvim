#include "editor_split_controller.h"

#include <algorithm>

EditorSplitController::EditorSplitController(Editor& editor) : editor(editor) {}

int EditorSplitController::tabBarRows() const
{
    return editor.tabBarRowsImpl();
}

int EditorSplitController::contentRows() const
{
    return editor.contentRowsImpl();
}

Editor::PaneLayout EditorSplitController::getPaneLayout(int pane) const
{
    return editor.getPaneLayoutImpl(pane);
}

void EditorSplitController::setPanePointers(int pane)
{
    editor.setPanePointersImpl(pane);
}

void EditorSplitController::enableSplit(bool vertical)
{
    editor.enableSplitImpl(vertical);
}

void EditorSplitController::closeSplit()
{
    editor.closeSplitImpl();
}

void EditorSplitController::switchPane()
{
    editor.switchPaneImpl();
}

void EditorSplitController::switchPaneDirection(int dx, int dy)
{
    editor.switchPaneDirectionImpl(dx, dy);
}

void EditorSplitController::syncBufferStateFromActivePane()
{
    editor.syncBufferStateFromActivePaneImpl();
}

void EditorSplitController::initSplitPanesFromBuffer()
{
    editor.initSplitPanesFromBufferImpl();
}

void EditorSplitController::switchToBufferInActivePane(int index)
{
    editor.switchToBufferInActivePaneImpl(index);
}

bool EditorSplitController::canSplit() const
{
    return editor.canSplitImpl();
}

int Editor::tabBarRowsImpl() const
{
    return (showTabs && !buffers.empty()) ? 1 : 0;
}

int Editor::contentRowsImpl() const
{
    if(splitActive)
    {
        PaneLayout layout = getPaneLayoutImpl(activePane);
        return std::max(1, layout.rows - tabBarRowsImpl());
    }
    return std::max(1, screenRows - tabBarRowsImpl());
}

Editor::PaneLayout Editor::getPaneLayoutImpl(int pane) const
{
    PaneLayout layout;
    layout.x = 0;
    layout.y = 0;
    layout.rows = screenRows;
    layout.cols = screenCols;

    if(!splitActive)
        return layout;

    if(splitVertical)
    {
        if(screenCols < 2)
            return layout;
        int leftCols = screenCols / 2;
        int rightCols = screenCols - leftCols;
        if(rightCols > 1)
            rightCols -= 1; // avoid auto-wrap at last column
        if(pane == 0)
        {
            layout.x = 0;
            layout.cols = leftCols;
        }
        else
        {
            layout.x = leftCols;
            layout.cols = rightCols;
        }
        layout.y = 0;
        layout.rows = screenRows;
    }
    else
    {
        if(screenRows < 2)
            return layout;
        int topRows = screenRows / 2;
        int bottomRows = screenRows - topRows;
        layout.x = 0;
        layout.cols = screenCols;
        if(pane == 0)
        {
            layout.y = 0;
            layout.rows = topRows;
        }
        else
        {
            layout.y = topRows;
            layout.rows = bottomRows;
        }
    }

    layout.rows = std::max(1, layout.rows);
    layout.cols = std::max(1, layout.cols);
    return layout;
}

void Editor::setPanePointersImpl(int pane)
{
    cursorX = &splitPanes[pane].cursorX;
    cursorY = &splitPanes[pane].cursorY;
    wantedX = &splitPanes[pane].wantedX;
    offsetX = &splitPanes[pane].offsetX;
    offsetY = &splitPanes[pane].offsetY;
}

void Editor::enableSplitImpl(bool vertical)
{
    if(!currentBuffer)
    {
        setStatusMessage("No buffer");
        return;
    }
    if(splitActive)
    {
        syncBufferStateFromActivePane();
    }
    splitActive = true;
    splitVertical = vertical;
    activePane = 0;
    splitTabBarOffset[0] = tabBarOffset;
    splitTabBarOffset[1] = tabBarOffset;
    initSplitPanesFromBuffer();
    setPanePointers(activePane);
    needsFullRedraw = true;
}

void Editor::closeSplitImpl()
{
    if(!splitActive)
        return;
    syncBufferStateFromActivePane();
    int paneIndex = activePane;
    splitActive = false;
    currentBufferIndex = splitPanes[paneIndex].bufferIndex;
    tabBarOffset = splitTabBarOffset[paneIndex];
    activePane = 0;
    updateCurrentBufferPointers();
    needsFullRedraw = true;
}

void Editor::switchPaneImpl()
{
    if(!splitActive)
        return;
    switchPaneDirectionImpl(activePane == 0 ? 1 : -1, activePane == 0 ? 1 : -1);
}

void Editor::switchPaneDirectionImpl(int dx, int dy)
{
    if(!splitActive)
        return;

    int nextPane = activePane;
    if(splitVertical)
    {
        if(dx < 0)
            nextPane = 0;
        else if(dx > 0)
            nextPane = 1;
    }
    else
    {
        if(dy < 0)
            nextPane = 0;
        else if(dy > 0)
            nextPane = 1;
    }

    if(nextPane == activePane)
        return;

    syncBufferStateFromActivePane();
    splitTabBarOffset[activePane] = tabBarOffset;
    activePane = nextPane;
    tabBarOffset = splitTabBarOffset[activePane];
    currentBufferIndex = splitPanes[activePane].bufferIndex;
    updateCurrentBufferPointers();
    adjustViewport();
    needsFullRedraw = true;
}

void Editor::syncBufferStateFromActivePaneImpl()
{
    if(!currentBuffer)
        return;
    currentBuffer->cursorX = splitPanes[activePane].cursorX;
    currentBuffer->cursorY = splitPanes[activePane].cursorY;
    currentBuffer->wantedX = splitPanes[activePane].wantedX;
    currentBuffer->offsetX = splitPanes[activePane].offsetX;
    currentBuffer->offsetY = splitPanes[activePane].offsetY;
}

void Editor::initSplitPanesFromBufferImpl()
{
    if(!currentBuffer)
        return;
    PaneState state;
    state.bufferIndex = currentBufferIndex;
    state.cursorX = currentBuffer->cursorX;
    state.cursorY = currentBuffer->cursorY;
    state.wantedX = currentBuffer->wantedX;
    state.offsetX = currentBuffer->offsetX;
    state.offsetY = currentBuffer->offsetY;
    splitPanes[0] = state;
    splitPanes[1] = state;
}

void Editor::switchToBufferInActivePaneImpl(int index)
{
    if(index < 0 || index >= static_cast<int>(buffers.size()))
        return;
    syncBufferStateFromActivePane();
    splitTabBarOffset[activePane] = tabBarOffset;
    tabBarOffset = splitTabBarOffset[activePane];
    splitPanes[activePane].bufferIndex = index;
    currentBufferIndex = index;
    updateCurrentBufferPointers();
    restoreBufferState();
    needsFullRedraw = true;
}

bool Editor::canSplitImpl() const
{
    if(!splitActive)
        return false;
    if(splitVertical)
        return screenCols >= 2;
    return screenRows >= 2;
}
