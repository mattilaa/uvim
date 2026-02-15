#include "editor_view_lsp_info.h"
#include "editor.h"

bool EditorViewLspInfo::draw(Editor& editor)
{
    editor.drawLspInfo();
    return true;
}
