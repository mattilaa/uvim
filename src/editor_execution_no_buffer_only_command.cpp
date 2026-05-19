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

bool NoBufferOnlyCommand::execute(Editor& editor,
                                  const CommandRequest& request) const
{
    if(editor.hasBuffer())
        return false;
    if(request.text == "ls" || request.text == "buffers" ||
       request.text == "bn" || request.text == "bnext" ||
       request.text == "bp" || request.text == "bprev" ||
       request.text == "bprevious" || request.text == "bd" ||
       request.text == "bdelete")
    {
        editor.setStatusMessage("No buffers");
        return true;
    }
    return false;
}
} // namespace command::execution
