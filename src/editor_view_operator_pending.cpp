#include "editor_view_operator_pending.h"
#include "editor.h"

bool EditorViewOperatorPending::draw(Editor& editor)
{
    editor.drawBufferView();
    return true;
}
