#include "editor_view_visual.h"
#include "editor.h"

bool EditorViewVisual::draw(Editor& editor)
{
    editor.drawBufferView();
    return true;
}
