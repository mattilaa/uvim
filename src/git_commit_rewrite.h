#pragma once

#include <string>
#include <vector>

// Rewrites an existing commit without some of its hunks (git log -> commit
// view -> d ... c). The dropped changes are kept as unstaged edits in the
// working tree, so nothing is lost. All functions run git in `repoRoot`.
namespace git_rewrite
{
// `git show --format= --no-color <hash>`: the commit's plain diff, used to
// build the patch of dropped hunks.
std::vector<std::string> plainCommitDiff(
    const std::string& repoRoot,
    const std::string& hash);

// Empty when `hash` can be rewritten; otherwise why not (dirty working
// tree, pushed commit, merge/root commit, merges on top, ...).
std::string checkRewritable(
    const std::string& repoRoot,
    const std::string& hash);

struct Result
{
    bool ok = false;
    std::string message;
    std::string newHash;      // the rewritten commit
    std::string originalHead; // HEAD before the rewrite (for undo)
    std::string newHead;      // HEAD after the rewrite
};

// Removes `patch` (a unified diff of hunks of `hash`) from that commit:
// amend for HEAD, otherwise rebase the commits on top. On a conflict the
// rebase is aborted and the repository is left as it was.
Result dropFromCommit(
    const std::string& repoRoot,
    const std::string& hash,
    const std::string& patch);

// Undo dropFromCommit: `git reset --mixed` back to `originalHead` (only if
// HEAD is still `newHead`). The working tree is never touched; it already
// holds the original content, so the result is clean again.
bool undoDrop(
    const std::string& repoRoot,
    const std::string& originalHead,
    const std::string& newHead,
    std::string& message);
} // namespace git_rewrite
