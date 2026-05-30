#include "color_selector_mode.h"
#include "editor.h"
#include "editor_execution_commands.h"
#include "mode_state_machine.h"

namespace command::execution
{
bool ColorSelectorCommand::execute(Editor& editor,
                                   const CommandRequest& request) const
{
    if(request.text != "colorselector")
        return false;

    if(!editor.hasBuffer())
    {
        editor.setStatusMessage("colorselector: no buffer");
        return true;
    }

    if(auto* stateMachine = editor.getModeStateMachine())
        stateMachine->transitionTo(editor::statemachine::ColorSelectorMode{});

    editor.needsFullRedraw = true;
    return true;
}
} // namespace command::execution
