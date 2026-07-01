#include "editor_split_controller.h"

#include <algorithm>
#include <limits>
#include <vector>

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
    PaneLayout full;
    full.x = 0;
    full.y = 0;
    full.rows = screenRows;
    full.cols = screenCols;

    if(!splitActive)
        return full;

    rebuildSplitPaneLayouts();
    if(pane >= 0 && pane < static_cast<int>(splitPaneLayouts.size()))
        return splitPaneLayouts[pane];
    return full;
}

void Editor::setPanePointersImpl(int pane)
{
    if(pane < 0 || pane >= static_cast<int>(splitPanes.size()))
        return;
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

    if(!splitActive || splitPanes.empty() || splitRoot < 0)
    {
        initSplitPanesFromBufferImpl();
    }

    const int activeNode = findSplitLeafNode(splitRoot, activePane);
    if(activeNode < 0)
        return;

    PaneState newState = splitPanes[activePane];
    const int newPane = static_cast<int>(splitPanes.size());
    splitPanes.push_back(newState);
    splitTabBarOffset.push_back(tabBarOffset);

    SplitNode oldLeaf;
    oldLeaf.leaf = true;
    oldLeaf.pane = activePane;
    const int oldLeafIndex = static_cast<int>(splitNodes.size());
    splitNodes.push_back(oldLeaf);

    SplitNode newLeaf;
    newLeaf.leaf = true;
    newLeaf.pane = newPane;
    const int newLeafIndex = static_cast<int>(splitNodes.size());
    splitNodes.push_back(newLeaf);

    splitNodes[activeNode].leaf = false;
    splitNodes[activeNode].vertical = vertical;
    splitNodes[activeNode].pane = -1;
    splitNodes[activeNode].first = oldLeafIndex;
    splitNodes[activeNode].second = newLeafIndex;

    splitActive = splitPanes.size() > 1;
    splitVertical = vertical;
    activePane = newPane;
    currentBufferIndex = splitPanes[activePane].bufferIndex;
    tabBarOffset = splitTabBarOffset[activePane];
    updateCurrentBufferPointers();
    setPanePointers(activePane);
    splitPaneLayouts.clear();
    needsFullRedraw = true;
}

void Editor::closeSplitImpl()
{
    if(!splitActive)
        return;
    syncBufferStateFromActivePane();

    const int paneIndex = activePane;
    const int activeNode = findSplitLeafNode(splitRoot, activePane);
    const int parentNode = findSplitParentNode(splitRoot, activeNode);
    if(activeNode < 0 || parentNode < 0 || paneIndex < 0 ||
       paneIndex >= static_cast<int>(splitPanes.size()))
    {
        splitActive = false;
        if(paneIndex >= 0 && paneIndex < static_cast<int>(splitPanes.size()))
            currentBufferIndex = splitPanes[paneIndex].bufferIndex;
        if(paneIndex >= 0 &&
           paneIndex < static_cast<int>(splitTabBarOffset.size()))
            tabBarOffset = splitTabBarOffset[paneIndex];
        activePane = 0;
        splitPanes.clear();
        splitTabBarOffset.clear();
        splitNodes.clear();
        splitRoot = -1;
        splitPaneLayouts.clear();
        updateCurrentBufferPointers();
        needsFullRedraw = true;
        return;
    }

    const SplitNode& parent = splitNodes[parentNode];
    const int siblingNode =
        parent.first == activeNode ? parent.second : parent.first;
    int nextPane = firstSplitLeafPane(siblingNode);

    splitNodes[parentNode] = splitNodes[siblingNode];

    splitPanes.erase(splitPanes.begin() + paneIndex);
    if(paneIndex < static_cast<int>(splitTabBarOffset.size()))
        splitTabBarOffset.erase(splitTabBarOffset.begin() + paneIndex);
    for(auto& node : splitNodes)
    {
        if(!node.leaf)
            continue;
        if(node.pane == paneIndex)
            node.pane = -1;
        else if(node.pane > paneIndex)
            --node.pane;
    }
    if(nextPane > paneIndex)
        --nextPane;

    splitActive = splitPanes.size() > 1;
    if(!splitActive)
    {
        splitActive = false;
        activePane = 0;
        splitNodes.clear();
        SplitNode root;
        root.leaf = true;
        root.pane = 0;
        splitNodes.push_back(root);
        splitRoot = 0;
    }
    else
    {
        activePane = std::max(0, nextPane);
    }

    if(activePane >= 0 && activePane < static_cast<int>(splitPanes.size()))
    {
        currentBufferIndex = splitPanes[activePane].bufferIndex;
        if(activePane < static_cast<int>(splitTabBarOffset.size()))
            tabBarOffset = splitTabBarOffset[activePane];
    }
    splitPaneLayouts.clear();
    updateCurrentBufferPointers();
    setPanePointers(activePane);
    needsFullRedraw = true;
}

void Editor::switchPaneImpl()
{
    if(!splitActive)
        return;
    switchPaneDirectionImpl(1, 0);
}

void Editor::switchPaneDirectionImpl(int dx, int dy)
{
    if(!splitActive)
        return;

    rebuildSplitPaneLayouts();
    if(activePane < 0 ||
       activePane >= static_cast<int>(splitPaneLayouts.size()))
        return;

    const PaneLayout current = splitPaneLayouts[activePane];
    const int currentLeft = current.x;
    const int currentRight = current.x + current.cols;
    const int currentTop = current.y;
    const int currentBottom = current.y + current.rows;
    const int currentCx = current.x + current.cols / 2;
    const int currentCy = current.y + current.rows / 2;

    int nextPane = -1;
    int bestPrimary = std::numeric_limits<int>::max();
    int bestSecondary = std::numeric_limits<int>::max();

    for(int pane = 0; pane < static_cast<int>(splitPaneLayouts.size()); ++pane)
    {
        if(pane == activePane)
            continue;
        const PaneLayout other = splitPaneLayouts[pane];
        if(other.rows <= 0 || other.cols <= 0)
            continue;

        const int otherLeft = other.x;
        const int otherRight = other.x + other.cols;
        const int otherTop = other.y;
        const int otherBottom = other.y + other.rows;
        const int otherCx = other.x + other.cols / 2;
        const int otherCy = other.y + other.rows / 2;

        int primary = 0;
        int secondary = 0;
        if(dx < 0)
        {
            if(otherRight > currentLeft)
                continue;
            primary = currentLeft - otherRight;
            const int overlap = std::min(currentBottom, otherBottom) -
                                std::max(currentTop, otherTop);
            secondary = overlap > 0 ? 0 : std::abs(otherCy - currentCy);
        }
        else if(dx > 0)
        {
            if(otherLeft < currentRight)
                continue;
            primary = otherLeft - currentRight;
            const int overlap = std::min(currentBottom, otherBottom) -
                                std::max(currentTop, otherTop);
            secondary = overlap > 0 ? 0 : std::abs(otherCy - currentCy);
        }
        else if(dy < 0)
        {
            if(otherBottom > currentTop)
                continue;
            primary = currentTop - otherBottom;
            const int overlap = std::min(currentRight, otherRight) -
                                std::max(currentLeft, otherLeft);
            secondary = overlap > 0 ? 0 : std::abs(otherCx - currentCx);
        }
        else if(dy > 0)
        {
            if(otherTop < currentBottom)
                continue;
            primary = otherTop - currentBottom;
            const int overlap = std::min(currentRight, otherRight) -
                                std::max(currentLeft, otherLeft);
            secondary = overlap > 0 ? 0 : std::abs(otherCx - currentCx);
        }
        else
        {
            continue;
        }

        if(primary < bestPrimary ||
           (primary == bestPrimary && secondary < bestSecondary))
        {
            bestPrimary = primary;
            bestSecondary = secondary;
            nextPane = pane;
        }
    }

    if(nextPane < 0 || nextPane == activePane)
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
    if(!currentBuffer || activePane < 0 ||
       activePane >= static_cast<int>(splitPanes.size()))
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
    splitPanes.clear();
    splitPanes.push_back(state);
    splitTabBarOffset.clear();
    splitTabBarOffset.push_back(tabBarOffset);
    splitNodes.clear();
    SplitNode root;
    root.leaf = true;
    root.pane = 0;
    splitNodes.push_back(root);
    splitRoot = 0;
    activePane = 0;
    splitPaneLayouts.clear();
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
    return screenCols >= 2 && screenRows >= 2;
}

void Editor::collectSplitLeafPanes(int node, std::vector<int>& panes) const
{
    if(node < 0 || node >= static_cast<int>(splitNodes.size()))
        return;
    const SplitNode& n = splitNodes[node];
    if(n.leaf)
    {
        if(n.pane >= 0)
            panes.push_back(n.pane);
        return;
    }
    collectSplitLeafPanes(n.first, panes);
    collectSplitLeafPanes(n.second, panes);
}

int Editor::findSplitLeafNode(int node, int pane) const
{
    if(node < 0 || node >= static_cast<int>(splitNodes.size()))
        return -1;
    const SplitNode& n = splitNodes[node];
    if(n.leaf)
        return n.pane == pane ? node : -1;
    int found = findSplitLeafNode(n.first, pane);
    if(found >= 0)
        return found;
    return findSplitLeafNode(n.second, pane);
}

int Editor::findSplitParentNode(int node, int child) const
{
    if(node < 0 || node >= static_cast<int>(splitNodes.size()) || child < 0)
        return -1;
    const SplitNode& n = splitNodes[node];
    if(n.leaf)
        return -1;
    if(n.first == child || n.second == child)
        return node;
    int found = findSplitParentNode(n.first, child);
    if(found >= 0)
        return found;
    return findSplitParentNode(n.second, child);
}

int Editor::firstSplitLeafPane(int node) const
{
    if(node < 0 || node >= static_cast<int>(splitNodes.size()))
        return -1;
    const SplitNode& n = splitNodes[node];
    if(n.leaf)
        return n.pane;
    int pane = firstSplitLeafPane(n.first);
    if(pane >= 0)
        return pane;
    return firstSplitLeafPane(n.second);
}

void Editor::rebuildSplitPaneLayouts() const
{
    splitPaneLayouts.assign(splitPanes.size(), PaneLayout{});
    if(!splitActive || splitRoot < 0 ||
       splitRoot >= static_cast<int>(splitNodes.size()))
        return;

    auto assignNode = [&](auto&& self, int node, PaneLayout layout) -> void
    {
        if(node < 0 || node >= static_cast<int>(splitNodes.size()))
            return;
        layout.rows = std::max(1, layout.rows);
        layout.cols = std::max(1, layout.cols);
        const SplitNode& n = splitNodes[node];
        if(n.leaf)
        {
            if(n.pane >= 0 &&
               n.pane < static_cast<int>(splitPaneLayouts.size()))
                splitPaneLayouts[n.pane] = layout;
            return;
        }

        PaneLayout first = layout;
        PaneLayout second = layout;
        if(n.vertical)
        {
            int leftCols = std::max(1, layout.cols / 2);
            int rightCols = std::max(1, layout.cols - leftCols);
            first.cols = leftCols;
            second.x = layout.x + leftCols;
            second.cols = rightCols;
        }
        else
        {
            int topRows = std::max(1, layout.rows / 2);
            int bottomRows = std::max(1, layout.rows - topRows);
            first.rows = topRows;
            second.y = layout.y + topRows;
            second.rows = bottomRows;
        }
        self(self, n.first, first);
        self(self, n.second, second);
    };

    PaneLayout root;
    root.x = 0;
    root.y = 0;
    root.rows = screenRows;
    root.cols = screenCols;
    assignNode(assignNode, splitRoot, root);
}
