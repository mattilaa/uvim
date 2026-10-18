#include "editor.h"
#include "editor_execution_command_helpers.h"
#include "editor_execution_commands.h"
#include "mode_state_machine.h"

namespace command::execution
{
using namespace detail;

bool PaletteCommand::execute(Editor& editor, const CommandRequest& request) const
{
    if(request.text != "palette")
        return false;
    requestMode(editor, COMMAND_PALETTE);
    return true;
}
} // namespace command::execution
