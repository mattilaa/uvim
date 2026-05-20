#include "editor_view_help.h"
#include "editor.h"
#include "mode_state_machine.h"

using namespace editor::statemachine;

bool EditorViewHelp::draw(Editor& editor)
{
    if(!editor.needsFullRedraw)
        return true;
    auto* stateMachine = editor.getModeStateMachine();
    if(!stateMachine)
        return true;
    if(auto* state = stateMachine->getState<HelpMode>())
    {
        state->draw(editor);
    }
    return true;
}
