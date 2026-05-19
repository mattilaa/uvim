#include "editor.h"
#include "editor_execution_command_helpers.h"
#include "editor_execution_commands.h"
#include "editor_path_utilities.h"
#include "editor_utils.h"
#include "file_browser_mode.h"
#include "gitignore.h"
#include "mode_state_machine.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace command::execution
{
using editor::helper::collectLocFiles;
using editor::helper::expandTildePath;
using editor::helper::locCommentRulesForPath;
using editor::helper::locCountInFile;
using editor::helper::locCountInLines;
using editor::helper::locIsTextFile;
using editor::helper::parse_int;
using editor::helper::trim_view;
using namespace detail;

bool HelpCommand::execute(Editor& editor, const CommandRequest& request) const
{
    const std::string& cmd = request.text;
    if(cmd != "help" && cmd != "h" && cmd.rfind("help ", 0) != 0 &&
       cmd.rfind("h ", 0) != 0)
        return false;

    std::string topic;
    if(cmd.rfind("help ", 0) == 0)
        topic = cmd.substr(5);
    else if(cmd.rfind("h ", 0) == 0)
        topic = cmd.substr(2);

    requestMode(editor, HELP, topic);
    return true;
}
} // namespace command::execution
