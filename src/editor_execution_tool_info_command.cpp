#include "editor.h"
#include "editor_execution_command_helpers.h"
#include "editor_execution_commands.h"
#include "mode_state_machine.h"

namespace command::execution
{
using namespace detail;

bool ToolInfoCommand::execute(Editor& editor,
                              const CommandRequest& request) const
{
    if(request.text != "toolinfo")
        return false;
    editor.showToolInfo();
    requestMode(editor, TOOL_INFO);
    return true;
}
} // namespace command::execution
