#include "editor_view_search_backward.h"
#include "editor.h"

bool EditorViewSearchBackward::draw(Editor& editor)
{
    editor.drawBufferView();
    return true;
}
