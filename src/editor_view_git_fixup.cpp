#include "editor_view_git_fixup.h"
#include "editor.h"
#include "mode_state_machine.h"

bool EditorViewGitFixup::draw(Editor& editor)
{
    auto* stateMachine = editor.getModeStateMachine();
    if(!stateMachine)
        return true;
    if(auto* state = stateMachine->getState<GitFixupMode>())
    {
        state->draw(editor);
    }
    return true;
}
