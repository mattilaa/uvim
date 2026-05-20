#include "editor_view_git_commit.h"
#include "editor.h"
#include "mode_state_machine.h"

using namespace editor::statemachine;

bool EditorViewGitCommit::draw(Editor& editor)
{
    auto* stateMachine = editor.getModeStateMachine();
    if(!stateMachine)
        return true;
    if(auto* state = stateMachine->getState<GitCommitMode>())
    {
        state->draw(editor);
    }
    return true;
}
