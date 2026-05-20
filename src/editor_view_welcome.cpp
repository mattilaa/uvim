#include "editor_view_welcome.h"
#include "editor.h"
#include "mode_state_machine.h"

using namespace editor::statemachine;

bool EditorViewWelcome::draw(Editor& editor)
{
    auto* stateMachine = editor.getModeStateMachine();
    if(!stateMachine)
        return true;
    if(auto* state = stateMachine->getState<WelcomeMode>())
    {
        state->draw(editor);
    }
    return true;
}
