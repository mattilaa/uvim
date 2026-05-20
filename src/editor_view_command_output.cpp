#include "editor_view_command_output.h"
#include "editor.h"
#include "mode_state_machine.h"

using namespace editor::statemachine;

bool EditorViewCommandOutput::draw(Editor& editor)
{
    auto* stateMachine = editor.getModeStateMachine();
    if(!stateMachine)
        return true;
    if(auto* state = stateMachine->getState<CommandOutputMode>())
    {
        state->draw(editor);
    }
    return true;
}
