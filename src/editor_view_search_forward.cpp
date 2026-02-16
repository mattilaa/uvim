#include "editor_view_search_forward.h"
#include "editor.h"

bool EditorViewSearchForward::draw(Editor& editor)
{
    editor.drawBufferView();
    return true;
}
