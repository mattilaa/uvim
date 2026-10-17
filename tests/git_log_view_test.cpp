#include "editor.h"
#include "mode_state_machine.h"
#include "git_commit_rewrite.h"
#include "process_pipe.h"
#include "real_mode_test_utils.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

using namespace editor::statemachine;

namespace
{
std::filesystem::path make_temp_dir(const std::string& prefix)
{
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path base =
        std::filesystem::temp_directory_path() / (prefix + std::to_string(now));
    std::filesystem::create_directories(base);
    return base;
}

void write_file(const std::filesystem::path& path, std::string_view content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}

int run_cmd(const std::string& cmd)
{
    return std::system(cmd.c_str());
}

std::string run_cmd_line(const std::vector<std::string>& args)
{
    ProcessPipe pipe(args);
    return pipe ? pipe.readLine() : "";
}

// `git --version` through ProcessPipe: no shell redirection, so it works
// the same everywhere.
bool hasGit()
{
    ProcessPipe pipe({"git", "--version"});
    if(!pipe)
        return false;
    pipe.readAll();
    return pipe.close() == 0;
}

void dispatch_string(ModeStateMachine& sm, const std::string& text)
{
    for(char c : text)
        sm.dispatch(static_cast<int>(c));
}
} // namespace

TEST(GitSearchHighlightTest, GitLogHighlightsOnlyMatches)
{
    Editor editor = Editor::createForTests();
    GitLogMode::Entry entry{"abc123", "fix abc now"};

    std::string out =
        GitLogMode::testRenderLine(editor.theme, entry, "abc", false, 80);

    std::string matchSeq = editor.theme.searchMatch();
    std::string normalText = editor.theme.reset() + editor.theme.baseFg();

    EXPECT_TRUE(text_utils::is_found(
        out.find(matchSeq + std::string("abc") + normalText)));
}

TEST(GitLogRenderTest, RendersTigStyleColumnsAndRefs)
{
    Editor editor = Editor::createForTests();
    std::string raw = "* ";
    raw += '\x1e';
    raw += "abc123";
    raw += '\x1f';
    raw += "2026-05-27 16:28 +0300";
    raw += '\x1f';
    raw += "Matti Laamanen";
    raw += '\x1f';
    raw += "HEAD -> main, origin/main, origin/HEAD";
    raw += '\x1f';
    raw += "parent1 parent2";
    raw += '\x1f';
    raw += "Merge branch 'feature/refactor_state_machines'";
    auto entry = GitLogMode::parseGraphEntry(raw);
    ASSERT_TRUE(entry.has_value());

    std::string out =
        GitLogMode::testRenderLine(editor.theme, *entry, "", false, 140);

    EXPECT_TRUE(text_utils::is_found(out.find("abc123")));
    EXPECT_TRUE(text_utils::is_found(out.find("<Matti Laamanen>")));
    EXPECT_EQ(entry->graph, "* ");
    EXPECT_TRUE(text_utils::is_found(out.find("[main]")));
    EXPECT_TRUE(text_utils::is_found(out.find("{origin/main}")));
    EXPECT_TRUE(text_utils::is_found(out.find("{origin/HEAD}")));
}

TEST(GitLogRenderTest, AppliesDroppedConnectorRowsToNextCommit)
{
    std::string raw = "* ";
    raw += '\x1e';
    raw += "abc123";
    raw += '\x1f';
    raw += "2026-05-24 19:28 +0300";
    raw += '\x1f';
    raw += "Matti Laamanen";
    raw += '\x1f';
    raw += "feature/move_code";
    raw += '\x1f';
    raw += "parent1";
    raw += '\x1f';
    raw += "Keep cursor on first edited line";

    auto entry = GitLogMode::parseGraphEntry(raw);
    ASSERT_TRUE(entry.has_value());
    GitLogMode::applyGraphConnector(*entry, "|/  ");

    EXPECT_EQ(entry->graph, "|/");
}

TEST(GitSearchHighlightTest, GitShowHighlightsOnlyMatches)
{
    Editor editor = Editor::createForTests();
    std::string line = "commit abc123";

    std::string out =
        GitShowCommitMode::testRenderLine(editor.theme, line, "abc", false);

    std::string matchSeq = editor.theme.searchMatch();
    std::string normalSeq = editor.theme.reset() + editor.theme.uiDim();

    EXPECT_TRUE(text_utils::is_found(
        out.find(matchSeq + std::string("abc") + normalSeq)));
}

TEST(GitLogCommandTest, FileBrowserGitLogUsesProjectRoot)
{
    auto repo = make_temp_dir("uvim_gitlog_");
    std::string repoStr = repo.string();

    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" init -q"), 0);
    ASSERT_EQ(run_cmd("git -C \"" + repoStr +
                      "\" config user.email \"test@example.com\""),
              0);
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" config user.name \"Test\""),
              0);

    auto file = repo / "README.md";
    write_file(file, "hello\n");

    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" add README.md"), 0);
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" commit -m \"init\" -q"), 0);

    Editor editor = Editor::createForTests();
    editor.setProjectRoot(repoStr);

    auto sm = std::make_unique<ModeStateMachine>(createModeContext(&editor),
                                                 FileBrowserMode{repoStr});
    editor.setModeStateMachineForTests(std::move(sm));
    auto* smPtr = editor.getModeStateMachine();
    ASSERT_NE(smPtr, nullptr);

    smPtr->dispatch(':');
    dispatch_string(*smPtr, "git log");
    smPtr->dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(smPtr->currentStateName(), "GITLOG");
}

TEST(GitShowCommandTest, GjOpensGitShowWhenBlameIsVisible)
{
    auto repo = make_temp_dir("uvim_gitshow_");
    std::string repoStr = repo.string();

    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" init -q"), 0);
    ASSERT_EQ(run_cmd("git -C \"" + repoStr +
                      "\" config user.email \"test@example.com\""),
              0);
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" config user.name \"Test\""),
              0);

    auto file = repo / "notes.txt";
    write_file(file, "line one\n");

    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" add notes.txt"), 0);
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" commit -m \"init\" -q"), 0);

    Editor editor = Editor::createForTests();
    editor.openFile(file.string());
    editor.showGitBlame = true;

    auto sm = std::make_unique<ModeStateMachine>(createModeContext(&editor),
                                                 NormalMode{});
    editor.setModeStateMachineForTests(std::move(sm));
    auto* smPtr = editor.getModeStateMachine();
    ASSERT_NE(smPtr, nullptr);

    Terminal::unreadKey('j');
    smPtr->dispatch('g');

    EXPECT_STREQ(smPtr->currentStateName(), "GITSHOW");
}

TEST(GitLogCommandTest, GblOpensGitLogAtBlamedCommit)
{
    auto repo = make_temp_dir("uvim_gitlog_blame_");
    std::string repoStr = repo.string();

    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" init -q"), 0);
    ASSERT_EQ(run_cmd("git -C \"" + repoStr +
                      "\" config user.email \"test@example.com\""),
              0);
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" config user.name \"Test\""),
              0);

    auto file = repo / "notes.txt";
    write_file(file, "line one\n");

    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" add notes.txt"), 0);
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" commit -m \"first\" -q"), 0);
    std::string firstHash =
        run_cmd_line({"git", "-C", repoStr, "rev-parse", "HEAD"});
    ASSERT_FALSE(firstHash.empty());

    write_file(file, "line one\nline two\n");
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" add notes.txt"), 0);
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" commit -m \"second\" -q"), 0);

    Editor editor = Editor::createForTests();
    editor.openFile(file.string());
    ASSERT_NE(editor.currentBuffer, nullptr);
    ASSERT_NE(editor.cursorY, nullptr);
    *editor.cursorY = 0;
    editor.showGitBlame = true;

    auto sm = std::make_unique<ModeStateMachine>(createModeContext(&editor),
                                                 NormalMode{});
    editor.setModeStateMachineForTests(std::move(sm));
    auto* smPtr = editor.getModeStateMachine();
    ASSERT_NE(smPtr, nullptr);

    Terminal::unreadKey('l');
    Terminal::unreadKey('b');
    smPtr->dispatch('g');

    auto* log = smPtr->getState<GitLogMode>();
    ASSERT_NE(log, nullptr);
    ASSERT_LT(log->cursor, (int)log->filtered.size());
    int selected = log->filtered[log->cursor];
    ASSERT_LT(selected, (int)log->entries.size());
    EXPECT_EQ(log->entries[selected].hash, firstHash);
}

TEST(GitFixupCommandTest, CommandOpensFixupForStagedFiles)
{
    auto repo = make_temp_dir("uvim_gitfixup_cmd_");
    std::string repoStr = repo.string();

    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" init -q"), 0);
    ASSERT_EQ(run_cmd("git -C \"" + repoStr +
                      "\" config user.email \"test@example.com\""),
              0);
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" config user.name \"Test\""),
              0);

    auto file = repo / "notes.txt";
    write_file(file, "line one\n");
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" add notes.txt"), 0);
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" commit -m \"init\" -q"), 0);

    write_file(file, "line one\nline two\n");
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" add notes.txt"), 0);

    Editor editor = Editor::createForTests();
    editor.setProjectRoot(repoStr);

    auto sm = std::make_unique<ModeStateMachine>(createModeContext(&editor),
                                                 NormalMode{});
    editor.setModeStateMachineForTests(std::move(sm));
    auto* smPtr = editor.getModeStateMachine();
    ASSERT_NE(smPtr, nullptr);

    smPtr->dispatch(':');
    dispatch_string(*smPtr, "git fixup");
    smPtr->dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(smPtr->currentStateName(), "GITFIXUP");
}

TEST(GitFixupCommandTest, StageFReturnsToSameStageOnEscape)
{
    auto repo = make_temp_dir("uvim_gitfixup_stage_");
    std::string repoStr = repo.string();

    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" init -q"), 0);
    ASSERT_EQ(run_cmd("git -C \"" + repoStr +
                      "\" config user.email \"test@example.com\""),
              0);
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" config user.name \"Test\""),
              0);

    auto file = repo / "notes.txt";
    write_file(file, "line one\n");
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" add notes.txt"), 0);
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" commit -m \"init\" -q"), 0);

    write_file(file, "line one\nline two\n");
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" add notes.txt"), 0);

    Editor editor = Editor::createForTests();
    auto stage = GitStageMode{{}, repoStr, repoStr};
    stage.fixupMarked.insert("notes.txt");
    stage.cursor = 3;
    stage.offset = 2;

    auto sm =
        std::make_unique<ModeStateMachine>(createModeContext(&editor), stage);
    editor.setModeStateMachineForTests(std::move(sm));
    auto* smPtr = editor.getModeStateMachine();
    ASSERT_NE(smPtr, nullptr);
    auto* initialStage = smPtr->getState<GitStageMode>();
    ASSERT_NE(initialStage, nullptr);
    const int originalCursor = initialStage->cursor;
    const int originalOffset = initialStage->offset;

    smPtr->dispatch('f');
    ASSERT_STREQ(smPtr->currentStateName(), "GITFIXUP");

    auto* fixup = smPtr->getState<GitFixupMode>();
    ASSERT_NE(fixup, nullptr);
    ASSERT_EQ(fixup->fixupFiles.size(), 1u);
    EXPECT_EQ(fixup->fixupFiles.front(), "notes.txt");

    smPtr->dispatch(keyCode(control::ControlKey::ESC));

    EXPECT_STREQ(smPtr->currentStateName(), "GITSTAGE");
    auto* restored = smPtr->getState<GitStageMode>();
    ASSERT_NE(restored, nullptr);
    EXPECT_TRUE(restored->fixupMarked.count("notes.txt"));
    EXPECT_EQ(restored->cursor, originalCursor);
    EXPECT_EQ(restored->offset, originalOffset);
}

TEST(GitFixupCommandTest, SpaceSelectsFixupCommit)
{
    Editor editor = Editor::createForTests();
    GitFixupMode mode{
        {{"abc123", "target commit"}}, "repo", "repo", {}, GitStageMode{}};
    auto sm =
        std::make_unique<ModeStateMachine>(createModeContext(&editor), mode);
    editor.setModeStateMachineForTests(std::move(sm));
    auto* smPtr = editor.getModeStateMachine();
    ASSERT_NE(smPtr, nullptr);

    smPtr->dispatch(keyCode(control::ControlKey::SPACE));

    auto* fixup = smPtr->getState<GitFixupMode>();
    ASSERT_NE(fixup, nullptr);
    EXPECT_TRUE(fixup->confirmActive);
    EXPECT_EQ(fixup->confirmHash, "abc123");
}

TEST(GitBlameDisplayTest, BlameDisplayTruncatesHashAndAuthorToEllipsis)
{
    auto repo = make_temp_dir("uvim_blame_display_");
    auto file = repo / "notes.txt";
    write_file(file, "line one\n");

    Editor editor = Editor::createForTests();
    editor.openFile(file.string());
    ASSERT_NE(editor.currentBuffer, nullptr);

    editor.showGitBlame = true;
    editor.currentBuffer->blameEntries.resize(1);
    auto& entry = editor.currentBuffer->blameEntries[0];
    entry.valid = true;
    entry.hash = "84397dc123456789";
    entry.author = "Matti Laamanen Exampleperson";
    entry.date = "2026-03-25";

    const std::string blame = editor.blameDisplayForLine(0);

    EXPECT_EQ(blame, "84397dc Matti Laamanen Exam...");
    EXPECT_EQ(text_utils::utf8DisplayWidth(blame), Editor::kGitBlameMaxWidth);
}

TEST(GitBlameDisplayTest, ExtendedBlameDisplayIncludesDateTime)
{
    auto repo = make_temp_dir("uvim_blame_display_extended_");
    auto file = repo / "notes.txt";
    write_file(file, "line one\n");

    Editor editor = Editor::createForTests();
    editor.openFile(file.string());
    ASSERT_NE(editor.currentBuffer, nullptr);

    editor.showGitBlame = true;
    editor.showGitBlameDateTime = true;
    editor.currentBuffer->blameEntries.resize(1);
    auto& entry = editor.currentBuffer->blameEntries[0];
    entry.valid = true;
    entry.hash = "84397dc123456789";
    entry.author = "Matti";
    entry.date = "2026-03-25 14:05";

    const std::string blame = editor.blameDisplayForLine(0);

    EXPECT_EQ(blame, "84397dc Matti 2026-03-25 14:05");
    EXPECT_LT(text_utils::utf8DisplayWidth(blame),
              Editor::kGitBlameDateTimeMaxWidth);
}

TEST(ShowChangesTest, DiffsUnsavedBufferAgainstSavedFile)
{
    if(!hasGit())
        GTEST_SKIP() << "git not installed";
    using uvim_test::make_temp_dir;
    using uvim_test::ScopedCurrentPath;
    using uvim_test::write_file;

    auto root = make_temp_dir("uvim_showchanges_");
    write_file(root / "notes.txt", "line one\nline two\nline three\n");
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.openFile("notes.txt", false);
    ASSERT_NE(editor.currentBuffer, nullptr);

    std::string title;
    std::string message;
    // Nothing edited yet.
    EXPECT_TRUE(editor.unsavedChangesDiff(title, message).empty());
    EXPECT_EQ(message, "No unsaved changes");

    (*editor.lines)[1] = "line two edited";
    editor.lines->erase(editor.lines->begin() + 2);
    *editor.dirty = true;
    editor.gitUseDefaultColors = false;
    const auto diff = editor.unsavedChangesDiff(title, message);
    ASSERT_FALSE(diff.empty()) << message;
    EXPECT_EQ(title, "CHANGES notes.txt");
    auto has = [&](const std::string& text)
    {
        return std::any_of(diff.begin(), diff.end(),
                           [&](const std::string& l)
                           { return l.find(text) != std::string::npos; });
    };
    EXPECT_TRUE(has("--- notes.txt (saved)"));
    EXPECT_TRUE(has("+++ notes.txt (unsaved)"));
    EXPECT_TRUE(has("-line two"));
    EXPECT_TRUE(has("+line two edited"));
    EXPECT_TRUE(has("-line three"));
    EXPECT_FALSE(has("uvim-unsaved-")) << "temp paths must not leak";
}

TEST(ShowChangesTest, DropAndUndoHunksInChangesView)
{
    if(!hasGit())
        GTEST_SKIP() << "git not installed";
    using uvim_test::makeMachine;
    using uvim_test::make_temp_dir;
    using uvim_test::ScopedCurrentPath;
    using uvim_test::write_file;

    auto root = make_temp_dir("uvim_showchanges_drop_");
    std::string text;
    for(int i = 1; i <= 30; ++i)
        text += "line " + std::to_string(i) + "\n";
    write_file(root / "h.txt", text);
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.openFile("h.txt", false);
    editor.gitUseDefaultColors = false;
    editor.saveState();
    (*editor.lines)[1] = "line 2 CHANGED";
    (*editor.lines)[14] = "line 15 CHANGED";
    (*editor.lines)[27] = "line 28 CHANGED";
    *editor.dirty = true;
    editor.saveState();

    std::string title;
    std::string message;
    GitShowCommitMode view{"", editor.unsavedChangesDiff(title, message)};
    ASSERT_FALSE(view.lines.empty()) << message;
    view.unsavedChanges = true;
    auto sm = makeMachine(editor, std::move(view));

    sm.dispatch('n'); // hunk 1
    sm.dispatch('n'); // hunk 2 (line 15)
    sm.dispatch('d');
    EXPECT_EQ((*editor.lines)[14], "line 15") << "hunk 2 reverted";
    EXPECT_EQ((*editor.lines)[1], "line 2 CHANGED");
    EXPECT_EQ((*editor.lines)[27], "line 28 CHANGED");
    auto* state = sm.getState<GitShowCommitMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->droppedHunks, 1);

    sm.dispatch('u');
    EXPECT_EQ((*editor.lines)[14], "line 15 CHANGED") << "u restores it";
    state = sm.getState<GitShowCommitMode>();
    EXPECT_EQ(state->droppedHunks, 0);

    // Drop everything: the buffer matches the saved file again.
    sm.dispatch('[');
    sm.dispatch('d');
    sm.dispatch('d');
    sm.dispatch('d');
    EXPECT_EQ((*editor.lines)[1], "line 2");
    EXPECT_EQ((*editor.lines)[14], "line 15");
    EXPECT_EQ((*editor.lines)[27], "line 28");
    EXPECT_FALSE(*editor.dirty);
    state = sm.getState<GitShowCommitMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->lines.front(), "No unsaved changes left.");
}

TEST(ShowChangesTest, HunkNavigationWrapsAround)
{
    using uvim_test::makeMachine;
    Editor editor = Editor::createForTests();
    std::vector<std::string> diff = {
        "diff --git a b",  "--- a",          "+++ b",
        "@@ -1,1 +1,1 @@", "-one",           "+ONE",
        "@@ -5,1 +5,1 @@", "-five",          "+FIVE",
        "@@ -9,1 +9,1 @@", "-nine",          "+NINE"};
    auto sm = makeMachine(editor, GitShowCommitMode{"DIFF", diff});
    auto current = [&]
    { return sm.getState<GitShowCommitMode>()->currentHunk; };

    sm.dispatch('n');
    sm.dispatch('n');
    sm.dispatch('n');
    EXPECT_EQ(current(), 2);

    sm.dispatch('n'); // past the last hunk
    EXPECT_EQ(current(), 0);
    EXPECT_NE(editor.statusMessage.find("wrapped to first"), std::string::npos)
        << editor.statusMessage;

    sm.dispatch('N'); // before the first hunk
    EXPECT_EQ(current(), 2);
    EXPECT_NE(editor.statusMessage.find("wrapped to last"), std::string::npos)
        << editor.statusMessage;

    sm.dispatch('['); // ]/[ wrap the same way
    EXPECT_EQ(current(), 1);
    EXPECT_EQ(editor.statusMessage, "Hunk 2/3");
}

namespace
{
// Runs a shell command in `dir`; returns its stdout. Uses `sh` from PATH
// (Git for Windows provides one), which wants forward slashes.
std::string sh(const std::filesystem::path& dir, const std::string& cmd)
{
    ProcessPipe pipe(
        {"sh", "-c", "cd '" + dir.generic_string() + "' && " + cmd});
    return pipe ? pipe.readAll() : std::string{};
}

// Repo with c1 (30 lines), c2 (edits line 2 and 25), c3 (another file).
std::filesystem::path makeRewriteRepo(const std::string& prefix,
                                      bool withTopCommit)
{
    auto root = uvim_test::make_temp_dir(prefix);
    std::string text;
    for(int i = 1; i <= 30; ++i)
        text += "a" + std::to_string(i) + "\n";
    uvim_test::write_file(root / "a.txt", text);
    uvim_test::write_file(root / "b.txt", "b\n");
    sh(root, "git init -q -b main && git config user.email t@t && "
             "git config user.name t && git add . && git commit -qm c1");
    sh(root, "sed -e 's/^a2$/a2 X/' -e 's/^a25$/a25 Y/' a.txt > t && "
             "mv t a.txt && git commit -qam c2");
    if(withTopCommit)
        sh(root, "echo b2 >> b.txt && git commit -qam c3");
    return root;
}

std::string trimmed(std::string s)
{
    while(!s.empty() && (s.back() == '\n' || s.back() == ' '))
        s.pop_back();
    return s;
}

// The patch of c2's hunk that touches line 25.
std::string secondHunkPatch(const std::filesystem::path& root,
                            const std::string& hash)
{
    const auto lines = git_rewrite::plainCommitDiff(root.string(), hash);
    std::string patch;
    bool inSecond = false;
    int hunks = 0;
    for(const auto& line : lines)
    {
        if(line.rfind("@@", 0) == 0)
            inSecond = ++hunks == 2;
        if(line.rfind("diff --git", 0) == 0 || line.rfind("index ", 0) == 0 ||
           line.rfind("--- ", 0) == 0 || line.rfind("+++ ", 0) == 0 ||
           inSecond)
            patch += line + "\n";
    }
    return patch;
}
} // namespace

TEST(CommitRewriteTest, DropsHunkFromOlderCommitAndUndoes)
{
    if(!hasGit())
        GTEST_SKIP() << "git not installed";
    auto root = makeRewriteRepo("uvim_rewrite_older_", true);
    const std::string c2 = trimmed(sh(root, "git rev-parse HEAD~1"));
    const std::string head = trimmed(sh(root, "git rev-parse HEAD"));

    auto result = git_rewrite::dropFromCommit(root.string(), c2,
                                              secondHunkPatch(root, c2));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_NE(result.newHash, c2);
    EXPECT_EQ(result.originalHead, head);
    // c2 now only changes line 2; c3 was replayed on top.
    const std::string newC2 = sh(root, "git show --format= " + result.newHash);
    EXPECT_NE(newC2.find("+a2 X"), std::string::npos);
    EXPECT_EQ(newC2.find("a25 Y"), std::string::npos);
    EXPECT_EQ(trimmed(sh(root, "git log --format=%s -1")), "c3");
    // The dropped change stays in the working tree.
    EXPECT_NE(sh(root, "git diff").find("+a25 Y"), std::string::npos);

    std::string message;
    ASSERT_TRUE(git_rewrite::undoDrop(root.string(), result.originalHead,
                                      result.newHead, message))
        << message;
    EXPECT_EQ(trimmed(sh(root, "git rev-parse HEAD")), head);
    EXPECT_TRUE(trimmed(sh(root, "git status --porcelain")).empty());
}

TEST(CommitRewriteTest, AmendsHeadAndRefusesUnsafeRewrites)
{
    if(!hasGit())
        GTEST_SKIP() << "git not installed";
    auto root = makeRewriteRepo("uvim_rewrite_head_", false);
    const std::string c2 = trimmed(sh(root, "git rev-parse HEAD"));

    auto result = git_rewrite::dropFromCommit(root.string(), c2,
                                              secondHunkPatch(root, c2));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(result.newHash, result.newHead);
    EXPECT_EQ(trimmed(sh(root, "git rev-list --count HEAD")), "2");

    // Dirty tree (the dropped change) blocks another rewrite.
    EXPECT_NE(git_rewrite::checkRewritable(root.string(), "HEAD")
                  .find("uncommitted"),
              std::string::npos);
    // Root commit cannot be rewritten.
    sh(root, "git stash -q");
    EXPECT_NE(git_rewrite::checkRewritable(root.string(), "HEAD~1")
                  .find("root"),
              std::string::npos);
    // Pushed commits are refused.
    sh(root, "git init -q --bare ../remote_" + root.filename().string() +
                 ".git && git remote add origin ../remote_" +
                 root.filename().string() +
                 ".git && git push -q -u origin main 2>/dev/null");
    EXPECT_NE(git_rewrite::checkRewritable(root.string(), "HEAD")
                  .find("pushed"),
              std::string::npos);
}

TEST(CommitRewriteTest, ConflictingRewriteLeavesRepoUnchanged)
{
    if(!hasGit())
        GTEST_SKIP() << "git not installed";
    auto root = makeRewriteRepo("uvim_rewrite_conflict_", false);
    // c3 edits the line c2 introduced, so c2 cannot lose it.
    sh(root, "sed -e 's/^a25 Y$/a25 Z/' a.txt > t && mv t a.txt && "
             "git commit -qam c3");
    const std::string c2 = trimmed(sh(root, "git rev-parse HEAD~1"));
    const std::string head = trimmed(sh(root, "git rev-parse HEAD"));

    auto result = git_rewrite::dropFromCommit(root.string(), c2,
                                              secondHunkPatch(root, c2));
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.message.find("conflict"), std::string::npos)
        << result.message;
    EXPECT_EQ(trimmed(sh(root, "git rev-parse HEAD")), head);
    EXPECT_TRUE(trimmed(sh(root, "git status --porcelain")).empty());
    EXPECT_TRUE(trimmed(sh(root, "git stash list")).empty());
}

TEST(CommitRewriteTest, CommitViewDropsReviewsAndRewrites)
{
    if(!hasGit())
        GTEST_SKIP() << "git not installed";
    auto root = makeRewriteRepo("uvim_rewrite_view_", true);
    uvim_test::ScopedCurrentPath cwd(root);
    const std::string c2 = trimmed(sh(root, "git rev-parse HEAD~1"));

    Editor editor = Editor::createForTests();
    editor.gitUseDefaultColors = false;
    GitShowCommitMode view{c2, editor.loadGitShowLines(c2)};
    ASSERT_FALSE(view.lines.empty());
    view.repoRoot = root.string();
    auto sm = uvim_test::makeMachine(editor, std::move(view));
    auto state = [&] { return sm.getState<GitShowCommitMode>(); };
    auto shows = [&](const std::string& text)
    {
        const auto& lines = state()->lines;
        return std::any_of(lines.begin(), lines.end(),
                           [&](const std::string& l)
                           { return l.find(text) != std::string::npos; });
    };

    sm.dispatch('n');
    sm.dispatch('n'); // the line-25 hunk
    sm.dispatch('d');
    EXPECT_TRUE(shows("+a2 X")) << "new commit diff keeps hunk 1";
    EXPECT_FALSE(shows("+a25 Y"));

    sm.dispatch('v'); // review what is dropped
    EXPECT_TRUE(state()->showDropped);
    EXPECT_TRUE(shows("+a25 Y"));
    EXPECT_FALSE(shows("+a2 X"));
    sm.dispatch('v');

    sm.dispatch('c');
    EXPECT_TRUE(state()->confirmRewrite);
    sm.dispatch('y');
    ASSERT_TRUE(state()->historyChanged) << editor.statusMessage;
    EXPECT_NE(state()->commitHash, c2);
    EXPECT_EQ(sh(root, "git show --format= HEAD~1").find("a25 Y"),
              std::string::npos);

    sm.dispatch('u'); // undo the rewrite
    EXPECT_EQ(state()->commitHash, c2) << editor.statusMessage;
    EXPECT_TRUE(trimmed(sh(root, "git status --porcelain")).empty());
}

