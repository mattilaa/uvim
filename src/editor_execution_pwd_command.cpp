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

bool PwdCommand::execute(Editor& editor, const CommandRequest& request) const
{
    if(request.text != "pwd")
        return false;
    fs::path cwd = EditorPathUtilities::currentWorkingDirectory();
    if(!cwd.empty())
        editor.setStatusMessage(cwd.string());
    else
        editor.setStatusMessage("Error getting current directory");
    return true;
}
} // namespace command::execution
