#include "editor_view_git_patch.h"
#include "editor.h"
#include "mode_state_machine.h"

bool EditorViewGitPatch::draw(Editor& editor)
{
    auto* stateMachine = editor.getModeStateMachine();
    if(!stateMachine)
        return true;
    if(auto* state = stateMachine->getState<GitPatchMode>())
    {
        state->draw(editor);
    }
    return true;
}
