#include "editor_view_visual_block.h"
#include "editor.h"

bool EditorViewVisualBlock::draw(Editor& editor)
{
    editor.drawBufferView();
    return true;
}
