#include "editor.h"
#include "editor_execution_command_helpers.h"
#include "editor_execution_commands.h"

namespace command::execution
{
bool ShowChangesCommand::execute(Editor& editor, const CommandRequest& request)
    const
{
    if(request.trimmed != "showchanges")
        return false;
    editor.openUnsavedChangesMode();
    return true;
}
} // namespace command::execution
