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

bool LocCommand::execute(Editor& editor, const CommandRequest& request) const
{
    bool listView = false;
    std::string path;
    if(!parse(request.text, listView, path))
        return false;
    return run(editor, listView, path);
}

bool LocCommand::parse(std::string_view command, bool& listView,
                       std::string& outPath)
{
    std::string_view trimmed = trim_view(command);
    if(trimmed.rfind("loc", 0) != 0 || trimmed.rfind("loctotal", 0) == 0)
        return false;

    std::string_view rest = trim_view(trimmed.substr(3));
    listView = false;
    if(!rest.empty() && rest.front() == '!')
    {
        listView = true;
        rest = trim_view(rest.substr(1));
    }
    if(rest.rfind("-l", 0) == 0)
    {
        listView = true;
        rest = trim_view(rest.substr(2));
    }
    else if(rest.rfind("--list", 0) == 0)
    {
        listView = true;
        rest = trim_view(rest.substr(6));
    }
    else if(rest.rfind("list", 0) == 0)
    {
        listView = true;
        rest = trim_view(rest.substr(4));
    }
    outPath = rest.empty() ? "" : std::string(rest);
    return true;
}

bool LocCommand::run(Editor& editor, bool listView, std::string path)
{
    bool explicitBuffer = false;
    if(path.empty())
    {
        if(editor.hasBuffer() && editor.filename && !editor.filename->empty())
        {
            path = *editor.filename;
            explicitBuffer = true;
        }
        else
            path = ".";
    }
    else if(path == "%")
    {
        if(editor.hasBuffer() && editor.filename && !editor.filename->empty())
        {
            path = *editor.filename;
            explicitBuffer = true;
        }
        else
        {
            editor.setStatusMessage("loc: no current buffer");
            return true;
        }
    }

    path = expandTildePath(path);
    std::error_code ec;
    std::filesystem::path targetPath = std::filesystem::absolute(path, ec);
    if(ec)
        targetPath = std::filesystem::path(path);
    if(!std::filesystem::exists(targetPath, ec))
    {
        editor.setStatusMessage("loc: path not found: " + path);
        return true;
    }

    std::vector<std::string> files;
    std::filesystem::path rootPath;
    std::string rootDisplay = path;
    bool useBufferForSingle = explicitBuffer;
    if(editor.hasBuffer() && editor.filename && !editor.filename->empty())
    {
        std::error_code currErr;
        std::filesystem::path currentPath =
            std::filesystem::absolute(*editor.filename, currErr);
        if(currErr)
            currentPath = std::filesystem::path(*editor.filename);
        std::error_code eqErr;
        if(std::filesystem::equivalent(targetPath, currentPath, eqErr))
            useBufferForSingle = true;
    }

    if(std::filesystem::is_directory(targetPath, ec))
    {
        listView = true;
        rootPath = targetPath;
        GitIgnore gitignore;
        if(editor.respectGitignore)
            gitignore.loadRecursive(rootPath.string());
        collectLocFiles(rootPath.string(), 0, gitignore, files);
    }
    else
    {
        rootPath = targetPath.parent_path();
        files.push_back(targetPath.string());
    }

    std::vector<Editor::LocEntry> entries;
    int totalLoc = 0;
    for(const auto& file : files)
    {
        if(!locIsTextFile(file))
            continue;

        auto rules = locCommentRulesForPath(file);
        int loc = (!listView && useBufferForSingle)
                      ? locCountInLines(*editor.lines, rules)
                      : locCountInFile(file, rules);
        totalLoc += loc;

        if(listView)
        {
            Editor::LocEntry entry;
            entry.path = file;
            entry.displayPath = file;
            if(!rootPath.empty())
            {
                std::error_code relErr;
                std::filesystem::path rel =
                    std::filesystem::relative(file, rootPath, relErr);
                if(!relErr)
                    entry.displayPath = rel.string();
            }
            entry.loc = loc;
            entries.push_back(std::move(entry));
        }
    }

    if(listView)
    {
        std::sort(entries.begin(), entries.end(),
                  [](const Editor::LocEntry& a, const Editor::LocEntry& b)
                  { return a.displayPath < b.displayPath; });
        editor.locList = std::move(entries);
        editor.locListTotal = totalLoc;
        editor.locListRoot = rootDisplay;
        requestMode(editor, LOC_LIST);
        editor.commandRequestedReturnMode.reset();
        editor.commandRequestedBrowseCursor = 0;
        editor.commandRequestedBrowseOffset = 0;
        editor.commandRequestedBrowseDirectory.clear();
    }

    editor.statusMessage.clear();
    editor.locMessage = "LOC " + std::to_string(totalLoc);
    editor.needsFullRedraw = true;
    return true;
}
} // namespace command::execution
