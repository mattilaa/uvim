#include "color_selector_mode.h"
#include "editor.h"
#include "editor_execution_commands.h"
#include "mode_state_machine.h"

#include <optional>
#include <string>

namespace command::execution
{
bool ColorSelectorCommand::execute(Editor& editor,
                                   const CommandRequest& request) const
{
    const bool editExisting = request.text == "colorselect";
    const bool plainSelector = request.text == "colorselector";
    if(editExisting || plainSelector)
    {
        if(!editor.hasBuffer())
        {
            editor.setStatusMessage(std::string(request.text) + ": no buffer");
            return true;
        }

        std::optional<editor::statemachine::ColorSelectorMode> mode =
            editor::statemachine::ColorSelectorMode::
                fromAnsiLiteralAtCursorAndRemove(editor);
        if(mode)
        {
            if(auto* stateMachine = editor.getModeStateMachine())
                stateMachine->transitionTo(*mode);

            editor.needsFullRedraw = true;
            return true;
        }

        if(editExisting)
        {
            editor.setStatusMessage(
                "colorselect: no ANSI color literal under cursor");
            return true;
        }
    }

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
