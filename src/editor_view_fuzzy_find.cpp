#include "editor_view_fuzzy_find.h"
#include "editor.h"
#include "mode_state_machine.h"

using namespace editor::statemachine;

bool EditorViewFuzzyFind::draw(Editor& editor)
{
    auto* stateMachine = editor.getModeStateMachine();
    if(!stateMachine)
        return true;
    if(auto* state = stateMachine->getState<FuzzyFindMode>())
    {
        state->draw(editor);
    }
    return true;
}
