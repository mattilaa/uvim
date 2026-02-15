#include "editor_view_grep_search.h"
#include "editor.h"
#include "mode_state_machine.h"

bool EditorViewGrepSearch::draw(Editor& editor)
{
    auto* stateMachine = editor.getModeStateMachine();
    if(!stateMachine)
        return true;
    if(auto* state = stateMachine->getState<GrepSearchMode>())
    {
        state->draw(editor);
    }
    return true;
}
