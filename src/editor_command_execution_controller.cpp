#include "editor.h"
#include "editor_command_controller.h"
#include "editor_execution_commands.h"
#include "editor_utils.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

void EditorCommandController::executeCommand(std::string_view cmd)
{
    if(!cmd.empty())
    {
        if(editor.commandHistory.empty() || editor.commandHistory.back() != cmd)
            editor.commandHistory.push_back(std::string(cmd));
        editor.commandHistoryIndex = -1;
    }

    command::execution::CommandRequest request{std::string(cmd),
                                               editor::helper::trim_view(cmd)};
    static const std::vector<
        std::unique_ptr<command::execution::EditorExecutionCommand>>
        commands = command::execution::buildCommands();

    for(const auto& command : commands)
    {
        if(command->execute(editor, request))
            return;
    }
}
