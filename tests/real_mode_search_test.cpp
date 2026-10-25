#include "editor_mode_controller.h"
#include "editor_utils.h"
#include "picker_query.h"
#include "real_mode_test_utils.h"

#include <thread>

using namespace uvim_test;

namespace
{
void flushGrepDebounce(GrepSearchMode& state, Editor& editor)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(310));
    state.processIdle(editor);
    // Searches and rg cache indexing run on the task queue; apply finished
    // jobs the way the editor main loop does.
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while(!editor.tasks.idle() && std::chrono::steady_clock::now() < deadline)
    {
        editor.tasks.pollCompletions();
        state.processIdle(editor);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    editor.tasks.pollCompletions();
    state.processIdle(editor);
    state.draw(editor);
}
} // namespace

TEST(RealModeTransitionsTest, SearchPromptShowsFullTypedPattern)
{
    std::string output;
    widgets::MessageBarView view{
        .currentMode = SEARCH_FORWARD,
        .screenCols = 80,
        .commandBuffer = "/long_pattern",
    };

    widgets::appendMessageBar(output, view);

    EXPECT_EQ(output, "/long_pattern");
}

TEST(RealModeTransitionsTest, SearchMatchContrastDefaultsHighAndPersists)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    ASSERT_EQ(editor.searchMatchContrast, 100);

    auto sm = makeMachine(editor, FuzzyFindMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_T));
    EXPECT_EQ(editor.searchMatchContrast, 35);

    sm.dispatch(keyCode(control::ControlKey::CTRL_A));
    ASSERT_STREQ(sm.currentStateName(), "GREP");
    EXPECT_EQ(editor.searchMatchContrast, 35);

    sm.dispatch(keyCode(control::ControlKey::CTRL_T));
    EXPECT_EQ(editor.searchMatchContrast, 100);
    sm.dispatch(keyCode(control::ControlKey::ESC));

    auto reopened = makeMachine(editor, FuzzyFindMode{});
    EXPECT_EQ(editor.searchMatchContrast, 100);
    EXPECT_STREQ(reopened.currentStateName(), "FUZZY");
}

TEST(RealModeTransitionsTest, SetSearchContrastAdjustsSharedPaletteStrength)
{
    Editor editor = Editor::createForTests();

    EXPECT_TRUE(editor.handleSetCommand("set searchcontrast=60"));
    EXPECT_EQ(editor.searchMatchContrast, 60);
    EXPECT_TRUE(editor.handleSetCommand("set searchcontrast+=25"));
    EXPECT_EQ(editor.searchMatchContrast, 85);
    EXPECT_TRUE(editor.handleSetCommand("set searchcontrast-=100"));
    EXPECT_EQ(editor.searchMatchContrast, 0);
    EXPECT_TRUE(editor.handleSetCommand("set searchcontrast=101"));
    EXPECT_EQ(editor.searchMatchContrast, 0);
}

TEST(RealModeTransitionsTest, IncrementalForwardSearchStartsAtSavedCursor)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"foo one", "foo two", "foo three"};
    *editor.cursorX = 4;
    *editor.cursorY = 1;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('/');
    sm.dispatch('f');
    sm.dispatch('o');
    sm.dispatch('o');

    EXPECT_EQ(editor.commandBuffer, "/foo");
    ASSERT_EQ(editor.searchMatches.size(), 3u);
    EXPECT_EQ(*editor.cursorY, 2);
    EXPECT_EQ(*editor.cursorX, 0);
    EXPECT_EQ(editor.currentMatchIndex, 2);
    EXPECT_STREQ(sm.currentStateName(), "/");
}

TEST(RealModeTransitionsTest, IncrementalForwardSearchRecomputesAfterEdit)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"abc here", "abd there"};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('/');
    sm.dispatch('a');
    sm.dispatch('b');
    sm.dispatch('c');
    sm.dispatch(keyCode(control::ControlKey::BACKSPACE));
    sm.dispatch('d');

    EXPECT_EQ(editor.commandBuffer, "/abd");
    ASSERT_EQ(editor.searchMatches.size(), 1u);
    EXPECT_EQ(editor.searchMatches[0].row, 1);
    EXPECT_EQ(*editor.cursorY, 1);
    EXPECT_EQ(*editor.cursorX, 0);
    EXPECT_EQ(editor.currentMatchIndex, 0);
}

TEST(RealModeTransitionsTest,
     IncrementalSearchClearsStaleHighlightsOnEmptyQuery)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"needle"};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('/');
    sm.dispatch('n');
    ASSERT_FALSE(editor.searchMatches.empty());
    EXPECT_TRUE(editor.isInSearchMatch(0, 0));

    editor.needsFullRedraw = false;
    sm.dispatch(keyCode(control::ControlKey::CTRL_U));

    EXPECT_EQ(editor.commandBuffer, "/");
    EXPECT_TRUE(editor.searchMatches.empty());
    EXPECT_FALSE(editor.isInSearchMatch(0, 0));
    EXPECT_TRUE(editor.needsFullRedraw);
}

TEST(RealModeTransitionsTest, CtrlXOpensRegexSearchForCurrentBuffer)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    set_buffer_filename(editor, "sample.cpp");
    editor.currentBuffer->lines = {
        "int value1 = 0;",
        "int other = 0;",
        "int value42 = 1;",
    };
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_X));

    ASSERT_STREQ(sm.currentStateName(), "REGEX");
    auto* state = sm.getState<RegexSearchMode>();
    ASSERT_NE(state, nullptr);

    for(char c : std::string("value[0-9]+"))
        sm.dispatch(c);

    state = sm.getState<RegexSearchMode>();
    ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->matches.size(), 2u);
    EXPECT_EQ(state->matches[0].lineNumber, 1);
    EXPECT_EQ(state->matches[0].matchText, "value1");

    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_EQ(*editor.cursorX, 4);
    EXPECT_FALSE(editor.searchMatches.empty());
}

TEST(RealModeTransitionsTest, RegexSearchCtrlSTogglesProjectFiles)
{
    auto root = make_temp_dir("uvim_regex_search_");
    write_file(root / "a.cpp", "int needle1 = 0;\n");
    write_file(root / "b.cpp", "int other = 0;\nint needle22 = 1;\n");
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    editor.createNewBuffer();
    set_buffer_filename(editor, (root / "current.cpp").string());
    editor.currentBuffer->lines = {"int local = 0;"};

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_X));
    sm.dispatch(keyCode(control::ControlKey::CTRL_S));
    for(char c : std::string("needle[0-9]+"))
        sm.dispatch(c);

    auto* state = sm.getState<RegexSearchMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(state->allFiles);
    ASSERT_EQ(state->matches.size(), 2u);
    EXPECT_TRUE(text_utils::is_found(state->matches[0].filepath.find("a.cpp")));
    EXPECT_TRUE(text_utils::is_found(state->matches[1].filepath.find("b.cpp")));
}

TEST(RealModeTransitionsTest, GrepSearchCtrlAEntersBeforeIndexing)
{
    auto root = make_temp_dir("uvim_grep_lazy_");
    write_file(root / "a.txt", "needle\n");
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    editor.createNewBuffer();

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_A));

    EXPECT_STREQ(sm.currentStateName(), "GREP");
    EXPECT_FALSE(editor.grepFileIndexInitialized);

    for(char c : std::string("needle"))
        sm.dispatch(c);

    auto* state = sm.getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);
    flushGrepDebounce(*state, editor);
    ASSERT_EQ(state->matches.size(), 1u);
    EXPECT_TRUE(text_utils::is_found(state->matches[0].filepath.find("a.txt")));
}

TEST(RealModeTransitionsTest, GrepSearchCtrlAReentersWithFreshVisibleState)
{
    auto root = make_temp_dir("uvim_grep_reenter_");
    write_file(root / "a.txt", "needle\n");
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    editor.createNewBuffer();

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_A));
    ASSERT_STREQ(sm.currentStateName(), "GREP");

    sm.dispatch(keyCode(control::ControlKey::ESC));
    ASSERT_STREQ(sm.currentStateName(), "NORMAL");

    sm.dispatch(keyCode(control::ControlKey::CTRL_A));
    EXPECT_STREQ(sm.currentStateName(), "GREP");
    auto* state = sm.getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(state->query.empty());
    EXPECT_TRUE(state->matches.empty());
    EXPECT_TRUE(editor.needsFullRedraw);
}

TEST(RealModeTransitionsTest, DiscardPendingInputClearsBufferedKeys)
{
    Terminal::unreadKey('x');
    ASSERT_TRUE(Terminal::hasBufferedKeys());

    Terminal::discardPendingInput();

    EXPECT_FALSE(Terminal::hasBufferedKeys());
}

TEST(RealModeTransitionsTest, FuzzyFindReusesInMemoryFileIndexUntilRefresh)
{
    auto root = make_temp_dir("uvim_fuzzy_memory_cache_");
    auto otherRoot = make_temp_dir("uvim_fuzzy_other_project_");
    write_file(root / "alpha.txt", "alpha\n");
    write_file(otherRoot / "gamma.txt", "gamma\n");
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    editor.createNewBuffer();

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_P));
    auto* fuzzy = sm.getState<FuzzyFindMode>();
    ASSERT_NE(fuzzy, nullptr);
    ASSERT_EQ(fuzzy->projectFiles.size(), 1u);

    sm.dispatch(keyCode(control::ControlKey::ESC));
    ASSERT_TRUE(editor.fuzzyFileIndexInitialized);
    ASSERT_EQ(editor.fuzzyProjectFiles.size(), 1u);

    write_file(root / "beta.txt", "beta\n");
    sm.dispatch(keyCode(control::ControlKey::CTRL_P));
    fuzzy = sm.getState<FuzzyFindMode>();
    ASSERT_NE(fuzzy, nullptr);
    EXPECT_EQ(fuzzy->projectFiles.size(), 1u);

    fuzzy->refreshFileIndex(editor);
    EXPECT_EQ(fuzzy->projectFiles.size(), 2u);

    sm.dispatch(keyCode(control::ControlKey::ESC));
    std::filesystem::current_path(otherRoot);
    sm.dispatch(keyCode(control::ControlKey::CTRL_P));
    fuzzy = sm.getState<FuzzyFindMode>();
    ASSERT_NE(fuzzy, nullptr);
    ASSERT_EQ(fuzzy->projectFiles.size(), 1u);
    EXPECT_EQ(fuzzy->projectFiles.front().name, "gamma.txt");
}

TEST(RealModeTransitionsTest, CtrlAOpensGrepSearchFromInsertMode)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"abc"};
    *editor.cursorX = 3;

    auto sm = makeMachine(editor, InsertMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_A));

    EXPECT_STREQ(sm.currentStateName(), "GREP");
    auto* state = sm.getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);

    sm.dispatch('v');

    EXPECT_EQ(state->query, "v");
    EXPECT_EQ(editor.currentBuffer->lines[0], "abc");
}

TEST(RealModeTransitionsTest, CtrlAOpensGrepSearchFromReplaceMode)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"abc"};
    *editor.cursorX = 1;

    auto sm = makeMachine(editor, ReplaceMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_A));

    EXPECT_STREQ(sm.currentStateName(), "GREP");
    auto* state = sm.getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);

    sm.dispatch('v');

    EXPECT_EQ(state->query, "v");
    EXPECT_EQ(editor.currentBuffer->lines[0], "abc");
}

TEST(RealModeTransitionsTest, ControllerCtrlAOpensGrepEvenWithStaleModeState)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"abc"};
    *editor.cursorX = 3;
    editor.setModeStateMachineForTests(std::make_unique<ModeStateMachine>(
        createModeContext(&editor), InsertMode{}));
    editor.currentMode = NORMAL;
    EditorModeController controller(editor);

    controller.handleKeypress(keyCode(control::ControlKey::CTRL_A));

    auto* sm = editor.getModeStateMachine();
    ASSERT_NE(sm, nullptr);
    EXPECT_STREQ(sm->currentStateName(), "GREP");
    EXPECT_EQ(editor.currentMode, GREP_SEARCH);

    controller.handleKeypress('v');

    auto* state = sm->getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->query, "v");
    EXPECT_EQ(editor.currentBuffer->lines[0], "abc");
}

TEST(RealModeTransitionsTest,
     ControllerCtrlAReopensGrepWhenCurrentModeIsStaleGrep)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"abc"};
    editor.setModeStateMachineForTests(std::make_unique<ModeStateMachine>(
        createModeContext(&editor), NormalMode{}));
    editor.currentMode = GREP_SEARCH;
    EditorModeController controller(editor);

    controller.handleKeypress(keyCode(control::ControlKey::CTRL_A));

    auto* sm = editor.getModeStateMachine();
    ASSERT_NE(sm, nullptr);
    EXPECT_STREQ(sm->currentStateName(), "GREP");
    EXPECT_EQ(editor.currentMode, GREP_SEARCH);

    controller.handleKeypress('v');

    auto* state = sm->getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->query, "v");
    EXPECT_EQ(editor.currentBuffer->lines[0], "abc");
}

TEST(RealModeTransitionsTest, GrepSearchBatchesQueuedPrintableInput)
{
    auto root = make_temp_dir("uvim_grep_batch_");
    write_file(root / "a.txt", "needle\n");
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    editor.createNewBuffer();

    auto sm = makeMachine(editor, GrepSearchMode{});
    Terminal::unreadKey('e');
    Terminal::unreadKey('l');
    Terminal::unreadKey('d');
    Terminal::unreadKey('e');
    Terminal::unreadKey('e');

    sm.dispatch('n');

    auto* state = sm.getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->query, "needle");
    flushGrepDebounce(*state, editor);
    ASSERT_EQ(state->matches.size(), 1u);
    EXPECT_FALSE(Terminal::hasBufferedKeys());
}

namespace
{
std::string numberedLines(int count, const std::string& marker, int markerLine)
{
    std::string text;
    for(int i = 1; i <= count; ++i)
        text += (i == markerLine ? marker : "line " + std::to_string(i)) + "\n";
    return text;
}

void typeText(ModeStateMachine& sm, const std::string& text)
{
    for(char c : text)
        sm.dispatch(c);
}
} // namespace

TEST(RealModeTransitionsTest, FuzzyFindLineSuffixJumpsToLine)
{
    auto root = make_temp_dir("uvim_fuzzy_line_");
    write_file(root / "target_file.txt", numberedLines(10, "line x", 0));
    write_file(root / "other.txt", "other\n");
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    editor.createNewBuffer();

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_P));
    typeText(sm, "target_file:7");
    auto* fuzzy = sm.getState<FuzzyFindMode>();
    ASSERT_NE(fuzzy, nullptr);
    ASSERT_EQ(fuzzy->matches.size(), 1u) << "the :7 suffix must not filter";

    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    EXPECT_TRUE(text_utils::is_found(
        editor.currentBuffer->filename.find("target_file.txt")));
    EXPECT_EQ(*editor.cursorY, 6);
    EXPECT_TRUE(editor.statusMessage.empty());
}

TEST(RealModeTransitionsTest, FuzzyFindMissingLineJumpsToEndWithMessage)
{
    auto root = make_temp_dir("uvim_fuzzy_line_missing_");
    write_file(root / "short.txt", numberedLines(5, "line x", 0));
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    editor.createNewBuffer();

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_P));
    typeText(sm, "short:415");
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_EQ(*editor.cursorY, static_cast<int>(editor.lines->size()) - 1);
    EXPECT_TRUE(text_utils::is_found(editor.statusMessage.find("not found")))
        << editor.statusMessage;
    EXPECT_TRUE(
        text_utils::is_found(editor.statusMessage.find("jumped to end")));

    sm.dispatch(keyCode(control::ControlKey::CTRL_P));
    typeText(sm, "short:0");
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_TRUE(
        text_utils::is_found(editor.statusMessage.find("jumped to start")));
}

TEST(RealModeTransitionsTest, FuzzyFindLineSuffixKeepsSelectedFile)
{
    auto root = make_temp_dir("uvim_fuzzy_line_keep_");
    write_file(root / "alpha_one.txt", numberedLines(9, "a", 0));
    write_file(root / "alpha_two.txt", numberedLines(9, "b", 0));
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    editor.createNewBuffer();

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_P));
    typeText(sm, "alpha");
    auto* fuzzy = sm.getState<FuzzyFindMode>();
    ASSERT_NE(fuzzy, nullptr);
    ASSERT_EQ(fuzzy->matches.size(), 2u);
    sm.dispatch(keyCode(control::ControlKey::CTRL_J)); // select the second
    const std::string chosen = fuzzy->matches[fuzzy->cursor].file.path;
    typeText(sm, ":3");
    fuzzy = sm.getState<FuzzyFindMode>();
    ASSERT_NE(fuzzy, nullptr);
    EXPECT_EQ(fuzzy->cursor, 1) << "typing the line suffix keeps selection";

    sm.dispatch(keyCode(control::ControlKey::ENTER));
    // Same file, but spelled differently on Windows (8.3 names, slashes).
    std::error_code ec;
    EXPECT_TRUE(
        std::filesystem::equivalent(editor.currentBuffer->filename, chosen, ec))
        << editor.currentBuffer->filename << " vs " << chosen;
    EXPECT_EQ(*editor.cursorY, 2);
}

TEST(RealModeTransitionsTest, FuzzyPreviewScrollFollowsSelection)
{
    auto root = make_temp_dir("uvim_fuzzy_preview_scroll_");
    write_file(root / "scroll_a.txt", numberedLines(50, "a", 0));
    write_file(root / "scroll_b.txt", numberedLines(50, "b", 0));
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    editor.createNewBuffer();

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_P));
    typeText(sm, "scroll");
    auto* fuzzy = sm.getState<FuzzyFindMode>();
    ASSERT_NE(fuzzy, nullptr);
    ASSERT_EQ(fuzzy->matches.size(), 2u);

    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_J));
    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_J));
    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_J));
    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_K));
    fuzzy = sm.getState<FuzzyFindMode>();
    EXPECT_EQ(fuzzy->previewScroll, 2);
    EXPECT_EQ(fuzzy->previewScrollKey, fuzzy->matches[0].file.path);

    // Another file: the offset starts over for it.
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_J));
    fuzzy = sm.getState<FuzzyFindMode>();
    EXPECT_EQ(fuzzy->previewScroll, 1);
    EXPECT_EQ(fuzzy->previewScrollKey, fuzzy->matches[1].file.path);
    EXPECT_STREQ(sm.currentStateName(), "FUZZY") << "scrolling keeps picker";
}

TEST(RealModeTransitionsTest, PreviewHighlightUsesPreviewedFileType)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->filename = "notes.txt"; // current buffer: plain text
    editor.currentBuffer->lines = {"plain"};

    const std::vector<std::string> lines = {"int value = 1; /* starts",
                                            "   still a comment */",
                                            "return \"text\";"};
    PreviewHighlight cache;
    editor.highlightPreviewLines(cache, "preview.cpp", lines, lines.size());
    ASSERT_EQ(cache.tokens.size(), lines.size());

    auto hasType = [](const std::vector<Token>& tokens, TokenType type)
    {
        return std::any_of(tokens.begin(), tokens.end(),
                           [&](const Token& t) { return t.type == type; });
    };
    EXPECT_TRUE(hasType(cache.tokens[0], TOKEN_TYPE)) << "int";
    EXPECT_TRUE(hasType(cache.tokens[0], TOKEN_NUMBER));
    // The block comment opened on line 1 carries over to line 2.
    EXPECT_TRUE(hasType(cache.tokens[1], TOKEN_COMMENT));
    EXPECT_TRUE(hasType(cache.tokens[2], TOKEN_STRING));
    EXPECT_TRUE(hasType(cache.tokens[2], TOKEN_KEYWORD)) << "return";
    // Tokenizing the preview must not change the current buffer's type.
    EXPECT_FALSE(editor.isFileType(FileType::Cpp));

    // Extending is incremental and switching files starts over.
    editor.highlightPreviewLines(cache, "preview.cpp", lines, 1);
    EXPECT_EQ(cache.tokens.size(), lines.size());
    editor.highlightPreviewLines(cache, "other.py", {"# comment"}, 1);
    EXPECT_EQ(cache.path, "other.py");
    ASSERT_EQ(cache.tokens.size(), 1u);
    EXPECT_TRUE(hasType(cache.tokens[0], TOKEN_COMMENT));
}

TEST(RealModeTransitionsTest, PreviewHighlightWorksWithoutAnOpenBuffer)
{
    // Regression: previews from the welcome screen (no buffer) crashed in
    // file type detection, which read the absent current buffer.
    Editor editor = Editor::createForTests();
    ASSERT_EQ(editor.lines, nullptr);

    const std::vector<std::string> script = {"#!/bin/sh", "echo \"hi\" # say"};
    PreviewHighlight cache;
    editor.highlightPreviewLines(cache, "runme", script, script.size());
    ASSERT_EQ(cache.tokens.size(), script.size());
    EXPECT_NE(cache.fileTypeMask &
                  (1ull << static_cast<unsigned>(FileType::Shell)),
              0u)
        << "shebang of the previewed file decides the type";

    PreviewHighlight cpp;
    editor.highlightPreviewLines(cpp, "a.cpp", {"int x = 1;"}, 1);
    EXPECT_EQ(cpp.tokens.size(), 1u);
}

TEST(RealModeTransitionsTest, PreviewShebangIgnoresCurrentBuffer)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"#!/bin/sh", "echo current"};

    PreviewHighlight cache;
    editor.highlightPreviewLines(cache, "notes", {"plain words"}, 1);
    EXPECT_EQ(cache.fileTypeMask &
                  (1ull << static_cast<unsigned>(FileType::Shell)),
              0u)
        << "the current buffer's shebang must not leak into the preview";
}

TEST(RealModeTransitionsTest, GrepLineSuffixOpensResultAtLine)
{
    auto root = make_temp_dir("uvim_grep_line_");
    write_file(root / "a.txt", numberedLines(12, "needle here", 2));
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    editor.createNewBuffer();

    auto sm = makeMachine(editor, GrepSearchMode{});
    typeText(sm, "needle:9");
    auto* state = sm.getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);
    flushGrepDebounce(*state, editor);
    ASSERT_EQ(state->matches.size(), 1u) << "searches for 'needle' only";
    EXPECT_EQ(state->matches[0].lineNumber, 2);

    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    EXPECT_EQ(*editor.cursorY, 8);
    EXPECT_EQ(editor.searchQuery, "needle");
}

TEST(RealModeTransitionsTest, GrepEscapedLineSuffixSearchesLiterally)
{
    auto root = make_temp_dir("uvim_grep_line_escaped_");
    write_file(root / "log.txt", "start\nerror:42 happened\nend\n");
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    editor.createNewBuffer();

    auto sm = makeMachine(editor, GrepSearchMode{});
    typeText(sm, "error\\:42");
    auto* state = sm.getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);
    flushGrepDebounce(*state, editor);
    ASSERT_EQ(state->matches.size(), 1u);
    EXPECT_EQ(state->matches[0].lineNumber, 2);

    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_EQ(*editor.cursorY, 1) << "no jump: opens at the match line";
}

TEST(RealModeTransitionsTest, GrepOpenSeedsSingleSearchMatchLazily)
{
    auto root = make_temp_dir("uvim_grep_open_seed_");
    write_file(root / "a.txt", "needle one\nother\nneedle two\n");
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    editor.createNewBuffer();

    auto sm = makeMachine(editor, GrepSearchMode{});
    for(char c : std::string("needle"))
        sm.dispatch(c);

    auto* state = sm.getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);
    flushGrepDebounce(*state, editor);
    ASSERT_EQ(state->matches.size(), 2u);

    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    EXPECT_TRUE(
        text_utils::is_found(editor.currentBuffer->filename.find("a.txt")));
    EXPECT_EQ(*editor.cursorY, 0);
    ASSERT_EQ(editor.searchMatches.size(), 1u);
    EXPECT_TRUE(editor.searchMatchesPartial);
    EXPECT_EQ(editor.searchMatches[0].row, 0);
    EXPECT_EQ(editor.searchMatches[0].col, 0);

    sm.dispatch(keyCode(typed::TypedKey::KEY_N));

    EXPECT_FALSE(editor.searchMatchesPartial);
    ASSERT_EQ(editor.searchMatches.size(), 2u);
    EXPECT_EQ(*editor.cursorY, 2);
}

#ifdef UVIM_ENABLE_RG_CACHE
TEST(RealModeTransitionsTest, OpenFilePromotesMatchingRgCacheLines)
{
    auto root = make_temp_dir("uvim_rg_cache_open_");
    write_file(root / "a.txt", "disk line\n");
    ScopedCurrentPath cwd(root);

    std::error_code ec;
    const auto size = std::filesystem::file_size(root / "a.txt", ec);
    ASSERT_FALSE(ec);
    const auto mtime = std::filesystem::last_write_time(root / "a.txt", ec);
    ASSERT_FALSE(ec);

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.rgCacheLoaded = true;

    Editor::RgCachedFile cached;
    cached.path = "a.txt";
    cached.size = size;
    cached.mtime = static_cast<long long>(mtime.time_since_epoch().count());
    cached.lines = {"rg cached"};
    cached.lowerLines = {"rg cached"};
    editor.rgCachedFiles.push_back(std::move(cached));

    editor.openFile("a.txt", false);

    ASSERT_NE(editor.currentBuffer, nullptr);
    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "rg cached");
}

TEST(RealModeTransitionsTest, GrepSearchBuildsAndUpdatesRgCache)
{
    auto root = make_temp_dir("uvim_rg_cache_");
    write_file(root / "a.txt", "alpha needle\n");
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    editor.createNewBuffer();

    auto sm = makeMachine(editor, GrepSearchMode{});
    for(char c : std::string("needle"))
        sm.dispatch(c);

    auto* state = sm.getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);
    flushGrepDebounce(*state, editor);
    ASSERT_EQ(state->matches.size(), 1u);
    EXPECT_TRUE(std::filesystem::exists(root / ".rg" / "index.tsv"));

    write_file(root / "a.txt", "alpha\n");
    state->refreshFileIndex(editor);
    sm.dispatch(keyCode(control::ControlKey::BACKSPACE));
    sm.dispatch('e');

    state = sm.getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);
    flushGrepDebounce(*state, editor);
    EXPECT_TRUE(state->matches.empty());
}

TEST(RealModeTransitionsTest, GrepSearchIndexesMultipleFilesConsistently)
{
    auto root = make_temp_dir("uvim_rg_cache_workers_");
    constexpr int fileCount = 24;
    for(int i = 0; i < fileCount; ++i)
    {
        write_file(root / ("file_" + std::to_string(i) + ".txt"),
                   "parallel needle " + std::to_string(i) + "\n");
    }
    ScopedCurrentPath cwd(root);

    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    editor.createNewBuffer();

    auto sm = makeMachine(editor, GrepSearchMode{});
    for(char c : std::string("needle"))
        sm.dispatch(c);

    auto* state = sm.getState<GrepSearchMode>();
    ASSERT_NE(state, nullptr);
    flushGrepDebounce(*state, editor);

    EXPECT_EQ(state->matches.size(), static_cast<size_t>(fileCount));
    EXPECT_EQ(editor.rgCachedFiles.size(), static_cast<size_t>(fileCount));
    EXPECT_TRUE(std::filesystem::exists(root / ".rg" / "index.tsv"));
}
#endif

TEST(RegexFuzzyQueryTest,
     SupportsRegexAlternationClassesAnchorsAndHighlightPositions)
{
    using namespace editor::helper;
    std::vector<int> positions;
    EXPECT_GT(fuzzyScoreWithPositions("re:^(main|test)\\.(cpp|c)$", "MAIN.cpp",
                                      positions),
              0);
    EXPECT_EQ(positions, (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7}));
    EXPECT_LT(fuzzyScoreWithPositions("re:^main", "not_main.cpp", positions),
              0);
    EXPECT_TRUE(positions.empty());
    EXPECT_GT(fuzzyScoreWithPositions("/foo[0-9]+/", "prefix foo42 suffix",
                                      positions),
              0);
    EXPECT_EQ(positions, (std::vector<int>{7, 8, 9, 10, 11}));
    EXPECT_GT(fuzzyScore("abc", "re:^\\D+$"), 0);
    EXPECT_LT(fuzzyScore("123", "re:^\\D+$"), 0);
    EXPECT_GT(fuzzyScore("foo/bar", "/foo\\/bar/"), 0);
}

TEST(RegexFuzzyQueryTest,
     HandlesInvalidEmptyAndZeroWidthQueriesWithoutStalePositions)
{
    using namespace editor::helper;
    std::vector<int> positions{99};
    EXPECT_LT(fuzzyScoreWithPositions("re:[", "abc", positions), 0);
    EXPECT_TRUE(positions.empty());
    EXPECT_NE(regexQueryError("re:[").find("Invalid regex"), std::string::npos);
    EXPECT_LT(fuzzyScore("abc", "re:["), 0);
    EXPECT_GT(fuzzyScoreWithPositions("re:^", "abc", positions), 0);
    EXPECT_TRUE(positions.empty());
    EXPECT_TRUE(regexQueryError("re:^").empty());
    EXPECT_GT(fuzzyScoreWithPositions("re:", "abc", positions), 0);
    EXPECT_TRUE(positions.empty());
    EXPECT_GT(fuzzyScore("abc", "//"), 0);
    EXPECT_GT(fuzzyScore("abc", "re:$"), 0);
}

TEST(RegexFuzzyQueryTest, PreservesOrdinaryFuzzyQueriesAndProvidesLiteralEscape)
{
    using namespace editor::helper;
    std::vector<int> positions;
    EXPECT_GT(fuzzyScoreWithPositions("mn", "main.cpp", positions), 0);
    EXPECT_EQ(positions, (std::vector<int>{0, 3}));
    EXPECT_FALSE(isRegexQuery("/tmp/project"));
    EXPECT_FALSE(isRegexQuery("fz:/tmp/"));
    EXPECT_GT(fuzzyScore("/tmp/project", "fz:/tmp/"), 0);
    EXPECT_GT(fuzzyScore("re:hello", "fz:re:"), 0);
    EXPECT_FALSE(regexQueryScore("normal", "normal").has_value());
    EXPECT_GT(fuzzyScoreWithPositions("re:main", "é main", positions), 0);
    EXPECT_EQ(positions, (std::vector<int>{3, 4, 5, 6}));
}

TEST(RegexFuzzyQueryTest, PreservesRegexPrefixesAndLineSuffixes)
{
    EXPECT_EQ(parsePickerQuery("re:").pattern, "re:");
    EXPECT_FALSE(parsePickerQuery("re:123").hasLine);
    EXPECT_EQ(parsePickerQuery("re:123").pattern, "re:123");
    const auto query = parsePickerQuery("/main\\.cpp$/:42");
    EXPECT_EQ(query.pattern, "/main\\.cpp$/");
    EXPECT_TRUE(query.hasLine);
    EXPECT_EQ(query.line, 42);
    EXPECT_EQ(parsePickerQuery("re:main$:12").pattern, "re:main$");
    EXPECT_EQ(parsePickerQuery("/port:123/").pattern, "/port:123/");
}

TEST(RealModeTransitionsTest, FuzzyFindRegexFiltersNamesAndRetainsLineJump)
{
    auto root = make_temp_dir("uvim_fuzzy_regex_");
    write_file(root / "main.cpp", numberedLines(10, "target", 7));
    write_file(root / "main.h", "header\n");
    write_file(root / "other.cpp", "other\n");
    ScopedCurrentPath cwd(root);
    Editor editor = Editor::createForTests();
    editor.useGitFileIndex = false;
    editor.createNewBuffer();
    auto sm = makeMachine(editor, FuzzyFindMode{});
    typeText(sm, "/^main\\.cpp$/:7");
    auto* finder = sm.getState<FuzzyFindMode>();
    ASSERT_NE(finder, nullptr);
    ASSERT_EQ(finder->matches.size(), 1u);
    EXPECT_EQ(finder->matches[0].file.name, "main.cpp");
    EXPECT_EQ(finder->matches[0].matchPositions.size(), 8u);
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_EQ(*editor.cursorY, 6);
    EXPECT_EQ(std::filesystem::canonical(editor.currentBuffer->filename),
              std::filesystem::canonical(root / "main.cpp"));
}

TEST(RealModeTransitionsTest,
     BufferBrowserRegexMatchesFilenamesAndRecoversFromInvalidPattern)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    set_buffer_filename(editor, "/tmp/regex_main.cpp");
    editor.createNewBuffer();
    set_buffer_filename(editor, "/tmp/regex_other.h");
    auto sm = makeMachine(editor, BufferBrowserMode{});
    typeText(sm, "re:^regex_.*\\.cpp$");
    auto* browser = sm.getState<BufferBrowserMode>();
    ASSERT_NE(browser, nullptr);
    ASSERT_EQ(browser->bufferMatches.size(), 1u);
    EXPECT_EQ(browser->bufferMatches[0].bufferIndex, 0);
    sm.dispatch(keyCode(control::ControlKey::CTRL_U));
    typeText(sm, "re:[");
    EXPECT_TRUE(browser->bufferMatches.empty());
    sm.dispatch(keyCode(control::ControlKey::CTRL_U));
    EXPECT_EQ(browser->bufferMatches.size(), 2u);
}

#ifdef UVIM_ENABLE_GIT_TOOLS
TEST(RegexFuzzyQueryTest, GitLogFilteringUsesRegexAcrossCommitText)
{
    Editor editor = Editor::createForTests();
    GitLogMode log;
    log.entries = {{"abc123", "Fix debugger stepping"}, {"def456", "Update documentation"}};
    log.query = "re:Fix.*stepp(ing)?";
    log.rebuildFilter(editor);
    ASSERT_EQ(log.filtered.size(), 1u);
    EXPECT_EQ(log.filtered[0], 0);
    log.query = "re:[";
    log.rebuildFilter(editor);
    EXPECT_TRUE(log.filtered.empty());
    auto sm = makeMachine(editor, GitLogMode{log.entries, false});
    typeText(sm, "re:Fix.*stepp(ing)?");
    const auto* view = sm.getState<GitLogMode>();
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->query, "re:Fix.*stepp(ing)?");
    EXPECT_FALSE(view->searchActive);
    ASSERT_EQ(view->filtered.size(), 1u);
}
#endif

#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
TEST(RegexFuzzyQueryTest, HelpSearchSupportsRegexAndInvalidPatternRecovery)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, HelpMode{});
    sm.dispatch('/');
    typeText(sm, "re:.*");
    auto* help = sm.getState<HelpMode>();
    ASSERT_NE(help, nullptr);
    EXPECT_EQ(help->searchQuery, "re:.*");
    EXPECT_FALSE(help->searchMatches.empty());
    sm.dispatch(keyCode(control::ControlKey::CTRL_U));
    typeText(sm, "re:[");
    EXPECT_TRUE(help->searchMatches.empty());
    sm.dispatch(keyCode(control::ControlKey::CTRL_U));
    EXPECT_FALSE(help->searchMatches.empty());
}

#endif

TEST(FuzzyTypoTest, FindsSomethingWithOutOfOrderCharactersAndSortedHighlights)
{
    std::vector<int> positions;
    const int typo = editor::helper::fuzzyScoreWithPositions(
        "shtio", "something", positions);
    EXPECT_GT(typo, 0);
    EXPECT_EQ(positions, (std::vector<int>{0, 1, 4, 5, 6}));
    EXPECT_GT(
        editor::helper::fuzzyScoreWithPositions("shtio", "shtio", positions),
        typo);
    EXPECT_GT(
        editor::helper::fuzzyScoreWithPositions("fiel", "file", positions), 0);
    EXPECT_EQ(positions, (std::vector<int>{0, 1, 2, 3}));
}

TEST(FuzzyTypoTest,
     PreservesRepeatedLettersAndRejectsScatteredOrMissingCharacters)
{
    std::vector<int> positions;
    EXPECT_GT(
        editor::helper::fuzzyScoreWithPositions("bkoo", "book", positions), 0);
    EXPECT_EQ(positions, (std::vector<int>{0, 1, 2, 3}));
    EXPECT_LT(editor::helper::fuzzyScoreWithPositions("bkk", "book", positions),
              0);
    EXPECT_TRUE(positions.empty());
    EXPECT_GT(editor::helper::fuzzyScoreWithPositions("ab", "ba", positions),
              0);
    EXPECT_LT(
        editor::helper::fuzzyScoreWithPositions("ab", "b____a", positions), 0);
    EXPECT_LT(editor::helper::fuzzyScoreWithPositions(
                  "shtio", "s________o________mething", positions),
              0);
    EXPECT_LT(editor::helper::fuzzyScoreWithPositions("shtiox", "something",
                                                      positions),
              0);
}

TEST(FuzzyTypoTest, LeavesRegexExactAndKeepsUtf8ByteOffsetsIntact)
{
    std::vector<int> positions;
    EXPECT_LT(editor::helper::fuzzyScoreWithPositions("re:shtio", "something",
                                                      positions),
              0);
    EXPECT_LT(editor::helper::fuzzyScoreWithPositions("/shtio/", "something",
                                                      positions),
              0);
    EXPECT_GT(editor::helper::fuzzyScoreWithPositions("fz:shtio", "é something",
                                                      positions),
              0);
    EXPECT_EQ(positions, (std::vector<int>{3, 4, 7, 8, 9}));
    EXPECT_LT(editor::helper::fuzzyScoreWithPositions("éa", "aé", positions),
              0);
}

TEST(FuzzyTypoTest, FileFinderRanksOrderedNamesAboveReorderedMatches)
{
    Editor editor = Editor::createForTests();
    FuzzyFindMode finder;
    finder.projectFiles = {{"something.cpp", "something.cpp", false},
                           {"shtio.cpp", "shtio.cpp", false},
                           {"unrelated.cpp", "unrelated.cpp", false}};
    finder.projectFilesInitialized = true;
    auto sm = makeMachine(editor, std::move(finder));
    typeText(sm, "shtio");
    const auto* state = sm.getState<FuzzyFindMode>();
    ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->matches.size(), 2u);
    EXPECT_EQ(state->matches[0].file.name, "shtio.cpp");
    EXPECT_EQ(state->matches[1].file.name, "something.cpp");
    EXPECT_EQ(state->matches[1].matchPositions,
              (std::vector<int>{0, 1, 4, 5, 6}));
}

TEST(FuzzyTypoTest, SharedScoringApisAgreeWithAndWithoutHighlights)
{
    for(const std::string query : {"", "shtio", "smt", "fz:shtio", "re:some.*",
                                   "/shtio/", "re:[", "missing", "bkk"})
    {
        SCOPED_TRACE(query);
        std::vector<int> positions{999}, legacyPositions{999};
        const int score =
            editor::helper::fuzzyMatchScore(query, "something", &positions);
        EXPECT_EQ(editor::helper::fuzzyMatchScore(query, "something"), score);
        EXPECT_EQ(editor::helper::fuzzyScore("something", query), score);
        EXPECT_EQ(editor::helper::fuzzyScoreWithPositions(query, "something",
                                                          legacyPositions),
                  score);
        EXPECT_EQ(positions, legacyPositions);
        if(score <= 0)
            EXPECT_TRUE(positions.empty());
    }
}

TEST(FuzzyTypoTest, CompletionUsesSharedMatcherAndKeepsOrderedMatchesFirst)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"shtio"};
    *editor.cursorX = 5;
    *editor.cursorY = 0;
    CompletionEntry typo, exact, unrelated;
    typo.label = "something";
    exact.label = "shtio";
    unrelated.label = "unrelated";
    editor.completionAll = {typo, exact, unrelated};
    editor.completionActive = true;
    editor.completionAnchorX = 0;
    editor.completionAnchorY = 0;
    editor.rebuildCompletionFilter();
    EXPECT_TRUE(editor.completionActive);
    EXPECT_EQ(editor.completionFiltered, (std::vector<int>{1, 0}));
}

TEST(FuzzyTypoTest, EmojiCompletionUsesSharedMatcher)
{
    Editor editor = Editor::createForTests();
    editor.emojiEntries = {{"", "", "something", ""},
                           {"", "", "shtio", ""},
                           {"", "", "unrelated", ""}};
    editor.emojiQuery = "shtio";
    editor.rebuildEmojiFilter();
    EXPECT_EQ(editor.emojiFiltered, (std::vector<int>{1, 0}));
    editor.emojiQuery = "re:shtio";
    editor.rebuildEmojiFilter();
    EXPECT_EQ(editor.emojiFiltered, (std::vector<int>{1}));
}

TEST(FuzzyTypoTest, FileBrowserUsesSharedMatcher)
{
    const auto root = make_temp_dir("uvim_shared_fuzzy_browser_");
    write_file(root / "something.cpp", "");
    write_file(root / "shtio.cpp", "");
    write_file(root / "unrelated.cpp", "");
    Editor editor = Editor::createForTests();
    editor.fileBrowserFuzzy = true;
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});
    typeText(sm, ":filter shtio");
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    const auto* browser = sm.getState<FileBrowserMode>();
    ASSERT_NE(browser, nullptr);
    std::vector<std::string> names;
    for(int index : browser->filterMatches)
        if(browser->fileList[index].name != "..")
            names.push_back(browser->fileList[index].name);
    EXPECT_EQ(names, (std::vector<std::string>{"shtio.cpp", "something.cpp"}));
}

TEST(FuzzyTypoTest, RecursivePathCompletionUsesLiteralSharedFuzzyMatching)
{
    const auto root = make_temp_dir("uvim_shared_fuzzy_path_");
    write_file(root / "something.cpp", "");
    write_file(root / "shtio.cpp", "");
#ifndef _WIN32
    write_file(root / "re:something.cpp", "");
#endif
    const auto matches = editor::helper::getRecursivePathCompletions(
        root.string() + "/shtio", false);
    ASSERT_FALSE(matches.empty());
    EXPECT_EQ(matches.front(), (root / "shtio.cpp").string());
    EXPECT_NE(std::find(matches.begin(), matches.end(),
                        (root / "something.cpp").string()),
              matches.end());
#ifndef _WIN32
    const auto literal = editor::helper::getRecursivePathCompletions(
        root.string() + "/re:shtio", false);
    EXPECT_NE(std::find(literal.begin(), literal.end(),
                        (root / "re:something.cpp").string()),
              literal.end());
    EXPECT_EQ(std::find(literal.begin(), literal.end(),
                        (root / "something.cpp").string()),
              literal.end());
#endif
}

TEST(FuzzyTypoTest, AssemblyMatchesAsslbyWhileTypingAndHighlightsBothEsses)
{
    std::vector<int> positions;
    const int score = editor::helper::fuzzyMatchScore("asslby", "assembly",
                                                      &positions);
    ASSERT_GT(score, 0);
    EXPECT_EQ(positions, (std::vector<int>{0, 1, 2, 5, 6, 7}));
    EXPECT_GT(editor::helper::fuzzyMatchScore("asslby", "asslby"), score);
    EXPECT_LT(editor::helper::fuzzyMatchScore("asslby", "ably"), 0);
    EXPECT_LT(editor::helper::fuzzyMatchScore("re:asslby", "assembly"), 0);

    Editor editor = Editor::createForTests();
    FuzzyFindMode finder;
    finder.projectFiles = {{"assembly.cpp", "assembly.cpp", false},
                           {"unrelated.cpp", "unrelated.cpp", false}};
    finder.projectFilesInitialized = true;
    auto sm = makeMachine(editor, std::move(finder));
    for(char ch : std::string("asslby"))
    {
        sm.dispatch(ch);
        const auto* state = sm.getState<FuzzyFindMode>();
        ASSERT_NE(state, nullptr);
        EXPECT_TRUE(std::any_of(state->matches.begin(), state->matches.end(),
                                 [](const FuzzyMatch& match)
                                 { return match.file.name == "assembly.cpp"; }))
            << state->query;
    }
    EXPECT_EQ(sm.getState<FuzzyFindMode>()->matches[0].matchPositions,
              (std::vector<int>{0, 1, 2, 5, 6, 7}));
}

TEST(FuzzyTypoTest, CompletionListsUseSharedRankingAndPreserveEmptyQueryOrder)
{
    const std::vector<std::string> candidates = {"something", "shtio", "nothing"};
    EXPECT_EQ(editor::helper::fuzzyCompletions(candidates, "shtio"),
              (std::vector<std::string>{"shtio", "something"}));
    EXPECT_EQ(editor::helper::fuzzyCompletions(candidates, ""), candidates);
    EXPECT_EQ(editor::helper::fuzzyCompletions(candidates, "re:^some"),
              (std::vector<std::string>{"something"}));
    EXPECT_TRUE(editor::helper::fuzzyCompletions(candidates, "re:[").empty());
}

TEST(FuzzyTypoTest, CommandSettingsAndHelpTabCandidatesUseSharedMatcher)
{
    Editor editor = Editor::createForTests();
    const auto commands = editor.getCommandCompletions("edti");
    EXPECT_NE(std::find(commands.begin(), commands.end(), "edit "),
              commands.end());
    const auto settings = editor.getSetCompletions("set autobraecs");
    EXPECT_NE(std::find(settings.begin(), settings.end(), "set autobraces"),
              settings.end());
    const auto help = editor.getHelpCompletions("navgi");
    EXPECT_NE(std::find(help.begin(), help.end(), "navigation"), help.end());
    EXPECT_EQ(editor.getHelpCompletions("re:^nav.*$"),
              (std::vector<std::string>{"navigation"}));
}

TEST(FuzzyTypoTest, HelpPopupKeepsFuzzyTopicsAndSupportsArgumentRegex)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});
    typeText(sm, ":help navgi");
    ASSERT_TRUE(editor.commandPopupSelection().has_value());
    EXPECT_EQ(*editor.commandPopupSelection(), "help navigation");
    sm.dispatch(keyCode(control::ControlKey::TAB));
    EXPECT_EQ(editor.commandBuffer, ":help navigation");
    sm.dispatch(keyCode(control::ControlKey::ESC));

    typeText(sm, ":help re:^nav.*$");
    ASSERT_EQ(editor.commandPopupFiltered.size(), 1u);
    EXPECT_EQ(*editor.commandPopupSelection(), "help navigation");
    sm.dispatch(keyCode(control::ControlKey::ESC));
    typeText(sm, ":help re:[");
    EXPECT_TRUE(editor.commandPopupFiltered.empty());
    EXPECT_NE(editor.commandPopupEmptyMessage.find("Invalid regex"),
              std::string::npos);
}

TEST(FuzzyTypoTest, WelcomeHelpPromptUsesSharedMatcherAndTabSelection)
{
    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, WelcomeMode{});
    typeText(sm, ":help navgi");
    ASSERT_TRUE(editor.commandPopupSelection().has_value());
    EXPECT_EQ(*editor.commandPopupSelection(), "help navigation");
    sm.dispatch(keyCode(control::ControlKey::TAB));
    ASSERT_NE(editor.commandPrompt, nullptr);
    EXPECT_EQ(editor.commandPrompt->getInput(), "help navigation");
}

TEST(FuzzyTypoTest, DirectPathCompletionUsesSharedMatcherAndLiteralPaths)
{
    const auto root = make_temp_dir("uvim_direct_fuzzy_paths_");
    write_file(root / "assembly.h", "");
    write_file(root / "asslby.h", "");
    write_file(root / "unrelated.h", "");
    const auto matches =
        editor::helper::getPathCompletions(root.string() + "/asslby");
    EXPECT_EQ(matches,
              (std::vector<std::string>{(root / "asslby.h").string(),
                                        (root / "assembly.h").string()}));
#ifndef _WIN32
    write_file(root / "re:assembly.h", "");
    const auto literal =
        editor::helper::getPathCompletions(root.string() + "/re:asslby");
    EXPECT_EQ(literal,
              (std::vector<std::string>{(root / "re:assembly.h").string()}));
#endif
}

#ifdef UVIM_ENABLE_GIT_TOOLS
TEST(FuzzyTypoTest, GitSubcommandTabFallbackUsesSharedMatcherInBothPrompts)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});
    typeText(sm, ":git pttyrlog");
    editor.cancelCommandPopup();
    sm.dispatch(keyCode(control::ControlKey::TAB));
    EXPECT_EQ(editor.commandBuffer, ":git prettylog");
    sm.dispatch(keyCode(control::ControlKey::ESC));

    auto welcome = makeMachine(editor, WelcomeMode{});
    typeText(welcome, ":git pttyrlog");
    editor.cancelCommandPopup();
    welcome.dispatch(keyCode(control::ControlKey::TAB));
    ASSERT_NE(editor.commandPrompt, nullptr);
    EXPECT_EQ(editor.commandPrompt->getInput(), "git prettylog");
}
#endif
