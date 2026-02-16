#include "editor_view_git_show.h"
#include "editor.h"
#include "mode_state_machine.h"

bool EditorViewGitShow::draw(Editor& editor)
{
    auto* stateMachine = editor.getModeStateMachine();
    if(!stateMachine)
        return true;
    if(auto* state = stateMachine->getState<GitShowCommitMode>())
    {
        state->draw(editor);
    }
    return true;
}
