#include "editor_execution_command_helpers.h"
#include "editor_execution_commands.h"

namespace command::execution
{
bool AsmCommand::execute(Editor& editor, const CommandRequest& request) const
{
    if(request.trimmed != "asm" && request.trimmed.rfind("asm ", 0) != 0)
        return false;
    detail::requestMode(
        editor,
        ASSEMBLY,
        request.trimmed.size() > 3 ? std::string(request.trimmed.substr(4))
                                   : "");
    return true;
}
} // namespace command::execution
