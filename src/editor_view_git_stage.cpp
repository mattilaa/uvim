#include "editor_view_git_stage.h"
#include "editor.h"
#include "mode_state_machine.h"

using namespace editor::statemachine;

bool EditorViewGitStage::draw(Editor& editor)
{
    auto* stateMachine = editor.getModeStateMachine();
    if(!stateMachine)
        return true;
    if(auto* state = stateMachine->getState<GitStageMode>())
    {
        state->draw(editor);
    }
    return true;
}
