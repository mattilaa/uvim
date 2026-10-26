#include "git_commit_rewrite.h"

#include "os_compat.h"
#include "process_pipe.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace git_rewrite
{
namespace
{
struct GitRun
{
    int status = -1;
    std::string output;
};

GitRun git(const std::string& repoRoot, std::vector<std::string> args)
{
    std::vector<std::string> command = {"git", "-C", repoRoot};
    command.insert(command.end(), args.begin(), args.end());
    GitRun run;
    ProcessPipe pipe(command);
    if(!pipe)
        return run;
    run.output = pipe.readAll();
    run.status = pipe.close();
    return run;
}

std::string trim(std::string text)
{
    while(!text.empty() &&
          (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
        text.pop_back();
    return text;
}

std::string revParse(const std::string& repoRoot, const std::string& rev)
{
    GitRun run = git(repoRoot, {"rev-parse", "--verify", "--quiet", rev});
    return run.status == 0 ? trim(run.output) : std::string{};
}

// A file in the system temp directory holding `content`; removed when the
// guard goes out of scope.
struct TempFile
{
    std::string path;

    TempFile(const std::string& stem, const std::string& content)
    {
        path = os_make_temp_file(stem);
        if(!path.empty())
            std::ofstream(path, std::ios::binary | std::ios::trunc) << content;
    }

    ~TempFile()
    {
        std::error_code ec;
        if(!path.empty())
            std::filesystem::remove(path, ec);
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
};
} // namespace

std::vector<std::string> plainCommitDiff(
    const std::string& repoRoot,
    const std::string& hash)
{
    GitRun run =
        git(repoRoot,
            {"--no-pager",
             "show",
             "--format=",
             "--no-color",
             "--no-ext-diff",
             hash});
    std::vector<std::string> lines;
    size_t pos = 0;
    while(pos < run.output.size())
    {
        size_t next = run.output.find('\n', pos);
        if(next == std::string::npos)
            next = run.output.size();
        lines.push_back(run.output.substr(pos, next - pos));
        pos = next + 1;
    }
    return lines;
}

std::string checkRewritable(
    const std::string& repoRoot,
    const std::string& hash)
{
    const std::string full = revParse(repoRoot, hash + "^{commit}");
    if(full.empty())
        return "unknown commit " + hash;

    GitRun parents = git(repoRoot, {"rev-list", "--parents", "-n", "1", full});
    int words = 0;
    bool inWord = false;
    for(char ch : trim(parents.output))
    {
        if(ch == ' ')
            inWord = false;
        else if(!inWord)
        {
            inWord = true;
            ++words;
        }
    }
    if(words == 1)
        return "cannot rewrite the root commit";
    if(words > 2)
        return "cannot rewrite a merge commit";

    if(git(repoRoot, {"merge-base", "--is-ancestor", full, "HEAD"}).status != 0)
        return "commit is not on the current branch";

    if(!trim(git(repoRoot, {"rev-list", "--merges", full + "..HEAD"}).output)
            .empty())
        return "merge commits on top of it; rewrite them manually";

    const std::string gitDir =
        trim(git(repoRoot, {"rev-parse", "--absolute-git-dir"}).output);
    std::error_code ec;
    for(const char* busy :
        {"rebase-merge", "rebase-apply", "MERGE_HEAD", "CHERRY_PICK_HEAD"})
    {
        if(!gitDir.empty() &&
           std::filesystem::exists(std::filesystem::path(gitDir) / busy, ec))
            return "a rebase/merge is in progress";
    }

    if(!trim(git(repoRoot, {"status", "--porcelain", "--untracked-files=no"})
                 .output)
            .empty())
        return "working tree has uncommitted changes; commit or stash first";

    GitRun upstream =
        git(repoRoot,
            {"rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}"});
    if(upstream.status == 0 &&
       git(repoRoot, {"merge-base", "--is-ancestor", full, "@{u}"}).status == 0)
        return "commit is already pushed to " + trim(upstream.output) +
               "; rewriting it would need a force push";
    return {};
}

Result dropFromCommit(
    const std::string& repoRoot,
    const std::string& hash,
    const std::string& patch)
{
    Result result;
    if(std::string reason = checkRewritable(repoRoot, hash); !reason.empty())
    {
        result.message = reason;
        return result;
    }
    const std::string full = revParse(repoRoot, hash + "^{commit}");
    result.originalHead = revParse(repoRoot, "HEAD");

    TempFile patchFile("uvim-drop-patch", patch);
    if(patchFile.path.empty())
    {
        result.message = "cannot create a temp file";
        return result;
    }

    // Remove the hunks from the index only and amend: the commit loses them
    // while the working tree keeps them as unstaged changes.
    auto amendWithoutHunks = [&]() -> bool
    {
        if(git(repoRoot, {"apply", "-R", "--cached", patchFile.path}).status !=
           0)
        {
            result.message = "the dropped hunks do not apply to the commit";
            return false;
        }
        if(git(repoRoot,
               {"commit", "--amend", "--no-edit", "--allow-empty", "--quiet"})
               .status != 0)
        {
            result.message = "git commit --amend failed (hooks?)";
            return false;
        }
        return true;
    };

    if(full == result.originalHead)
    {
        if(!amendWithoutHunks())
            return result;
        result.newHash = result.newHead = revParse(repoRoot, "HEAD");
        result.ok = true;
        result.message = "Rewrote " + full.substr(0, 8) + " -> " +
                         result.newHash.substr(0, 8) +
                         "; dropped changes are unstaged in the working tree";
        return result;
    }

    // Older commit: stop at it with an "edit" rebase, amend, then replay the
    // commits on top while the dropped changes wait in a stash.
    const std::string onTop =
        trim(git(repoRoot, {"rev-list", "--count", full + "..HEAD"}).output);
    TempFile editor(
        "uvim-seq-editor",
        "awk 'NR==1{sub(/^pick/,\"edit\")}1' \"$1\" > \"$1.uvim\" "
        "&& mv \"$1.uvim\" \"$1\"\n");
    if(git(repoRoot,
           {"-c",
            // git runs the editor through sh (also on Windows, from Git for
            // Windows), which wants forward slashes. Temp paths have no '.
            "sequence.editor=sh '" +
                std::filesystem::path(editor.path).generic_string() + "'",
            "-c",
            "core.editor=true",
            "rebase",
            "-i",
            "--quiet",
            full + "^"})
               .status != 0 ||
       revParse(repoRoot, "HEAD") != full)
    {
        git(repoRoot, {"rebase", "--abort"});
        result.message = "could not start the rebase";
        return result;
    }
    if(!amendWithoutHunks())
    {
        git(repoRoot, {"rebase", "--abort"});
        return result;
    }
    const std::string rewritten = revParse(repoRoot, "HEAD");
    const bool stashed = git(repoRoot,
                             {"stash",
                              "push",
                              "--quiet",
                              "-m",
                              "uvim: hunks dropped from " + full.substr(0, 8)})
                             .status == 0;
    if(git(repoRoot, {"-c", "core.editor=true", "rebase", "--continue"})
           .status != 0)
    {
        // A later commit depends on the dropped hunks: restore everything.
        git(repoRoot, {"rebase", "--abort"});
        if(stashed)
            git(repoRoot, {"stash", "drop", "--quiet"});
        result.message = "later commits conflict without these hunks; "
                         "nothing was changed";
        return result;
    }
    result.newHead = revParse(repoRoot, "HEAD");
    result.newHash = revParse(repoRoot, "HEAD~" + onTop);
    if(result.newHash.empty())
        result.newHash = rewritten;
    result.ok = true;
    result.message = "Rewrote " + full.substr(0, 8) + " -> " +
                     result.newHash.substr(0, 8) + " (" + onTop +
                     " commit(s) replayed)";
    if(stashed && git(repoRoot, {"stash", "pop", "--quiet"}).status != 0)
        result.message += "; dropped changes kept in `git stash`";
    else
        result.message += "; dropped changes are unstaged in the working tree";
    return result;
}

bool undoDrop(
    const std::string& repoRoot,
    const std::string& originalHead,
    const std::string& newHead,
    std::string& message)
{
    if(revParse(repoRoot, "HEAD") != newHead)
    {
        message = "HEAD has moved since the rewrite; undo it manually "
                  "(original: " +
                  originalHead.substr(0, 8) + ")";
        return false;
    }
    // --mixed moves HEAD and the index back but never touches the working
    // tree, which still holds the dropped changes (= the original content).
    if(git(repoRoot, {"reset", "--mixed", "--quiet", originalHead}).status != 0)
    {
        message = "git reset failed; original was " + originalHead.substr(0, 8);
        return false;
    }
    message =
        "Restored the original commit (HEAD " + originalHead.substr(0, 8) + ")";
    return true;
}
} // namespace git_rewrite
