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

bool EditCommand::execute(Editor& editor, const CommandRequest& request) const
{
    if(request.text.rfind("e ", 0) != 0 && request.text.rfind("edit ", 0) != 0)
        return false;
    std::string path = request.text.rfind("e ", 0) == 0
                           ? request.text.substr(2)
                           : request.text.substr(5);
    return openEditTarget(editor, path);
}
} // namespace command::execution
