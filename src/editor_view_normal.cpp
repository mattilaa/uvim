#include "editor_view_normal.h"
#include "editor.h"

bool EditorViewNormal::draw(Editor& editor)
{
    editor.drawBufferView();
    return true;
}
