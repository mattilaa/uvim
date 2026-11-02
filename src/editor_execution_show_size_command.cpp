#include "editor.h"
#include "editor_execution_commands.h"

namespace command::execution
{
bool ShowSizeCommand::execute(Editor& editor, const CommandRequest& request)
    const
{
    if(request.trimmed == "ss" || request.trimmed.starts_with("ss "))
    {
        editor.setStatusMessage(
            "ss: pause the debugger and use :ss [expression]");
        return true;
    }
    if(request.trimmed != "gs")
        return false;
    editor.openSizePopupForCursor();
    return true;
}

} // namespace command::execution
