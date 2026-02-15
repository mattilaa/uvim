#include "editor_view_visual_line.h"
#include "editor.h"

bool EditorViewVisualLine::draw(Editor& editor)
{
    editor.drawBufferView();
    return true;
}
