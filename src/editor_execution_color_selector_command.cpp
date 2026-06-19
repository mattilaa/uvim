#include "color_selector_mode.h"
#include "editor.h"
#include "editor_execution_commands.h"
#include "mode_state_machine.h"

#include <optional>

namespace command::execution
{
bool ColorSelectorCommand::execute(Editor& editor,
                                   const CommandRequest& request) const
{
    if(request.text != "colorselect")
        return false;

    if(!editor.hasBuffer())
    {
        editor.setStatusMessage("colorselect: no buffer");
        return true;
    }

    std::optional<VisualColorRange> visualRange = editor.takeVisualColorRange();
    std::optional<editor::statemachine::ColorSelectorMode> mode;
    if(visualRange)
        mode = editor::statemachine::ColorSelectorMode::forVisualRange(
            *visualRange);
    else
        mode = editor::statemachine::ColorSelectorMode::
            fromAnsiLiteralAtCursorAndRemove(editor);
    if(auto* stateMachine = editor.getModeStateMachine())
    {
        stateMachine->transitionTo(
            mode.value_or(editor::statemachine::ColorSelectorMode{false}));
    }

    editor.needsFullRedraw = true;
    return true;
}
} // namespace command::execution
