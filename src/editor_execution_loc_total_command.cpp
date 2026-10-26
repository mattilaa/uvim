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
#include <memory>
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

bool LocTotalCommand::execute(Editor& editor, const CommandRequest& request)
    const
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

    // Walking and counting a large tree takes a while; do it in the
    // background and report through the status bar / :tasks.
    struct Count
    {
        int totalLoc = 0;
        int files = 0;
    };

    auto count = std::make_shared<Count>();
    const std::string root = targetPath.string();
    const bool isDirectory = std::filesystem::is_directory(targetPath, ec);
    const bool respectGitignore = editor.respectGitignore;
    Editor* ed = &editor;

    editor.submitTask(
        "loctotal " + path,
        "loc",
        [count, root, isDirectory, respectGitignore](TaskQueue::Context& ctx)
        {
            std::vector<std::string> files;
            if(isDirectory)
            {
                ctx.detail("listing files");
                GitIgnore gitignore;
                if(respectGitignore)
                    gitignore.loadRecursive(root);
                collectLocFiles(
                    root,
                    0,
                    gitignore,
                    files,
                    [&ctx] { return ctx.cancelled(); });
                ctx.detail({});
            }
            else
                files.push_back(root);

            const long long total = (long long)files.size();
            long long scanned = 0;
            for(const auto& file : files)
            {
                if(ctx.cancelled())
                    return;
                ctx.progress(++scanned, total);
                if(!locIsTextFile(file))
                    continue;
                count->totalLoc +=
                    locCountInFile(file, locCommentRulesForPath(file));
                ++count->files;
            }
        },
        [ed, count]
        {
            ed->statusMessage.clear();
            ed->locMessage = "LOC total " + std::to_string(count->totalLoc);
            ed->needsFullRedraw = true;
        });
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
