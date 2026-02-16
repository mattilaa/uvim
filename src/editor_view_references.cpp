#include "editor_view_references.h"
#include "editor.h"

bool EditorViewReferences::draw(Editor& editor)
{
    editor.drawReferences();
    return true;
}
