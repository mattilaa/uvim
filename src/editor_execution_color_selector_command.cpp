#include "color_selector_mode.h"
#include "editor.h"
#include "editor_execution_commands.h"
#include "mode_state_machine.h"

namespace command::execution
{
bool ColorSelectorCommand::execute(Editor& editor,
                                   const CommandRequest& request) const
{
    if(request.text != "colorselector" && request.text != "colorselector bg" &&
       request.text != "colorselector background")
        return false;

    if(!editor.hasBuffer())
    {
        editor.setStatusMessage("colorselector: no buffer");
        return true;
    }

    const bool background = request.text == "colorselector bg" ||
                            request.text == "colorselector background";
    if(auto* stateMachine = editor.getModeStateMachine())
        stateMachine->transitionTo(
            editor::statemachine::ColorSelectorMode{background});

    editor.needsFullRedraw = true;
    return true;
}
} // namespace command::execution
