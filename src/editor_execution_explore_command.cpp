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

bool ExploreCommand::execute(Editor& editor,
                             const CommandRequest& request) const
{
    std::string path;
    if(!parse(request.text, path))
        return false;
    requestMode(editor, FILE_BROWSER, path);
    return true;
}

bool ExploreCommand::parse(std::string_view command, std::string& outPath)
{
    if(command == "Ex" || command == "ex" || command == "E" ||
       command == "Explore" || command == "explore")
    {
        outPath = ".";
        return true;
    }

    auto starts = [&](std::string_view prefix)
    { return command.rfind(prefix, 0) == 0; };
    if(starts("Ex ") || starts("ex ") || starts("E ") || starts("Explore ") ||
       starts("explore "))
    {
        std::string_view rest = command.substr(command.find(' ') + 1);
        rest = trim_view(rest);
        outPath = rest.empty() ? "." : std::string(rest);
        return true;
    }
    return false;
}
} // namespace command::execution
