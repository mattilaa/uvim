#include "color_picker_mode.h"
#include "editor.h"
#include "editor_execution_commands.h"
#include "mode_state_machine.h"

namespace command::execution
{
bool ColorPickerCommand::execute(Editor& editor,
                                 const CommandRequest& request) const
{
    if(request.text != "colorpicker")
        return false;

    if(!editor.hasBuffer())
    {
        editor.setStatusMessage("colorpicker: no buffer");
        return true;
    }

    if(auto* stateMachine = editor.getModeStateMachine())
        stateMachine->transitionTo(
            editor::statemachine::ColorPickerMode{false});

    editor.needsFullRedraw = true;
    return true;
}
} // namespace command::execution
