#include "editor_execution_command_helpers.h"
#include "editor_execution_commands.h"

namespace command::execution
{
bool FunctionsCommand::execute(Editor& editor, const CommandRequest& request)
    const
{
    if(request.text != "functions")
        return false;
    detail::requestMode(editor, FUNCTION_BROWSER);
    return true;
}
} // namespace command::execution
