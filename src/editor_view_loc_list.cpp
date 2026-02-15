#include "editor_view_loc_list.h"
#include "editor.h"
#include "mode_state_machine.h"

bool EditorViewLocList::draw(Editor& editor)
{
    auto* stateMachine = editor.getModeStateMachine();
    if(!stateMachine)
        return true;
    if(auto* state = stateMachine->getState<LocListMode>())
    {
        state->draw(editor);
    }
    return true;
}
