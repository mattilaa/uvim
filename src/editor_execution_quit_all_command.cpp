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

bool QuitAllCommand::execute(Editor& editor,
                             const CommandRequest& request) const
{
    if(request.text == "qall!" || request.text == "qa!")
    {
        clearAndExit();
    }
    if(request.text != "qall" && request.text != "qa")
        return false;
    if(hasDirtyBuffers(editor))
        editor.setStatusMessage(
            "Some buffers have unsaved changes (add ! to override)");
    else
        clearAndExit();
    return true;
}
} // namespace command::execution
