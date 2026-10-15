#include "editor.h"
#include "editor_execution_command_helpers.h"
#include "editor_execution_commands.h"
#include "mode_state_machine.h"

namespace command::execution
{
using namespace detail;

bool TasksCommand::execute(Editor& editor, const CommandRequest& request) const
{
    if(request.text != "tasks")
        return false;
    requestMode(editor, TASK_LIST);
    return true;
}
} // namespace command::execution
