#include "real_mode_test_utils.h"
#include "substitute_command.h"

using namespace uvim_test;

namespace
{
void contents(Editor& editor, std::vector<std::string> lines)
{
    editor.createNewBuffer();
    *editor.lines = std::move(lines);
    editor.saveState();
    editor.currentBuffer->savedUndoIndex = editor.currentBuffer->undoIndex;
    *editor.dirty = false;
}
} // namespace

TEST(SubstituteTest, CurrentLineFirstMatchAndWholeBufferGlobal)
{
    auto editor = Editor::createForTests();
    contents(editor, {"foo foo", "foo foo", "FOO"});
    *editor.cursorY = 1;
    auto sm = makeMachine(editor, NormalMode{});
    dispatch_command(sm, "s/foo/bar/");
    EXPECT_EQ(*editor.lines,
              (std::vector<std::string>{"foo foo", "bar foo", "FOO"}));
    dispatch_command(sm, "%s/foo/baz/g");
    EXPECT_EQ(*editor.lines,
              (std::vector<std::string>{"baz baz", "bar baz", "FOO"}));
    EXPECT_EQ(editor.statusMessage, "3 substitutions on 2 lines");
    EXPECT_TRUE(*editor.dirty);
}

TEST(SubstituteTest, NumericRelativeAndVisualRanges)
{
    auto editor = Editor::createForTests();
    contents(editor, {"x", "x", "x", "x"});
    auto sm = makeMachine(editor, NormalMode{});
    dispatch_command(sm, "2,3s/x/y/");
    EXPECT_EQ(*editor.lines, (std::vector<std::string>{"x", "y", "y", "x"}));
    *editor.cursorY = 1;
    dispatch_command(sm, ".+1,$s/./z/");
    EXPECT_EQ(*editor.lines, (std::vector<std::string>{"x", "y", "z", "z"}));
    editor.currentBuffer->visualStartY = 1;
    editor.currentBuffer->visualEndY = 0;
    dispatch_command(sm, "'<,'>s/./v/");
    EXPECT_EQ(*editor.lines, (std::vector<std::string>{"v", "v", "z", "z"}));
}

TEST(SubstituteTest, RegexCapturesWholeMatchEscapesAndAlternateDelimiter)
{
    auto editor = Editor::createForTests();
    contents(editor, {"foo12 /old/path &"});
    editor.executeCommand(R"(s#(foo)([0-9]+)#\2-\1-&-\&#)");
    EXPECT_EQ(editor.lines->front(), "12-foo-foo12-& /old/path &");
    editor.executeCommand(R"(substitute/\/old\/path/\/new\/path/)");
    EXPECT_EQ(editor.lines->front(), "12-foo-foo12-& /new/path &");
    editor.executeCommand(R"(s.\/new\/path.done.)");
    EXPECT_EQ(editor.lines->front(), "12-foo-foo12-& done &");
}

TEST(SubstituteTest, CaseFlagsCountOnlyAndQuietNoMatch)
{
    auto editor = Editor::createForTests();
    contents(editor, {"Foo foo FOO"});
    const int undo = editor.currentBuffer->undoIndex;
    editor.executeCommand("%s/foo/bar/gin");
    EXPECT_EQ(editor.lines->front(), "Foo foo FOO");
    EXPECT_EQ(editor.statusMessage, "3 matches on 1 lines");
    EXPECT_EQ(editor.currentBuffer->undoIndex, undo);
    EXPECT_FALSE(*editor.dirty);
    editor.executeCommand("s/foo/bar/giI");
    EXPECT_EQ(editor.lines->front(), "Foo bar FOO");
    editor.executeCommand("s/missing/no/e");
    EXPECT_EQ(editor.statusMessage, "0 substitutions on 0 lines");
}

TEST(SubstituteTest, InvalidInputIsAtomicAndDoesNotCreateUndoEntries)
{
    auto editor = Editor::createForTests();
    contents(editor, {"foo foo", "foo"});
    const auto original = *editor.lines;
    const int undo = editor.currentBuffer->undoIndex;
    for(const std::string command :
        {"s/foo/bar", "s/[/bar/", "s/foo/bar/z", "3s/foo/bar/", "0s/foo/bar/",
         "2,1s/foo/bar/", "999999999999999999s/foo/bar/", "s/missing/bar/",
         "s//bar/"})
    {
        SCOPED_TRACE(command);
        editor.executeCommand(command);
        EXPECT_EQ(*editor.lines, original);
        EXPECT_EQ(editor.currentBuffer->undoIndex, undo);
        EXPECT_FALSE(*editor.dirty);
        EXPECT_TRUE(editor.statusMessage.starts_with("substitute:"));
    }
}

TEST(SubstituteTest, EmptyPatternReusesSearchAndZeroWidthMatchesTerminate)
{
    auto editor = Editor::createForTests();
    contents(editor, {"foo", "bar"});
    editor.currentBuffer->lastSearchQuery = "foo";
    editor.executeCommand("s//qux/");
    EXPECT_EQ(editor.lines->front(), "qux");
    editor.executeCommand("%s/^|$/!/g");
    EXPECT_EQ(*editor.lines, (std::vector<std::string>{"!qux!", "!bar!"}));
}

TEST(SubstituteTest, NewlineReplacementIsOneUndoStepAndRestoresCursor)
{
    auto editor = Editor::createForTests();
    contents(editor, {"foo foo"});
    *editor.cursorX = 5;
    const int before = editor.currentBuffer->undoIndex;
    editor.executeCommand(R"(%s/foo/a\nb/g)");
    EXPECT_EQ(*editor.lines, (std::vector<std::string>{"a", "b a", "b"}));
    EXPECT_EQ(editor.currentBuffer->undoIndex, before + 1);
    editor.undo();
    EXPECT_EQ(*editor.lines, (std::vector<std::string>{"foo foo"}));
    EXPECT_EQ(*editor.cursorX, 5);
    EXPECT_FALSE(*editor.dirty);
    editor.redo();
    EXPECT_EQ(*editor.lines, (std::vector<std::string>{"a", "b a", "b"}));
}

TEST(SubstituteTest, PromptDoesNotCompleteSubstitutionPatternsOrReplacement)
{
    auto editor = Editor::createForTests();
    contents(editor, {"foo"});
    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(':');
    for(char ch : std::string("s#foo#hello world#"))
        sm.dispatch(ch);
    EXPECT_FALSE(editor.commandPopupActive);
    sm.dispatch(keyCode(control::ControlKey::TAB));
    EXPECT_EQ(editor.commandBuffer, ":s#foo#hello world#");
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_EQ(editor.lines->front(), "hello world");
}

TEST(SubstituteTest, WelcomePromptReportsNoBufferWithoutOpeningOne)
{
    auto editor = Editor::createForTests();
    auto sm = makeMachine(editor, WelcomeMode{});
    dispatch_command(sm, "s/foo/bar/");
    EXPECT_FALSE(editor.hasBuffer());
    EXPECT_EQ(editor.statusMessage, "substitute: No buffer");
}

TEST(SubstituteTest,
     ProjectStagesOnlyMatchingFilesAndHonorsIgnoreAndBinaryRules)
{
    const auto root = make_temp_dir("uvim_project_substitute_");
    write_file(root / ".gitignore", "ignored/\n");
    write_file(root / "first.txt", "foo foo\n");
    write_file(root / "nested" / "second.cpp", "foo\n");
    write_file(root / "unchanged.txt", "other\n");
    write_file(root / "ignored" / "skip.txt", "foo\n");
    write_file(root / ".hidden.txt", "foo\n");
    write_file(root / "binary.cpp", std::string("foo\0foo\0", 8));
    auto editor = Editor::createForTests();
    editor.projectRoot = root.string();
    editor.respectGitignore = true;
    editor.executeCommand("%%s/foo/bar/g");
    ASSERT_EQ(editor.buffers.size(), 2u);
    EXPECT_NE(editor.statusMessage.find("3 substitutions in 2 project files"),
              std::string::npos);
    for(const auto& buffer : editor.buffers)
        EXPECT_TRUE(buffer->dirty);
    EXPECT_EQ(read_file(root / "first.txt"), "foo foo\n");
    EXPECT_EQ(read_file(root / "nested" / "second.cpp"), "foo\n");
    editor.executeCommand("wa");
    EXPECT_EQ(read_file(root / "first.txt"), "bar bar\n");
    EXPECT_EQ(read_file(root / "nested" / "second.cpp"), "bar\n");
    EXPECT_EQ(read_file(root / "ignored" / "skip.txt"), "foo\n");
    EXPECT_EQ(read_file(root / ".hidden.txt"), "foo\n");
}

TEST(SubstituteTest,
     ProjectUsesUnsavedBuffersKeepsSelectionAndAllowsPerFileUndo)
{
    const auto root = make_temp_dir("uvim_project_substitute_dirty_");
    const auto first = root / "first.txt";
    write_file(first, "disk\n");
    write_file(root / "second.txt", "foo\n");
    auto editor = Editor::createForTests();
    editor.projectRoot = root.string();
    editor.openFile(first.string(), false);
    editor.lines->front() = "foo unsaved";
    editor.saveState();
    const int original = editor.currentBufferIndex;
    editor.executeCommand("%%s/foo/bar/g");
    EXPECT_EQ(editor.currentBufferIndex, original);
    EXPECT_EQ(editor.lines->front(), "bar unsaved") << editor.statusMessage;
    EXPECT_EQ(read_file(first), "disk\n");
    editor.undo();
    EXPECT_EQ(editor.lines->front(), "foo unsaved");
    EXPECT_TRUE(*editor.dirty);
}

TEST(SubstituteTest, ProjectCountOnlyDoesNotOpenOrModifyBuffers)
{
    const auto root = make_temp_dir("uvim_project_substitute_count_");
    write_file(root / "file.txt", "foo FOO\n");
    auto editor = Editor::createForTests();
    editor.projectRoot = root.string();
    auto sm = makeMachine(editor, WelcomeMode{});
    dispatch_command(sm, "%%s/foo/bar/gin");
    EXPECT_TRUE(editor.buffers.empty());
    EXPECT_NE(editor.statusMessage.find("2 matches in 1 project files"),
              std::string::npos);
    EXPECT_EQ(read_file(root / "file.txt"), "foo FOO\n");
}

TEST(SubstituteTest, ProjectNeverFollowsSymlinksOutsideRootEvenForOpenBuffers)
{
    const auto root = make_temp_dir("uvim_project_substitute_symlink_");
    const auto outside = make_temp_dir("uvim_project_substitute_outside_");
    write_file(root / "inside.txt", "foo\n");
    write_file(outside / "outside.txt", "foo\n");
    std::error_code ec;
    std::filesystem::create_directory_symlink(outside, root / "link", ec);
    if(ec)
        GTEST_SKIP() << "Directory symlinks unavailable: " << ec.message();
    auto editor = Editor::createForTests();
    editor.projectRoot = root.string();
    editor.openFile((root / "link" / "outside.txt").string(), false);
    const int original = editor.currentBufferIndex;
    editor.executeCommand("%%s/foo/bar/g");
    EXPECT_EQ(editor.currentBufferIndex, original);
    EXPECT_EQ(editor.lines->front(), "foo");
    EXPECT_FALSE(*editor.dirty);
    EXPECT_EQ(read_file(outside / "outside.txt"), "foo\n");
}

TEST(SubstituteTest, RecognitionDoesNotInterceptExistingCommands)
{
    for(const std::string text : {"s/foo/bar/g", "%s#foo#bar#", "%%s/a/b/",
                                  "2,5s/", "'<,'>s/", "substitute/a/b/"})
        EXPECT_TRUE(command::execution::isSubstituteCommand(text)) << text;
    for(const std::string text :
        {"set", "split", "sc", "se", "shtio", "git stash", ""})
        EXPECT_FALSE(command::execution::isSubstituteCommand(text)) << text;
}

TEST(SubstituteTest, ProjectIncludesNewNamedBuffersAndCanDisableGitignore)
{
    const auto root = make_temp_dir("uvim_project_substitute_new_");
    write_file(root / ".gitignore", "ignored.txt\n");
    write_file(root / "ignored.txt", "foo\n");
    auto editor = Editor::createForTests();
    editor.projectRoot = root.string();
    editor.respectGitignore = false;
    contents(editor, {"foo new"});
    set_buffer_filename(editor, (root / "new.txt").string());
    *editor.dirty = true;
    editor.executeCommand("%%s/foo/bar/g");
    EXPECT_EQ(editor.lines->front(), "bar new");
    ASSERT_EQ(editor.buffers.size(), 2u);
    EXPECT_FALSE(std::filesystem::exists(root / "new.txt"));
    EXPECT_EQ(read_file(root / "ignored.txt"), "foo\n");
    editor.undo();
    EXPECT_EQ(editor.lines->front(), "foo new");
}

namespace
{
void confirmKeys(std::string_view keys)
{
    for(auto key = keys.rbegin(); key != keys.rend(); ++key)
        Terminal::unreadKey(static_cast<unsigned char>(*key));
}
} // namespace

TEST(SubstituteTest, ConfirmationApprovesOrSkipsEachMatchAndUndoesAsOneChange)
{
    auto editor = Editor::createForTests();
    contents(editor, {"foo foo foo"});
    const int before = editor.currentBuffer->undoIndex;
    confirmKeys("yny");
    editor.executeCommand("%s/foo/bar/gc");
    EXPECT_EQ(editor.lines->front(), "bar foo bar");
    EXPECT_EQ(editor.statusMessage, "2 substitutions on 1 lines");
    EXPECT_EQ(editor.currentBuffer->undoIndex, before + 1);
    editor.undo();
    EXPECT_EQ(editor.lines->front(), "foo foo foo");
    EXPECT_FALSE(*editor.dirty);
    EXPECT_EQ(Terminal::bufferedKeyCount(), 0);
}

TEST(SubstituteTest, DecliningEveryConfirmationLeavesBufferAndUndoUnchanged)
{
    auto editor = Editor::createForTests();
    contents(editor, {"foo foo", "foo"});
    const int before = editor.currentBuffer->undoIndex;
    confirmKeys("nnn");
    editor.executeCommand("%s/foo/bar/gc");
    EXPECT_EQ(*editor.lines, (std::vector<std::string>{"foo foo", "foo"}));
    EXPECT_EQ(editor.currentBuffer->undoIndex, before);
    EXPECT_FALSE(*editor.dirty);
    EXPECT_EQ(editor.statusMessage, "0 substitutions on 0 lines");
    EXPECT_EQ(Terminal::bufferedKeyCount(), 0);
}

TEST(SubstituteTest, ConfirmAllAppliesRemainingMatchesWithoutMoreQuestions)
{
    auto editor = Editor::createForTests();
    contents(editor, {"foo foo", "foo"});
    confirmKeys("na");
    editor.executeCommand("%s/foo/bar/gc");
    EXPECT_EQ(*editor.lines, (std::vector<std::string>{"foo bar", "bar"}));
    EXPECT_EQ(Terminal::bufferedKeyCount(), 0);
}

TEST(SubstituteTest, ConfirmationQuitEscapeAndInterruptKeepOnlyApprovedChanges)
{
    for(const char stop : {'q', '\x1b', '\x03'})
    {
        auto editor = Editor::createForTests();
        contents(editor, {"foo foo", "foo"});
        confirmKeys(std::string("y") + stop);
        editor.executeCommand("%s/foo/bar/gc");
        EXPECT_EQ(*editor.lines, (std::vector<std::string>{"bar foo", "foo"}));
        EXPECT_EQ(editor.statusMessage, "1 substitutions on 1 lines; stopped");
        EXPECT_EQ(Terminal::bufferedKeyCount(), 0);
    }
}

TEST(SubstituteTest, ConfirmLastApprovesCurrentMatchAndStops)
{
    auto editor = Editor::createForTests();
    contents(editor, {"foo foo", "foo"});
    confirmKeys("l");
    editor.executeCommand("%s/foo/bar/gc");
    EXPECT_EQ(*editor.lines, (std::vector<std::string>{"bar foo", "foo"}));
    EXPECT_EQ(editor.statusMessage, "1 substitutions on 1 lines; stopped");
    EXPECT_EQ(Terminal::bufferedKeyCount(), 0);
}

TEST(SubstituteTest, ConfirmationWithoutGlobalOnlyConsidersFirstMatchOnEachLine)
{
    auto editor = Editor::createForTests();
    contents(editor, {"foo foo", "foo foo"});
    confirmKeys("ny");
    editor.executeCommand("%s/foo/bar/c");
    EXPECT_EQ(*editor.lines, (std::vector<std::string>{"foo foo", "bar foo"}));
    EXPECT_EQ(Terminal::bufferedKeyCount(), 0);
}

TEST(SubstituteTest, CountOnlyWithConfirmNeverPromptsOrConsumesKeys)
{
    auto editor = Editor::createForTests();
    contents(editor, {"foo foo"});
    confirmKeys("x");
    editor.executeCommand("%s/foo/bar/gcn");
    EXPECT_EQ(editor.lines->front(), "foo foo");
    EXPECT_EQ(editor.statusMessage, "2 matches on 1 lines");
    EXPECT_EQ(Terminal::readKey(), 'x');
}

TEST(SubstituteTest, ProjectConfirmationAllSpansFilesAndLeavesDiskUntouched)
{
    const auto root = make_temp_dir("uvim_project_confirm_all_");
    write_file(root / "a.txt", "foo foo\n");
    write_file(root / "b.txt", "foo\n");
    auto editor = Editor::createForTests();
    editor.projectRoot = root.string();
    confirmKeys("na");
    editor.executeCommand("%%s/foo/bar/gc");
    ASSERT_EQ(editor.buffers.size(), 2u);
    EXPECT_EQ(editor.buffers[0]->lines.front(), "foo bar");
    EXPECT_EQ(editor.buffers[1]->lines.front(), "bar");
    EXPECT_EQ(read_file(root / "a.txt"), "foo foo\n");
    EXPECT_EQ(read_file(root / "b.txt"), "foo\n");
    EXPECT_EQ(Terminal::bufferedKeyCount(), 0);
}

TEST(SubstituteTest, ProjectConfirmationQuitDoesNotOpenSkippedFiles)
{
    const auto root = make_temp_dir("uvim_project_confirm_quit_");
    write_file(root / "a.txt", "foo\n");
    write_file(root / "b.txt", "foo\n");
    auto editor = Editor::createForTests();
    editor.projectRoot = root.string();
    confirmKeys("yq");
    editor.executeCommand("%%s/foo/bar/gc");
    ASSERT_EQ(editor.buffers.size(), 1u);
    EXPECT_EQ(editor.lines->front(), "bar");
    EXPECT_NE(editor.statusMessage.find("stopped"), std::string::npos);
    EXPECT_EQ(read_file(root / "a.txt"), "foo\n");
    EXPECT_EQ(read_file(root / "b.txt"), "foo\n");
    EXPECT_EQ(Terminal::bufferedKeyCount(), 0);
}
