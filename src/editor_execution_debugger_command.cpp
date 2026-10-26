#include "editor_execution_command_helpers.h"
#include "editor_execution_commands.h"

namespace command::execution
{
bool DebuggerCommand::execute(Editor& editor, const CommandRequest& request)
    const
{
    const auto text = request.trimmed;
    const bool attach = text == "dbgattach" || text.rfind("dbgattach ", 0) == 0;
    const bool run = text == "dbgrun" || text.rfind("dbgrun ", 0) == 0;
    if(!attach && !run)
        return false;
    const size_t prefix = attach ? 9 : 6;
    detail::requestMode(
        editor,
        DEBUGGER,
        std::string(attach ? "a " : "r ") +
            (text.size() > prefix ? std::string(text.substr(prefix + 1)) : ""));
    return true;
}
} // namespace command::execution
