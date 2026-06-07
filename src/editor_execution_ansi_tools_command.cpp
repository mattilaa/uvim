#include "ansi_tools_mode.h"
#include "editor.h"
#include "editor_execution_commands.h"
#include "mode_state_machine.h"

namespace command::execution
{
bool AnsiToolsCommand::execute(Editor& editor,
                               const CommandRequest& request) const
{
    if(request.text != "ansitools")
        return false;

    if(!editor.hasBuffer())
    {
        editor.setStatusMessage("ansitools: no buffer");
        return true;
    }

    if(auto* stateMachine = editor.getModeStateMachine())
        stateMachine->transitionTo(editor::statemachine::AnsiToolsMode{});

    editor.needsFullRedraw = true;
    return true;
}
} // namespace command::execution
