#pragma once

#include "editor.h"

class EditorSplitController
{
public:
    explicit EditorSplitController(Editor& editor);

    int tabBarRows() const;
    int contentRows() const;
    Editor::PaneLayout getPaneLayout(int pane) const;
    void setPanePointers(int pane);
    void enableSplit(bool vertical);
    void closeSplit();
    void switchPane();
    void switchPaneDirection(int dx, int dy);
    void syncBufferStateFromActivePane();
    void initSplitPanesFromBuffer();
    void switchToBufferInActivePane(int index);
    bool canSplit() const;

private:
    Editor& editor;
};
