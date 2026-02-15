#include "editor_view_buffer_browser.h"
#include "editor.h"
#include "mode_state_machine.h"

bool EditorViewBufferBrowser::draw(Editor& editor)
{
    auto* stateMachine = editor.getModeStateMachine();
    if(!stateMachine)
        return true;
    if(auto* state = stateMachine->getState<BufferBrowserMode>())
    {
        state->draw(editor);
    }
    return true;
}
