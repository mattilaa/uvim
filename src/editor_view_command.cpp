#include "editor_view_command.h"
#include "editor.h"

bool EditorViewCommand::draw(Editor& editor)
{
    editor.drawBufferView();
    return true;
}
