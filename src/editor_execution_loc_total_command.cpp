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

bool LocTotalCommand::execute(Editor& editor,
                              const CommandRequest& request) const
{
    std::string path;
    if(!parse(request.text, path))
        return false;
    if(path.empty())
        path = editor.projectRoot.empty() ? "." : editor.projectRoot;
    path = expandTildePath(path);

    std::error_code ec;
    std::filesystem::path targetPath = std::filesystem::absolute(path, ec);
    if(ec)
        targetPath = std::filesystem::path(path);
    if(!std::filesystem::exists(targetPath, ec))
    {
        editor.setStatusMessage("loctotal: path not found: " + path);
        return true;
    }

    std::vector<std::string> files;
    if(std::filesystem::is_directory(targetPath, ec))
    {
        GitIgnore gitignore;
        if(editor.respectGitignore)
            gitignore.loadRecursive(targetPath.string());
        collectLocFiles(targetPath.string(), 0, gitignore, files);
    }
    else
        files.push_back(targetPath.string());

    int totalLoc = 0;
    for(const auto& file : files)
    {
        if(!locIsTextFile(file))
            continue;
        totalLoc += locCountInFile(file, locCommentRulesForPath(file));
    }

    editor.statusMessage.clear();
    editor.locMessage = "LOC total " + std::to_string(totalLoc);
    editor.needsFullRedraw = true;
    return true;
}

bool LocTotalCommand::parse(std::string_view command, std::string& outPath)
{
    std::string_view trimmed = trim_view(command);
    if(trimmed.rfind("loctotal", 0) != 0)
        return false;
    std::string_view rest = trim_view(trimmed.substr(8));
    outPath = rest.empty() ? "" : std::string(rest);
    return true;
}
} // namespace command::execution
