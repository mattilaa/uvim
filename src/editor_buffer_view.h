#pragma once

#include "editor_draw_component.h"
#include "mode.h"

class EditorBufferView : public EditorDrawComponent
{
public:
    using EditorDrawComponent::EditorDrawComponent;

    void draw();

private:
    int lastOffsetY = -1;
    int lastOffsetX = -1;
    int lastCursorY = -1;
    int lastVisualStartY = -1;
    int lastVisualEndY = -1;
    Mode lastMode = NORMAL;
};
