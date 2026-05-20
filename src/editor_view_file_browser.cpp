#include "editor_view_file_browser.h"
#include "editor.h"
#include "mode_state_machine.h"

using namespace editor::statemachine;

bool EditorViewFileBrowser::draw(Editor& editor)
{
    auto* stateMachine = editor.getModeStateMachine();
    if(!stateMachine)
        return true;
    if(auto* state = stateMachine->getState<FileBrowserMode>())
    {
        state->draw(editor);
    }
    return true;
}
