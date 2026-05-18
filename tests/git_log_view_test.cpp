#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
std::filesystem::path make_temp_dir(const std::string& prefix)
{
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path base =
        std::filesystem::temp_directory_path() /
        (prefix + std::to_string(now));
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
    std::string normalHash = editor.theme.reset() + editor.theme.uiAccent();
    std::string normalText = editor.theme.reset() + editor.theme.baseFg();

    EXPECT_NE(out.find(matchSeq + std::string("abc") + normalHash),
              std::string::npos);
    EXPECT_NE(out.find(matchSeq + std::string("abc") + normalText),
              std::string::npos);
}

TEST(GitSearchHighlightTest, GitShowHighlightsOnlyMatches)
{
    Editor editor = Editor::createForTests();
    std::string line = "commit abc123";

    std::string out = GitShowCommitMode::testRenderLine(
        editor.theme, line, "abc", false);

    std::string matchSeq = editor.theme.searchMatch();
    std::string normalSeq = editor.theme.reset() + editor.theme.uiDim();

    EXPECT_NE(out.find(matchSeq + std::string("abc") + normalSeq),
              std::string::npos);
}

TEST(GitLogCommandTest, FileBrowserGitLogUsesProjectRoot)
{
    auto repo = make_temp_dir("uvim_gitlog_");
    std::string repoStr = repo.string();

    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" init -q"), 0);
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" config user.email \"test@example.com\""),
              0);
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" config user.name \"Test\""),
              0);

    auto file = repo / "README.md";
    write_file(file, "hello\n");

    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" add README.md"), 0);
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" commit -m \"init\" -q"),
              0);

    Editor editor = Editor::createForTests();
    editor.setProjectRoot(repoStr);

    auto sm = std::make_unique<ModeStateMachine>(
        createModeContext(&editor), FileBrowserMode{repoStr});
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
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" commit -m \"init\" -q"),
              0);

    Editor editor = Editor::createForTests();
    editor.openFile(file.string());
    editor.showGitBlame = true;

    auto sm = std::make_unique<ModeStateMachine>(
        createModeContext(&editor), NormalMode{});
    editor.setModeStateMachineForTests(std::move(sm));
    auto* smPtr = editor.getModeStateMachine();
    ASSERT_NE(smPtr, nullptr);

    Terminal::unreadKey('j');
    smPtr->dispatch('g');

    EXPECT_STREQ(smPtr->currentStateName(), "GITSHOW");
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
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" commit -m \"init\" -q"),
              0);

    write_file(file, "line one\nline two\n");
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" add notes.txt"), 0);

    Editor editor = Editor::createForTests();
    editor.setProjectRoot(repoStr);

    auto sm = std::make_unique<ModeStateMachine>(
        createModeContext(&editor), NormalMode{});
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
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" commit -m \"init\" -q"),
              0);

    write_file(file, "line one\nline two\n");
    ASSERT_EQ(run_cmd("git -C \"" + repoStr + "\" add notes.txt"), 0);

    Editor editor = Editor::createForTests();
    auto stage = GitStageMode{{}, repoStr, repoStr};
    stage.fixupMarked.insert("notes.txt");
    stage.cursor = 3;
    stage.offset = 2;

    auto sm = std::make_unique<ModeStateMachine>(
        createModeContext(&editor), stage);
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
    GitFixupMode mode{{{"abc123", "target commit"}}, "repo", "repo", {},
                      GitStageMode{}};
    auto sm = std::make_unique<ModeStateMachine>(
        createModeContext(&editor), mode);
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
