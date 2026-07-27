#include "editor_view_tool_info.h"
#include "editor.h"

bool EditorViewToolInfo::draw(Editor& editor)
{
    editor.drawToolInfo();
    return true;
}
