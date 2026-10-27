#include "editor.h"
#include "editor_execution_commands.h"

namespace command::execution
{
bool GitStatusCommand::execute(
    Editor& editor,
    const CommandRequest& request) const
{
    if(request.trimmed != "git status")
        return false;
    editor.openGitStatusMode();
    return true;
}
} // namespace command::execution
