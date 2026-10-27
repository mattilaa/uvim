#include "editor.h"
#include "editor_execution_command_helpers.h"
#include "editor_execution_commands.h"
#include "editor_path_utilities.h"
#include "editor_utils.h"
#include "file_browser_mode.h"
#include "gitignore.h"
#include "mode_state_machine.h"
#include "process_pipe.h"

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

bool GitLogCommand::execute(Editor& editor, const CommandRequest& request) const
{
    if(request.trimmed != "git log")
        return false;
    editor.openGitLogMode();
    return true;
}

bool GitRebaseCommand::execute(
    Editor& editor,
    const CommandRequest& request) const
{
    if(!request.trimmed.starts_with("git rebase"))
        return false;
    if(request.trimmed.size() > 10 && request.trimmed[10] != ' ')
        return false;

    const std::string_view option =
        request.trimmed.size() > 10
            ? editor::helper::trim_view(request.trimmed.substr(10))
            : std::string_view{};
    if(option == "--interactive")
    {
        editor.openGitLogMode();
        auto* machine = editor.getModeStateMachine();
        auto* log = machine
                        ? machine->getState<editor::statemachine::GitLogMode>()
                        : nullptr;
        if(!log)
            return true;

        log->rebasePickMode = true;
        // Put the oldest commit targeted by a fixup at the bottom of the
        // initial tree, and select it as the default rebase starting point.
        size_t base = 0;
        bool found = false;
        for(size_t i = 0; i < log->entries.size(); ++i)
        {
            const std::string& subject = log->entries[i].subject;
            const size_t prefix = subject.starts_with("fixup!") ? 6
                                  : subject.starts_with("squash!") ? 7
                                                                    : 0;
            if(prefix == 0)
                continue;
            const std::string_view target = editor::helper::trim_view(
                std::string_view(subject).substr(prefix));
            for(size_t j = i + 1; j < log->entries.size(); ++j)
            {
                if(log->entries[j].subject == target)
                {
                    base = std::max(base, j);
                    found = true;
                    break;
                }
            }
        }
        if(found)
        {
            log->entries.resize(base + 1);
            log->rebuildFilter(editor);
            log->cursor = static_cast<int>(base);
            log->scrollOffset = log->cursor;
        }
        editor.needsFullRedraw = true;
        return true;
    }

    if(option != "--continue" && option != "--abort" &&
       option != "--skip" && option != "--quit")
    {
        editor.setStatusMessage(
            "git rebase: use --interactive, --continue, --abort, --skip, or --quit");
        return true;
    }

    std::string dir = editor.getProjectRoot();
    if(editor.currentBuffer && !editor.currentBuffer->filename.empty())
    {
        const fs::path file(editor.currentBuffer->filename);
        dir = file.has_parent_path() ? file.parent_path().string() : ".";
    }
    if(dir.empty())
        dir = ".";
    ProcessPipe pipe({"git", "-C", dir, "rebase", std::string(option)});
    const int status = pipe ? pipe.close() : -1;
    editor.setStatusMessage(
        status == 0 ? "git rebase: " + std::string(option) + " done"
                    : "git rebase: " + std::string(option) + " failed");
    return true;
}
} // namespace command::execution
