#include "editor.h"
#include "editor_execution_commands.h"
#include "process_pipe.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace
{
std::string gitLine(const std::vector<std::string>& args)
{
    ProcessPipe pipe(args);
    if(!pipe)
        return {};
    const std::string line = pipe.readLine();
    return pipe.close() == 0 ? line : std::string{};
}

std::vector<std::string> gitLines(const std::vector<std::string>& args)
{
    ProcessPipe pipe(args);
    if(!pipe)
        return {};
    std::vector<std::string> lines;
    while(true)
    {
        std::string line = pipe.readLine();
        if(line.empty())
            break;
        lines.push_back(std::move(line));
    }
    return pipe.close() == 0 ? lines : std::vector<std::string>{};
}

std::string pushDirectory(const Editor& editor)
{
    if(editor.currentBuffer && !editor.currentBuffer->filename.empty())
    {
        const std::filesystem::path file(editor.currentBuffer->filename);
        return file.has_parent_path() ? file.parent_path().string() : ".";
    }
    return editor.getProjectRoot().empty() ? "." : editor.getProjectRoot();
}

bool runPush(Editor& editor, const std::vector<std::string>& args)
{
    ProcessPipe pipe;
    if(!pipe.open(args, "r", true))
    {
        editor.setStatusMessage("git push: could not start git");
        return false;
    }
    pipe.readAll();
    const bool success = pipe.close() == 0;
    editor.setStatusMessage(success ? "git push: done" : "git push: failed");
    return success;
}
} // namespace

namespace command::execution
{
bool GitPushCommand::execute(Editor& editor, const CommandRequest& request) const
{
    if(request.trimmed != "git push")
        return false;

    const std::string dir = pushDirectory(editor);
    const std::string branch = gitLine(
        {"git", "-C", dir, "symbolic-ref", "--quiet", "--short", "HEAD"});
    if(branch.empty())
    {
        editor.setStatusMessage("git push: no current branch or repository");
        return true;
    }

    const std::string upstream = gitLine(
        {"git", "-C", dir, "for-each-ref", "--format=%(upstream)",
         "refs/heads/" + branch});
    if(!upstream.empty())
    {
        runPush(editor, {"git", "-C", dir, "push"});
        return true;
    }

    const std::vector<std::string> remotes =
        gitLines({"git", "-C", dir, "remote"});
    std::string remote;
    if(std::find(remotes.begin(), remotes.end(), "origin") != remotes.end())
        remote = "origin";
    else if(remotes.size() == 1)
        remote = remotes.front();
    else if(remotes.empty())
    {
        editor.setStatusMessage("git push: no remote configured");
        return true;
    }
    else
    {
        editor.setStatusMessage(
            "git push: multiple remotes; configure an upstream first");
        return true;
    }

    editor.pendingGitPush = Editor::PendingGitPush{dir, remote, branch};
    editor.setStatusMessage(
        "Push new branch? [y/N] " + branch + " -> " + remote +
        " (set upstream)");
    editor.needsFullRedraw = true;
    return true;
}
} // namespace command::execution

bool Editor::handlePendingGitPushKey(int key)
{
    if(!pendingGitPush)
        return false;

    const PendingGitPush pending = std::move(*pendingGitPush);
    pendingGitPush.reset();
    if(key != 'y' && key != 'Y')
    {
        setStatusMessage("git push: canceled");
        needsFullRedraw = true;
        return true;
    }

    runPush(*this,
            {"git", "-C", pending.repoDir, "push", "-u", pending.remote,
             pending.branch});
    needsFullRedraw = true;
    return true;
}
