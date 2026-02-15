#include "editor_view_replace.h"
#include "editor.h"

bool EditorViewReplace::draw(Editor& editor)
{
    editor.drawBufferView();
    return true;
}
