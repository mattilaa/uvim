#include "editor_view_insert.h"
#include "editor.h"

bool EditorViewInsert::draw(Editor& editor)
{
    editor.drawBufferView();
    return true;
}
