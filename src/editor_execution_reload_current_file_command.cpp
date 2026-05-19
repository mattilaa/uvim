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

bool ReloadCurrentFileCommand::execute(Editor& editor,
                                       const CommandRequest& request) const
{
    bool force = false;
    if(!matches(request.text, force))
        return false;
    if(!editor.hasBuffer() || !editor.filename || editor.filename->empty())
    {
        editor.setStatusMessage("No file to reload");
        return true;
    }
    if(*editor.dirty && !force)
    {
        editor.setStatusMessage(
            "No write since last change (use :e!% to discard)");
        return true;
    }
    editor.reloadCurrentFile();
    return true;
}

bool ReloadCurrentFileCommand::matches(std::string_view command, bool& force)
{
    std::string_view trimmed = trim_view(command);
    auto check = [&](std::string_view prefix) -> bool
    {
        if(trimmed.rfind(prefix, 0) != 0)
            return false;
        std::string_view rest = trim_view(trimmed.substr(prefix.size()));
        if(!rest.empty() && rest.front() == '!')
        {
            force = true;
            rest = trim_view(rest.substr(1));
        }
        return rest == "%";
    };
    return check("e") || check("edit");
}
} // namespace command::execution
