#include "real_mode_test_utils.h"

using namespace uvim_test;

TEST(RealModeTransitionsTest, LocListReturnsToFileBrowserWithCursorState)
{
    auto root = make_temp_dir("uvim_loc_return_");
    write_file(root / "a.cpp", "int main() { return 0; }\n");
    write_file(root / "b.cpp", "int add(int a,int b){return a+b;}\n");

    std::error_code ec;
    std::filesystem::current_path(root, ec);

    Editor editor = Editor::createForTests();
    FileBrowserMode browse{root.string()};
    browse.browserCursor = 1;
    browse.browserOffset = 0;

    auto sm = makeMachine(editor, browse);

    sm.dispatch(':');
    sm.dispatch('l');
    sm.dispatch('o');
    sm.dispatch('c');
    sm.dispatch(' ');
    sm.dispatch('.');
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm.currentStateName(), "LOC");

    sm.dispatch('q');

    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
    auto* fb = sm.getState<FileBrowserMode>();
    ASSERT_NE(fb, nullptr);
    EXPECT_EQ(fb->currentDirectory, root.string());
    EXPECT_EQ(fb->browserCursor, 1);
    EXPECT_EQ(fb->browserOffset, 0);
}

TEST(RealModeTransitionsTest, EditDotCommandOpensFileBrowser)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(':');
    sm.dispatch('e');
    sm.dispatch(' ');
    sm.dispatch('.');
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
}

TEST(RealModeTransitionsTest, EditCommandWithPathOpensFileBrowser)
{
    const auto root = make_temp_dir("uvim_edit_path_browser_");
    write_file(root / "alpha.txt", "alpha\n");

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(':');
    sm.dispatch('e');
    sm.dispatch(' ');
    for(char ch : root.string())
        sm.dispatch(ch);
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
}

TEST(RealModeTransitionsTest, FileBrowserReusesHotListingWithoutCopying)
{
    const auto root = make_temp_dir("uvim_browser_hot_cache_");
    write_file(root / "alpha.txt", "alpha\n");
    write_file(root / "beta.txt", "beta\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* firstBrowser = sm.getState<FileBrowserMode>();
    ASSERT_NE(firstBrowser, nullptr);
    ASSERT_FALSE(firstBrowser->fileList.empty());
    const FileEntry* firstStorage = firstBrowser->fileList.data();

    sm.transitionTo(NormalMode{});
    EXPECT_TRUE(editor.fileBrowserHotListingInitialized);

    sm.transitionTo(FileBrowserMode{root.string()});
    auto* secondBrowser = sm.getState<FileBrowserMode>();
    ASSERT_NE(secondBrowser, nullptr);
    EXPECT_EQ(secondBrowser->fileList.data(), firstStorage);
    EXPECT_FALSE(editor.fileBrowserHotListingInitialized);
}

TEST(RealModeTransitionsTest, FileBrowserRejectsChangedHotListing)
{
    const auto root = make_temp_dir("uvim_browser_stale_hot_cache_");
    write_file(root / "alpha.txt", "alpha\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});
    sm.transitionTo(NormalMode{});

    write_file(root / "beta.txt", "beta\n");
    std::error_code ec;
    const auto oldTime = std::filesystem::last_write_time(root, ec);
    ASSERT_FALSE(ec);
    std::filesystem::last_write_time(root, oldTime + std::chrono::seconds(1),
                                     ec);
    ASSERT_FALSE(ec);

    sm.transitionTo(FileBrowserMode{root.string()});
    auto* browser = sm.getState<FileBrowserMode>();
    ASSERT_NE(browser, nullptr);
    EXPECT_TRUE(std::any_of(browser->fileList.begin(), browser->fileList.end(),
                            [](const FileEntry& entry)
                            { return entry.name == "beta.txt"; }));
}

TEST(RealModeTransitionsTest, EditDotCommandFromCommandModeOpensFileBrowser)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(':');
    sm.dispatch('e');
    sm.dispatch(' ');
    sm.dispatch('.');
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
}

TEST(RealModeTransitionsTest, CommandLineNumberInputSuppressesCommandPopup)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(':');
    ASSERT_TRUE(editor.commandPopupActive);
    sm.dispatch('5');
    sm.dispatch('2');
    sm.dispatch('5');

    EXPECT_FALSE(editor.commandPopupActive);
    EXPECT_TRUE(editor.needsFullRedraw);
}

TEST(RealModeTransitionsTest, EnteringCommandModeClearsCompletionPopup)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.completionActive = true;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(':');

    EXPECT_FALSE(editor.completionActive);
}

TEST(RealModeTransitionsTest, FileBrowserFuzzyDisabledIgnoresTyping)
{
    Editor editor = Editor::createForTests();
    editor.fileBrowserFuzzy = false;
    auto sm = makeMachine(editor, FileBrowserMode{std::string(".")});

    sm.dispatch('a');

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_FALSE(state->filterActive);
    EXPECT_TRUE(state->filterQuery.empty());
    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
}

TEST(RealModeTransitionsTest,
     FileBrowserCommandSlashRegexSearchStaysInCurrentDirectory)
{
    auto root = make_temp_dir("uvim_browse_regex_local_");
    write_file(root / "a.txt", "a\n");
    write_file(root / "sub" / "needle.txt", "n\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int aIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "a.txt")
        {
            aIndex = i;
            break;
        }
    }
    ASSERT_GE(aIndex, 0);

    dispatch_command(sm, "/needle\\.txt");
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(text_utils::is_found(
        editor.statusMessage.find("No match for regex: needle\\.txt")));
    EXPECT_NE(state->browserCursor, aIndex);

    dispatch_command(sm, "/a\\.txt");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    ASSERT_NE(editor.currentBuffer, nullptr);
    EXPECT_TRUE(
        text_utils::is_found(editor.currentBuffer->filename.find("a.txt")));
}

TEST(RealModeTransitionsTest, FileBrowserCommandQuestionRegexSearchesBackward)
{
    auto root = make_temp_dir("uvim_browse_regex_back_");
    write_file(root / "alpha.txt", "a\n");
    write_file(root / "beta.txt", "b\n");
    write_file(root / "gamma.txt", "g\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int alphaIndex = -1;
    int betaIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "alpha.txt")
            alphaIndex = i;
        if(state->fileList[i].name == "beta.txt")
            betaIndex = i;
    }
    ASSERT_GE(alphaIndex, 0);
    ASSERT_GE(betaIndex, 0);

    state->browserCursor = betaIndex;
    dispatch_command(sm, "?^alpha\\.txt$");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    ASSERT_NE(editor.currentBuffer, nullptr);
    EXPECT_TRUE(
        text_utils::is_found(editor.currentBuffer->filename.find("alpha.txt")));
}

TEST(RealModeTransitionsTest, FileBrowserSlashKeyRunsLocalRegexSearch)
{
    auto root = make_temp_dir("uvim_browse_slash_key_");
    write_file(root / "alpha.txt", "a\n");
    write_file(root / "beta.txt", "b\n");
    write_file(root / "sub" / "alpha.txt", "nested\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int alphaIndex = -1;
    int betaIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "alpha.txt")
            alphaIndex = i;
        if(state->fileList[i].name == "beta.txt")
            betaIndex = i;
    }
    ASSERT_GE(alphaIndex, 0);
    ASSERT_GE(betaIndex, 0);

    state->browserCursor = betaIndex;
    sm.dispatch('/');
    sm.dispatch('a');
    sm.dispatch('l');
    sm.dispatch('p');
    sm.dispatch('h');
    sm.dispatch('a');
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    ASSERT_NE(editor.currentBuffer, nullptr);
    EXPECT_TRUE(
        text_utils::is_found(editor.currentBuffer->filename.find("alpha.txt")));
}

TEST(RealModeTransitionsTest, FileBrowserQuestionKeyRunsBackwardRegexSearch)
{
    auto root = make_temp_dir("uvim_browse_question_key_");
    write_file(root / "alpha.txt", "a\n");
    write_file(root / "beta.txt", "b\n");
    write_file(root / "gamma.txt", "g\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int alphaIndex = -1;
    int gammaIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "alpha.txt")
            alphaIndex = i;
        if(state->fileList[i].name == "gamma.txt")
            gammaIndex = i;
    }
    ASSERT_GE(alphaIndex, 0);
    ASSERT_GE(gammaIndex, 0);

    state->browserCursor = gammaIndex;
    sm.dispatch('?');
    sm.dispatch('a');
    sm.dispatch('l');
    sm.dispatch('p');
    sm.dispatch('h');
    sm.dispatch('a');
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    ASSERT_NE(editor.currentBuffer, nullptr);
    EXPECT_TRUE(
        text_utils::is_found(editor.currentBuffer->filename.find("alpha.txt")));
}

TEST(RealModeTransitionsTest, FileBrowserCtrlAStillOpensGrepSearch)
{
    auto root = make_temp_dir("uvim_browse_ctrls_");
    write_file(root / "a.txt", "a\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    sm.dispatch(keyCode(control::ControlKey::CTRL_A));

    EXPECT_STREQ(sm.currentStateName(), "GREP");
}

TEST(RealModeTransitionsTest, FileBrowserNAndNShiftWrapSearchMatches)
{
    auto root = make_temp_dir("uvim_browse_n_wrap_");
    write_file(root / "edit-a.txt", "a\n");
    write_file(root / "edit-b.txt", "b\n");
    write_file(root / "zzz.txt", "z\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int aIndex = -1;
    int bIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "edit-a.txt")
            aIndex = i;
        if(state->fileList[i].name == "edit-b.txt")
            bIndex = i;
    }
    ASSERT_GE(aIndex, 0);
    ASSERT_GE(bIndex, 0);

    state->searchMatches = {aIndex, bIndex};
    state->lastSearchPattern = "edit";
    state->lastSearchPrefix = '/';
    state->currentSearchMatch = 0;
    state->browserCursor = aIndex;

    sm.dispatch('n');
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->browserCursor, bIndex);

    sm.dispatch('n');
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->browserCursor, aIndex);

    sm.dispatch('N');
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->browserCursor, bIndex);
}

TEST(RealModeTransitionsTest, FileBrowserSearchTabCompletionCyclesMatches)
{
    auto root = make_temp_dir("uvim_browse_search_tab_");
    write_file(root / "edit-alpha.txt", "a\n");
    write_file(root / "edit-beta.txt", "b\n");
    write_file(root / "zzz.txt", "z\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int alphaIndex = -1;
    int betaIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "edit-alpha.txt")
            alphaIndex = i;
        if(state->fileList[i].name == "edit-beta.txt")
            betaIndex = i;
    }
    ASSERT_GE(alphaIndex, 0);
    ASSERT_GE(betaIndex, 0);

    sm.dispatch('/');
    sm.dispatch('e');
    sm.dispatch(keyCode(control::ControlKey::TAB));
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    ASSERT_NE(editor.currentBuffer, nullptr);
    EXPECT_TRUE(text_utils::is_found(
        editor.currentBuffer->filename.find("edit-alpha.txt")));

    Editor editor2 = Editor::createForTests();
    auto sm2 = makeMachine(editor2, FileBrowserMode{root.string()});
    sm2.dispatch('/');
    sm2.dispatch('e');
    sm2.dispatch(keyCode(control::ControlKey::SHIFT_TAB));
    sm2.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm2.currentStateName(), "NORMAL");
    ASSERT_NE(editor2.currentBuffer, nullptr);
    EXPECT_TRUE(
        text_utils::is_found(editor2.currentBuffer->filename.find("edit-")));
}

TEST(RealModeTransitionsTest, FileBrowserCtrlJKCyclesWhileSearchPromptActive)
{
    auto root = make_temp_dir("uvim_browse_ctrljk_prompt_");
    write_file(root / "edit-alpha.txt", "a\n");
    write_file(root / "edit-beta.txt", "b\n");
    write_file(root / "edit-gamma.txt", "g\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int alphaIndex = -1;
    int betaIndex = -1;
    int gammaIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "edit-alpha.txt")
            alphaIndex = i;
        if(state->fileList[i].name == "edit-beta.txt")
            betaIndex = i;
        if(state->fileList[i].name == "edit-gamma.txt")
            gammaIndex = i;
    }
    ASSERT_GE(alphaIndex, 0);
    ASSERT_GE(betaIndex, 0);
    ASSERT_GE(gammaIndex, 0);

    sm.dispatch('/');
    sm.dispatch('e');
    sm.dispatch('d');
    sm.dispatch('i');
    sm.dispatch('t');
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    ASSERT_TRUE(state->commandPrompt);
    EXPECT_TRUE(state->commandPrompt->isActive());
    EXPECT_EQ(state->commandPrompt->getInput(), "/edit");

    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    ASSERT_TRUE(state->commandPrompt);
    EXPECT_TRUE(state->commandPrompt->isActive());
    EXPECT_EQ(state->browserCursor, betaIndex);

    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->browserCursor, gammaIndex);

    sm.dispatch(keyCode(control::ControlKey::CTRL_K));
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->browserCursor, betaIndex);

    sm.dispatch(keyCode(control::ControlKey::CTRL_K));
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->browserCursor, alphaIndex);
}

TEST(RealModeTransitionsTest, FileBrowserEscClearsSearchBeforeExit)
{
    auto root = make_temp_dir("uvim_browse_esc_clear_search_");
    write_file(root / "edit-alpha.txt", "a\n");
    write_file(root / "edit-beta.txt", "b\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    sm.dispatch('/');
    sm.dispatch('e');
    sm.dispatch('d');
    sm.dispatch('i');
    sm.dispatch('t');
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    ASSERT_FALSE(state->searchMatches.empty());

    sm.dispatch(keyCode(control::ControlKey::ESC));
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
    EXPECT_TRUE(state->searchMatches.empty());
    EXPECT_TRUE(state->lastSearchPattern.empty());
}

TEST(RealModeTransitionsTest, FileBrowserSearchTypingMovesToFirstMatch)
{
    auto root = make_temp_dir("uvim_browse_live_first_match_");
    write_file(root / "alpha.txt", "a\n");
    write_file(root / "beta.txt", "b\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int alphaIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "alpha.txt")
            alphaIndex = i;
    }
    ASSERT_GE(alphaIndex, 0);

    sm.dispatch('/');
    sm.dispatch('a');

    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    ASSERT_TRUE(state->commandPrompt);
    EXPECT_TRUE(state->commandPrompt->isActive());
    EXPECT_EQ(state->browserCursor, alphaIndex);
}

TEST(RealModeTransitionsTest, FileBrowserSearchEnterOpensMatchedFile)
{
    auto root = make_temp_dir("uvim_browse_search_enter_file_");
    write_file(root / "target.txt", "hello\n");
    write_file(root / "other.txt", "other\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    sm.dispatch('/');
    sm.dispatch('t');
    sm.dispatch('a');
    sm.dispatch('r');
    sm.dispatch('g');
    sm.dispatch('e');
    sm.dispatch('t');
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    ASSERT_NE(editor.currentBuffer, nullptr);
    EXPECT_TRUE(text_utils::is_found(
        editor.currentBuffer->filename.find("target.txt")));
}

TEST(RealModeTransitionsTest,
     FileBrowserPromptSearchCtrlNEnterOpensSelectedMatches)
{
    auto root = make_temp_dir("uvim_browse_search_multi_open_");
    write_file(root / "match-alpha.txt", "alpha\n");
    write_file(root / "match-beta.txt", "beta\n");
    write_file(root / "other.txt", "other\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    sm.dispatch(':');
    sm.dispatch('/');
    for(char ch : std::string("match-"))
        sm.dispatch(ch);
    sm.dispatch(keyCode(control::ControlKey::CTRL_N));

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->selectedFiles.size(), 2u);

    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    EXPECT_EQ(editor.buffers.size(), 2u);
    ASSERT_NE(editor.currentBuffer, nullptr);
    EXPECT_TRUE(text_utils::is_found(
        editor.buffers[0]->filename.find("match-alpha.txt")));
    EXPECT_TRUE(text_utils::is_found(
        editor.buffers[1]->filename.find("match-beta.txt")));
}

TEST(RealModeTransitionsTest, FileBrowserSearchEnterOpensMatchedDirectory)
{
    auto root = make_temp_dir("uvim_browse_search_enter_dir_");
    write_file(root / "docs" / "readme.txt", "docs\n");
    write_file(root / "z.txt", "z\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    sm.dispatch('/');
    sm.dispatch('d');
    sm.dispatch('o');
    sm.dispatch('c');
    sm.dispatch('s');
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    const std::string mode = sm.currentStateName();
    EXPECT_TRUE(mode == "BROWSE" || mode == "NORMAL");
}

TEST(RealModeTransitionsTest, FileBrowserParentThenEnterSiblingDirectoryStays)
{
    auto root = make_temp_dir("uvim_browse_parent_then_sibling_");
    write_file(root / "from" / "a.txt", "from\n");
    write_file(root / "to" / "b.txt", "to\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{(root / "from").string()});

    // Enter parent via ".." (first entry).
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);

    int toIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "to")
        {
            toIndex = i;
            break;
        }
    }
    ASSERT_GE(toIndex, 0);

    for(int i = 0; i < toIndex; ++i)
        sm.dispatch('j');

    sm.dispatch(keyCode(control::ControlKey::ENTER));

    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    // Compare via path components to stay portable (Windows uses backslash).
    auto endsWithDir = [](const std::string& path,
                          const std::string& dirName) -> bool
    {
        std::filesystem::path p(path);
        return p.filename() == dirName;
    };
    EXPECT_TRUE(endsWithDir(state->currentDirectory, "to"));
    EXPECT_FALSE(endsWithDir(state->currentDirectory, "from"));
}

TEST(RealModeTransitionsTest, BufferBrowserSelectionCanJumpBackAndForward)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->filename = "one.txt";
    editor.currentBuffer->lines = {"one"};
    editor.createNewBuffer();
    editor.currentBuffer->filename = "two.txt";
    editor.currentBuffer->lines = {"two"};
    editor.switchToBuffer(0);
    *editor.cursorX = 2;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_W));
    ASSERT_STREQ(sm.currentStateName(), "BUFFERS");

    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    sm.dispatch(keyCode(control::ControlKey::ENTER));

    EXPECT_EQ(editor.currentBufferIndex, 1);
    ASSERT_EQ(editor.jumpBackStack.size(), 1u);
    ASSERT_STREQ(sm.currentStateName(), "NORMAL");

    sm.dispatch(keyCode(control::ControlKey::CTRL_O));

    EXPECT_EQ(editor.currentBufferIndex, 0);
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_EQ(*editor.cursorX, 2);
    ASSERT_EQ(editor.jumpForwardStack.size(), 1u);

    sm.dispatch(keyCode(control::ControlKey::CTRL_I));

    EXPECT_EQ(editor.currentBufferIndex, 1);
}

TEST(RealModeTransitionsTest, BufferBrowserStartsOnCurrentBuffer)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->filename = "one.txt";
    editor.createNewBuffer();
    editor.currentBuffer->filename = "two.txt";
    editor.createNewBuffer();
    editor.currentBuffer->filename = "three.txt";
    editor.switchToBuffer(1);

    auto sm = makeMachine(editor, BufferBrowserMode{});
    auto* browser = sm.getState<BufferBrowserMode>();
    ASSERT_NE(browser, nullptr);
    ASSERT_GE(browser->bufferCursor, 0);
    ASSERT_LT(browser->bufferCursor,
              static_cast<int>(browser->bufferMatches.size()));
    EXPECT_EQ(browser->bufferMatches[browser->bufferCursor].bufferIndex, 1);
}

TEST(RealModeTransitionsTest, BufferBrowserCtrlXClosesSelectedBuffer)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->filename = "one.txt";
    editor.currentBuffer->lines = {"one"};
    editor.createNewBuffer();
    editor.currentBuffer->filename = "two.txt";
    editor.currentBuffer->lines = {"two"};
    editor.switchToBuffer(0);

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_W));
    ASSERT_STREQ(sm.currentStateName(), "BUFFERS");

    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    sm.dispatch(keyCode(control::ControlKey::CTRL_X));

    ASSERT_EQ(editor.buffers.size(), 1u);
    EXPECT_EQ(editor.currentBufferIndex, 0);
    EXPECT_EQ(editor.currentBuffer->filename, "one.txt");
    EXPECT_STREQ(sm.currentStateName(), "BUFFERS");
}

TEST(RealModeTransitionsTest, BufferBrowserCtrlXLastBufferReturnsWelcome)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->filename = "only.txt";
    editor.currentBuffer->lines = {"only"};

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_W));
    ASSERT_STREQ(sm.currentStateName(), "BUFFERS");

    sm.dispatch(keyCode(control::ControlKey::CTRL_X));

    EXPECT_TRUE(editor.buffers.empty());
    EXPECT_FALSE(editor.hasBuffer());
    EXPECT_EQ(editor.currentMode, WELCOME);
    EXPECT_STREQ(sm.currentStateName(), "WELCOME");
}

TEST(RealModeTransitionsTest, BufferBrowserCtrlShiftXClosesSearchedBuffers)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->filename = "one.cpp";
    editor.currentBuffer->lines = {"one"};
    editor.createNewBuffer();
    editor.currentBuffer->filename = "one.h";
    editor.currentBuffer->lines = {"one header"};
    editor.createNewBuffer();
    editor.currentBuffer->filename = "two.cpp";
    editor.currentBuffer->lines = {"two"};
    editor.switchToBuffer(2);

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_W));
    ASSERT_STREQ(sm.currentStateName(), "BUFFERS");

    sm.dispatch('o');
    sm.dispatch('n');
    sm.dispatch('e');
    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_X));

    ASSERT_EQ(editor.buffers.size(), 1u);
    EXPECT_EQ(editor.currentBufferIndex, 0);
    EXPECT_EQ(editor.currentBuffer->filename, "two.cpp");
    EXPECT_STREQ(sm.currentStateName(), "BUFFERS");
}

TEST(RealModeTransitionsTest, BufferBrowserCtrlShiftXAllMatchesReturnsWelcome)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->filename = "one.cpp";
    editor.currentBuffer->lines = {"one"};
    editor.createNewBuffer();
    editor.currentBuffer->filename = "one.h";
    editor.currentBuffer->lines = {"one header"};

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(keyCode(control::ControlKey::CTRL_W));
    ASSERT_STREQ(sm.currentStateName(), "BUFFERS");

    sm.dispatch('o');
    sm.dispatch('n');
    sm.dispatch('e');
    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_X));

    EXPECT_TRUE(editor.buffers.empty());
    EXPECT_FALSE(editor.hasBuffer());
    EXPECT_EQ(editor.currentMode, WELCOME);
    EXPECT_STREQ(sm.currentStateName(), "WELCOME");
}
