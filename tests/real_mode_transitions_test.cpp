#include "command_palette.h"
#include "real_mode_test_utils.h"
#include "terminal.h"
#include "widgets/command_popup.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>

using namespace uvim_test;

namespace
{
bool contains_command(const std::vector<std::string>& commands,
                      const std::string& expected)
{
    return std::find(commands.begin(), commands.end(), expected) !=
           commands.end();
}

bool contains_help_text(const std::vector<std::string>& lines,
                        std::string_view expected)
{
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line)
                       { return line.find(expected) != std::string::npos; });
}
} // namespace

TEST(RealModeTransitionsTest, WelcomeEscStaysInWelcome)
{
    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, WelcomeMode{});

    sm.dispatch(keyCode(control::ControlKey::ESC));

    EXPECT_STREQ(sm.currentStateName(), "WELCOME");
}

TEST(RealModeTransitionsTest, WelcomeLeaderXOpensFileBrowser)
{
    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, WelcomeMode{});

    sm.dispatch(' ');
    sm.dispatch('x');

    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
}

TEST(RealModeTransitionsTest, CommandPopupIncludesRegisteredExCommands)
{
    Editor editor = Editor::createForTests();

    EXPECT_TRUE(contains_command(editor.getCommandCompletions("pw"), "pwd"));
    EXPECT_TRUE(contains_command(editor.getCommandCompletions("ene"), "enew"));
    EXPECT_TRUE(contains_command(editor.getCommandCompletions("fmt"), "fmt"));
    EXPECT_TRUE(
        contains_command(editor.getCommandCompletions("format"), "format"));
    EXPECT_TRUE(contains_command(editor.getCommandCompletions("bd!"), "bd!"));
    EXPECT_TRUE(
        contains_command(editor.getCommandCompletions("tabn"), "tabnext"));
    EXPECT_TRUE(
        contains_command(editor.getCommandCompletions("tabp"), "tabprev"));
    EXPECT_TRUE(
        contains_command(editor.getCommandCompletions("git a"), "git add"));
    EXPECT_TRUE(contains_command(editor.getCommandCompletions("vs"), "vs"));
    EXPECT_TRUE(contains_command(editor.getCommandCompletions("hs"), "hs"));
    EXPECT_TRUE(
        contains_command(editor.getCommandCompletions("clo"), "close"));
    EXPECT_TRUE(contains_command(editor.getCommandCompletions("se"), "se"));
    EXPECT_TRUE(contains_command(editor.getCommandCompletions("ve"), "ve"));
    EXPECT_FALSE(
        contains_command(editor.getCommandCompletions("se", FILE_BROWSER),
                         "se"));
    EXPECT_FALSE(
        contains_command(editor.getCommandCompletions("ve", FILE_BROWSER),
                         "ve"));
    EXPECT_TRUE(
        contains_command(editor.getCommandCompletions("run", FILE_BROWSER),
                         "run "));
    EXPECT_FALSE(
        contains_command(editor.getCommandCompletions("vsplit"), "vsplit"));
    EXPECT_FALSE(
        contains_command(editor.getCommandCompletions("hsplit"), "hsplit"));
    for(const char* obsolete :
        {"write", "quit", "new", "vnew", "only", "tabclose", "syntax",
         "noh", "nohlsearch"})
    {
        EXPECT_FALSE(contains_command(editor.getCommandCompletions(obsolete),
                                      obsolete));
    }
#ifdef UVIM_ENABLE_RG_CACHE
    EXPECT_TRUE(
        contains_command(editor.getSetCompletions("set rgup"),
                         "set rgupdate=300"));
#endif
}

TEST(RealModeTransitionsTest, EveryCommandCompletionHasPopupDocumentation)
{
    Editor editor = Editor::createForTests();

    for(Mode mode : {NORMAL, FILE_BROWSER})
    {
        for(const auto& command : editor.getCommandCompletions("", mode))
        {
            EXPECT_FALSE(widgets::commandDocumentation(command).empty())
                << "missing command popup documentation for: " << command;
        }
    }
}

TEST(RealModeTransitionsTest, ReopeningFileUsesExistingBufferCache)
{
    const auto root = make_temp_dir("uvim_open_cache_");
    const auto file = root / "alpha.txt";
    write_file(file, "alpha\n");

    Editor editor = Editor::createForTests();
    editor.openFile(file.string());

    ASSERT_EQ(editor.buffers.size(), 1u);
    const std::string canonical = std::filesystem::canonical(file).string();
    ASSERT_EQ(editor.bufferIndexByFilename[canonical], 0);

    editor.openFile((root / "." / "alpha.txt").string());

    EXPECT_EQ(editor.buffers.size(), 1u);
    EXPECT_EQ(editor.currentBufferIndex, 0);
    EXPECT_EQ(editor.currentBuffer->filename, canonical);
}

TEST(RealModeTransitionsTest, OpenFilePromotesColdOpenCache)
{
    const auto root = make_temp_dir("uvim_cold_open_cache_");
    const auto file = root / "cached.txt";
    write_file(file, "first\nsecond\n");

    Editor editor = Editor::createForTests();
    const std::string canonical = std::filesystem::canonical(file).string();

    editor.prewarmColdOpenFile(file.string());

    ASSERT_EQ(editor.buffers.size(), 0u);
    ASSERT_EQ(editor.coldOpenCache.size(), 1u);
    ASSERT_NE(editor.coldOpenCache.find(canonical), editor.coldOpenCache.end());

    editor.openFile((root / "." / "cached.txt").string(), false);

    ASSERT_EQ(editor.buffers.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->filename, canonical);
    ASSERT_EQ(editor.lines->size(), 2u);
    EXPECT_EQ((*editor.lines)[0], "first");
    EXPECT_EQ((*editor.lines)[1], "second");
    EXPECT_TRUE(editor.coldOpenCache.empty());
}

#ifdef UVIM_ENABLE_RG_CACHE
TEST(RealModeTransitionsTest, SetRgUpdateChangesGrepDelay)
{
    Editor editor = Editor::createForTests();

    EXPECT_EQ(editor.rgUpdateMs, 300);
    EXPECT_TRUE(editor.handleSetCommand("set rgupdate=125"));
    EXPECT_EQ(editor.rgUpdateMs, 125);
    EXPECT_TRUE(editor.handleSetCommand("set rgupdate=0"));
    EXPECT_EQ(editor.rgUpdateMs, 0);
    EXPECT_TRUE(editor.handleSetCommand("set rgupdate=6000"));
    EXPECT_EQ(editor.rgUpdateMs, 0);
}
#endif

TEST(RealModeTransitionsTest, SymbolUnderCursorToleratesQualifiedBoundaries)
{
    Editor editor = Editor::createForTests();
    editor.screenRows = 8;
    editor.screenCols = 100;
    editor.createNewBuffer();
    std::string line = "struct AnsiToolsMode : widgets::PopupBase";
    editor.currentBuffer->lines = {line};
    *editor.cursorY = 0;

    *editor.cursorX = static_cast<int>(line.find("::"));
    EXPECT_EQ(editor.getSymbolUnderCursor(), "PopupBase");
    EXPECT_EQ(editor.symbolPrefix, "widgets::");

    *editor.cursorX = static_cast<int>(line.find("PopupBase") + 9);
    EXPECT_EQ(editor.getSymbolUnderCursor(), "PopupBase");
    EXPECT_EQ(editor.symbolPrefix, "widgets::");
}

TEST(RealModeTransitionsTest, CommandPopupDocumentsHsAsHorizontalSplit)
{
    Editor editor = Editor::createForTests();
    std::vector<std::string> entries = {"hs"};
    std::vector<int> filtered = {0};
    widgets::CommandPopupView view{
        widgets::PopupFrameView{editor.theme, 20, 80}, entries, filtered, 0, 0};

    std::string output;
    widgets::drawCommandPopup(output, view);

    EXPECT_TRUE(text_utils::is_found(output.find("Split horizontally")));
    EXPECT_FALSE(text_utils::is_found(output.find("Split vertically")));
    EXPECT_TRUE(text_utils::is_found(output.find("\xE2\x95\xAD")));
    EXPECT_TRUE(text_utils::is_found(output.find("\xE2\x94\x80")));
    EXPECT_TRUE(text_utils::is_found(output.find("\xE2\x95\xAE")));
}

TEST(RealModeTransitionsTest, VeOpensFileBrowserInVerticalSplit)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    set_buffer_filename(editor, "/tmp/uvim_ve_test.txt");
    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "ve");

    EXPECT_TRUE(editor.splitActive);
    EXPECT_TRUE(editor.splitVertical);
    EXPECT_EQ(editor.activePane, 1);
    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
}

TEST(RealModeTransitionsTest, SeOpensFileBrowserInHorizontalSplit)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    set_buffer_filename(editor, "/tmp/uvim_se_test.txt");
    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "se");

    EXPECT_TRUE(editor.splitActive);
    EXPECT_FALSE(editor.splitVertical);
    EXPECT_EQ(editor.activePane, 1);
    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
}

TEST(RealModeTransitionsTest, VsSplitsFileBrowserVertically)
{
    const auto root = make_temp_dir("uvim_browser_vs_split_");
    write_file(root / "alpha.txt", "alpha\n");

    Editor editor = Editor::createForTests();
    auto smPtr = std::make_unique<ModeStateMachine>(
        createModeContext(&editor), FileBrowserMode{root.string()});
    auto& sm = *smPtr;
    editor.setModeStateMachineForTests(std::move(smPtr));

    dispatch_command(sm, "vs");

    EXPECT_TRUE(editor.splitActive);
    EXPECT_TRUE(editor.splitVertical);
    EXPECT_EQ(editor.activePane, 1);
    EXPECT_TRUE(editor.buffers.empty());
    EXPECT_EQ(editor.currentBuffer, nullptr);
    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
}

TEST(RealModeTransitionsTest, VeWithoutNamedBufferOpensBrowserWithoutSplit)
{
    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, WelcomeMode{});

    dispatch_command(sm, "ve");

    EXPECT_FALSE(editor.splitActive);
    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
}

TEST(RealModeTransitionsTest, VsFromStartupBrowserCreatesVerticalSplit)
{
    const auto root = make_temp_dir("uvim_ve_start_browser_");
    write_file(root / "alpha.txt", "alpha\n");

    Editor editor = Editor::createForTests();
    auto smPtr = std::make_unique<ModeStateMachine>(
        createModeContext(&editor), FileBrowserMode{root.string()});
    auto& sm = *smPtr;
    editor.setModeStateMachineForTests(std::move(smPtr));

    dispatch_command(sm, "vs");

    EXPECT_TRUE(editor.splitActive);
    EXPECT_TRUE(editor.splitVertical);
    EXPECT_EQ(editor.activePane, 1);
    EXPECT_TRUE(editor.buffers.empty());
    EXPECT_EQ(editor.currentBuffer, nullptr);
    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
    auto* browser = sm.getState<FileBrowserMode>();
    ASSERT_NE(browser, nullptr);
    EXPECT_EQ(browser->currentDirectory, root.string());
}

TEST(RealModeTransitionsTest, HsFromStartupBrowserCreatesHorizontalSplit)
{
    const auto root = make_temp_dir("uvim_se_start_browser_");
    write_file(root / "alpha.txt", "alpha\n");

    Editor editor = Editor::createForTests();
    auto smPtr = std::make_unique<ModeStateMachine>(
        createModeContext(&editor), FileBrowserMode{root.string()});
    auto& sm = *smPtr;
    editor.setModeStateMachineForTests(std::move(smPtr));

    dispatch_command(sm, "hs");

    EXPECT_TRUE(editor.splitActive);
    EXPECT_FALSE(editor.splitVertical);
    EXPECT_EQ(editor.activePane, 1);
    EXPECT_TRUE(editor.buffers.empty());
    EXPECT_EQ(editor.currentBuffer, nullptr);
    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
    auto* browser = sm.getState<FileBrowserMode>();
    ASSERT_NE(browser, nullptr);
    EXPECT_EQ(browser->currentDirectory, root.string());
}

TEST(RealModeTransitionsTest, BrowserHsDoesNotFallBackToEditorSplitCommand)
{
    const auto root = make_temp_dir("uvim_browser_hs_local_");
    write_file(root / "alpha.txt", "alpha\n");

    Editor editor = Editor::createForTests();
    auto smPtr = std::make_unique<ModeStateMachine>(
        createModeContext(&editor), FileBrowserMode{root.string()});
    auto& sm = *smPtr;
    editor.setModeStateMachineForTests(std::move(smPtr));

    dispatch_command(sm, "hs");

    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
    EXPECT_EQ(editor.currentMode, FILE_BROWSER);
    EXPECT_TRUE(editor.splitActive);
    EXPECT_FALSE(editor.splitVertical);
    EXPECT_TRUE(editor.buffers.empty());
    EXPECT_EQ(editor.currentBuffer, nullptr);
    auto* browser = sm.getState<FileBrowserMode>();
    ASSERT_NE(browser, nullptr);
    EXPECT_TRUE(browser->isBrowserPane(0));
    EXPECT_TRUE(browser->isBrowserPane(1));
}

TEST(RealModeTransitionsTest, BrowserSplitCommandsWorkAfterBrowserIsSplit)
{
    const auto root = make_temp_dir("uvim_ve_se_split_browser_");
    write_file(root / "alpha.txt", "alpha\n");

    Editor editor = Editor::createForTests();
    auto smPtr = std::make_unique<ModeStateMachine>(
        createModeContext(&editor), FileBrowserMode{root.string()});
    auto& sm = *smPtr;
    editor.setModeStateMachineForTests(std::move(smPtr));

    dispatch_command(sm, "vs");
    ASSERT_STREQ(sm.currentStateName(), "BROWSE");
    ASSERT_TRUE(editor.splitActive);
    ASSERT_EQ(editor.splitPanes.size(), 2u);
    auto* splitBrowser = sm.getState<FileBrowserMode>();
    ASSERT_NE(splitBrowser, nullptr);
    ASSERT_TRUE(splitBrowser->isBrowserPane(0));
    ASSERT_TRUE(splitBrowser->isBrowserPane(1));

    editor.drawFullScreen();
    dispatch_command(sm, "hs");

    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
    EXPECT_TRUE(editor.splitActive);
    EXPECT_EQ(editor.splitPanes.size(), 3u);
    EXPECT_EQ(editor.activePane, 2);
    EXPECT_TRUE(editor.buffers.empty());
    EXPECT_EQ(editor.currentBuffer, nullptr);
}

TEST(RealModeTransitionsTest, BrowserSplitDrawsWithoutCreatingBuffer)
{
    const auto root = make_temp_dir("uvim_browser_split_draw_");
    write_file(root / "alpha.txt", "alpha\n");

    Editor editor = Editor::createForTests();
    auto smPtr = std::make_unique<ModeStateMachine>(
        createModeContext(&editor), FileBrowserMode{root.string()});
    auto& sm = *smPtr;
    editor.setModeStateMachineForTests(std::move(smPtr));

    dispatch_command(sm, "vs");
    ASSERT_STREQ(sm.currentStateName(), "BROWSE");
    ASSERT_TRUE(editor.splitActive);
    ASSERT_TRUE(editor.buffers.empty());

    editor.drawFullScreen();

    EXPECT_TRUE(editor.buffers.empty());
    EXPECT_EQ(editor.currentBuffer, nullptr);
    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
}

TEST(RealModeTransitionsTest, BrowserSplitCommandWorksAfterDraw)
{
    const auto root = make_temp_dir("uvim_browser_split_after_draw_");
    write_file(root / "alpha.txt", "alpha\n");

    Editor editor = Editor::createForTests();
    auto smPtr = std::make_unique<ModeStateMachine>(
        createModeContext(&editor), FileBrowserMode{root.string()});
    auto& sm = *smPtr;
    editor.setModeStateMachineForTests(std::move(smPtr));

    dispatch_command(sm, "vs");
    ASSERT_STREQ(sm.currentStateName(), "BROWSE");
    ASSERT_TRUE(editor.splitActive);

    editor.drawFullScreen();
    dispatch_command(sm, "hs");

    EXPECT_TRUE(editor.splitActive);
    EXPECT_EQ(editor.splitPanes.size(), 3u);
    EXPECT_TRUE(editor.buffers.empty());
    EXPECT_EQ(editor.currentBuffer, nullptr);
    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
}

TEST(RealModeTransitionsTest, StartupBrowserSplitPanesKeepSeparateCursors)
{
    const auto root = make_temp_dir("uvim_browser_split_cursors_");
    write_file(root / "alpha.txt", "alpha\n");
    write_file(root / "beta.txt", "beta\n");
    write_file(root / "gamma.txt", "gamma\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    dispatch_command(sm, "vs");
    ASSERT_STREQ(sm.currentStateName(), "BROWSE");
    auto* browser = sm.getState<FileBrowserMode>();
    ASSERT_NE(browser, nullptr);
    ASSERT_EQ(editor.activePane, 1);

    sm.dispatch(keyCode(typed::TypedKey::KEY_J));
    EXPECT_EQ(browser->browserCursor, 1);

    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_H));
    EXPECT_EQ(editor.activePane, 0);
    EXPECT_EQ(browser->browserCursor, 0);

    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_L));
    EXPECT_EQ(editor.activePane, 1);
    EXPECT_EQ(browser->browserCursor, 1);
}

TEST(RealModeTransitionsTest, StartupBrowserSplitPanesKeepSeparateSelections)
{
    const auto root = make_temp_dir("uvim_browser_split_selections_");
    write_file(root / "alpha.txt", "alpha\n");
    write_file(root / "beta.txt", "beta\n");
    write_file(root / "gamma.txt", "gamma\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    dispatch_command(sm, "vs");
    ASSERT_STREQ(sm.currentStateName(), "BROWSE");
    auto* browser = sm.getState<FileBrowserMode>();
    ASSERT_NE(browser, nullptr);
    ASSERT_EQ(editor.activePane, 1);

    sm.dispatch(keyCode(typed::TypedKey::KEY_J));
    sm.dispatch(keyCode(control::ControlKey::SPACE));
    ASSERT_EQ(browser->selectedFiles.size(), 1u);

    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_H));
    EXPECT_EQ(editor.activePane, 0);
    EXPECT_TRUE(browser->selectedFiles.empty());

    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_L));
    EXPECT_EQ(editor.activePane, 1);
    EXPECT_EQ(browser->selectedFiles.size(), 1u);
}

TEST(RealModeTransitionsTest, StartupBrowserSplitPaneScrollsActivePane)
{
    const auto root = make_temp_dir("uvim_browser_split_scroll_");
    for(int i = 0; i < 24; ++i)
        write_file(root / ("file_" + std::to_string(i) + ".txt"), "x\n");

    Editor editor = Editor::createForTests();
    editor.screenRows = 8;
    editor.screenCols = 80;
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    dispatch_command(sm, "vs");
    ASSERT_STREQ(sm.currentStateName(), "BROWSE");
    auto* browser = sm.getState<FileBrowserMode>();
    ASSERT_NE(browser, nullptr);
    ASSERT_EQ(editor.activePane, 1);

    for(int i = 0; i < 12; ++i)
        sm.dispatch(keyCode(typed::TypedKey::KEY_J));

    EXPECT_GT(browser->browserCursor, 0);
    EXPECT_GT(browser->browserOffset, 0);
}

TEST(RealModeTransitionsTest, HsKeepsCursorVisibleInShorterPane)
{
    const auto root = make_temp_dir("uvim_browser_sex_visible_");
    for(int i = 0; i < 24; ++i)
        write_file(root / ("file_" + std::to_string(i) + ".txt"), "x\n");

    Editor editor = Editor::createForTests();
    editor.screenRows = 10;
    editor.screenCols = 80;
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* browser = sm.getState<FileBrowserMode>();
    ASSERT_NE(browser, nullptr);
    browser->browserCursor = 18;
    browser->browserOffset = 0;
    browser->savePaneState(editor.activePane);

    dispatch_command(sm, "hs");
    ASSERT_STREQ(sm.currentStateName(), "BROWSE");
    ASSERT_TRUE(editor.splitActive);
    ASSERT_FALSE(editor.splitVertical);

    const auto layout = editor.getPaneLayout(editor.activePane);
    const int visibleRows = std::max(1, layout.rows - 1);
    EXPECT_GE(browser->browserCursor, browser->browserOffset);
    EXPECT_LT(browser->browserCursor, browser->browserOffset + visibleRows);
}

TEST(RealModeTransitionsTest, HorizontalBrowserPaneSwitchKeepsCursorVisible)
{
    const auto root = make_temp_dir("uvim_browser_horizontal_switch_visible_");
    for(int i = 0; i < 24; ++i)
        write_file(root / ("file_" + std::to_string(i) + ".txt"), "x\n");

    Editor editor = Editor::createForTests();
    editor.screenRows = 10;
    editor.screenCols = 80;
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    dispatch_command(sm, "hs");
    ASSERT_STREQ(sm.currentStateName(), "BROWSE");
    ASSERT_TRUE(editor.splitActive);
    ASSERT_FALSE(editor.splitVertical);
    ASSERT_EQ(editor.activePane, 1);

    auto* browser = sm.getState<FileBrowserMode>();
    ASSERT_NE(browser, nullptr);
    browser->browserCursor = 2;
    browser->browserOffset = 18;
    browser->savePaneState(0);
    browser->browserCursor = 18;
    browser->browserOffset = 18;
    browser->savePaneState(1);

    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_K));

    ASSERT_EQ(editor.activePane, 0);
    const auto layout = editor.getPaneLayout(editor.activePane);
    const int visibleRows = std::max(1, layout.rows - 1);
    EXPECT_GE(browser->browserCursor, browser->browserOffset);
    EXPECT_LT(browser->browserCursor, browser->browserOffset + visibleRows);
}

TEST(RealModeTransitionsTest, FileBrowserOpenSeparatesEditorAndBrowserPanes)
{
    const auto root = make_temp_dir("uvim_browser_editor_panes_separate_");
    write_file(root / "alpha.txt", "alpha\n");
    write_file(root / "beta.txt", "beta\n");

    Editor editor = Editor::createForTests();
    editor.screenRows = 16;
    editor.screenCols = 80;
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    dispatch_command(sm, "vs");
    dispatch_command(sm, "hs");
    ASSERT_STREQ(sm.currentStateName(), "BROWSE");
    ASSERT_TRUE(editor.splitActive);
    ASSERT_GE(editor.splitPanes.size(), 3u);

    auto* browser = sm.getState<FileBrowserMode>();
    ASSERT_NE(browser, nullptr);
    for(int i = 0; i < static_cast<int>(browser->fileList.size()); ++i)
    {
        if(browser->fileList[i].name == "alpha.txt")
        {
            browser->browserCursor = i;
            browser->browserOffset = 0;
            break;
        }
    }

    sm.dispatch(keyCode(control::ControlKey::ENTER));

    ASSERT_STREQ(sm.currentStateName(), "NORMAL");
    EXPECT_FALSE(editor.splitActive);
    ASSERT_TRUE(editor.currentBuffer != nullptr);
    EXPECT_EQ(std::filesystem::weakly_canonical(editor.currentBuffer->filename),
              std::filesystem::weakly_canonical(root / "alpha.txt"));

    dispatch_command(sm, "e " + root.string());

    ASSERT_STREQ(sm.currentStateName(), "BROWSE");
    EXPECT_TRUE(editor.splitActive);
    EXPECT_GE(editor.splitPanes.size(), 3u);
}

TEST(RealModeTransitionsTest, FileBrowserQClosesOnlyActiveSplitPane)
{
    const auto root = make_temp_dir("uvim_browser_q_split_");
    write_file(root / "alpha.txt", "alpha\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    dispatch_command(sm, "vs");
    ASSERT_TRUE(editor.splitActive);
    ASSERT_EQ(editor.activePane, 1);

    sm.dispatch(keyCode(typed::TypedKey::KEY_Q));

    EXPECT_FALSE(editor.splitActive);
    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
    EXPECT_EQ(editor.activePane, 0);
}

TEST(RealModeTransitionsTest, FileBrowserQFinalPaneReturnsWelcomeWithoutBuffer)
{
    const auto root = make_temp_dir("uvim_browser_q_welcome_");
    write_file(root / "alpha.txt", "alpha\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    sm.dispatch(keyCode(typed::TypedKey::KEY_Q));

    EXPECT_STREQ(sm.currentStateName(), "WELCOME");
}

TEST(RealModeTransitionsTest, FileBrowserQFinalPaneReturnsNormalWithBuffer)
{
    const auto root = make_temp_dir("uvim_browser_q_normal_");
    write_file(root / "alpha.txt", "alpha\n");

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    set_buffer_filename(editor, (root / "alpha.txt").string());
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    sm.dispatch(keyCode(typed::TypedKey::KEY_Q));

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, FileBrowserCapitalQClosesAllSplitPanesToWelcome)
{
    const auto root = make_temp_dir("uvim_browser_cap_q_");
    write_file(root / "alpha.txt", "alpha\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    dispatch_command(sm, "vs");
    ASSERT_TRUE(editor.splitActive);
    dispatch_command(sm, "vs");
    ASSERT_TRUE(editor.splitActive);

    sm.dispatch(keyCode(typed::TypedKey::KEY_CAP_Q));

    EXPECT_FALSE(editor.splitActive);
    EXPECT_STREQ(sm.currentStateName(), "WELCOME");
}

TEST(RealModeTransitionsTest, FileBrowserCapitalQSavesBrowserPaneLayout)
{
    const auto root = make_temp_dir("uvim_browser_cap_q_save_panes_");
    write_file(root / "alpha.txt", "alpha\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    dispatch_command(sm, "vs");
    ASSERT_TRUE(editor.splitActive);
    ASSERT_EQ(editor.splitPanes.size(), 2u);

    sm.dispatch(keyCode(typed::TypedKey::KEY_CAP_Q));

    EXPECT_FALSE(editor.splitActive);
    EXPECT_STREQ(sm.currentStateName(), "WELCOME");

    auto sm2 = makeMachine(editor, FileBrowserMode{root.string()});

    EXPECT_STREQ(sm2.currentStateName(), "BROWSE");
    EXPECT_TRUE(editor.splitActive);
    EXPECT_EQ(editor.splitPanes.size(), 2u);
}

TEST(RealModeTransitionsTest, FileBrowserQSavesRemovedPaneLayout)
{
    const auto root = make_temp_dir("uvim_browser_q_save_removed_pane_");
    write_file(root / "alpha.txt", "alpha\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    dispatch_command(sm, "vs");
    ASSERT_TRUE(editor.splitActive);

    sm.dispatch(keyCode(typed::TypedKey::KEY_Q));

    EXPECT_FALSE(editor.splitActive);
    EXPECT_STREQ(sm.currentStateName(), "BROWSE");

    sm.dispatch(keyCode(typed::TypedKey::KEY_CAP_Q));
    EXPECT_STREQ(sm.currentStateName(), "WELCOME");

    auto sm2 = makeMachine(editor, FileBrowserMode{root.string()});

    EXPECT_STREQ(sm2.currentStateName(), "BROWSE");
    EXPECT_FALSE(editor.splitActive);
}

TEST(RealModeTransitionsTest, FileBrowserCapitalQClosesAllSplitPanesToNormal)
{
    const auto root = make_temp_dir("uvim_browser_cap_q_normal_");
    write_file(root / "alpha.txt", "alpha\n");

    Editor editor = Editor::createForTests();
    editor.openFile((root / "alpha.txt").string());
    auto sm = makeMachine(editor, FileBrowserMode{root.string(),
                                                  (root / "alpha.txt").string()});

    dispatch_command(sm, "vs");
    ASSERT_TRUE(editor.splitActive);
    dispatch_command(sm, "vs");
    ASSERT_TRUE(editor.splitActive);

    sm.dispatch(keyCode(typed::TypedKey::KEY_CAP_Q));

    EXPECT_FALSE(editor.splitActive);
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, BareHelpOpensIndexAndFuzzySearchJumpsToRow)
{
    Editor editor = Editor::createForTests();
    editor.screenRows = 8;
    editor.screenCols = 100;
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "help");

    ASSERT_STREQ(sm.currentStateName(), "HELP");
    auto* help = sm.getState<HelpMode>();
    ASSERT_NE(help, nullptr);
    EXPECT_TRUE(help->topic.empty());
    ASSERT_FALSE(help->lines.empty());
    EXPECT_EQ(help->lines.front(), "# uvim Help");
    ASSERT_GE(help->selectedLine, 0);
    ASSERT_LT(help->selectedLine, (int)help->lines.size());
    EXPECT_TRUE(text_utils::is_found(help->lines[help->selectedLine].find(
        "`:help`")));

    sm.dispatch(keyCode(typed::TypedKey::KEY_J));
    help = sm.getState<HelpMode>();
    ASSERT_NE(help, nullptr);
    ASSERT_GE(help->selectedLine, 0);
    ASSERT_LT(help->selectedLine, (int)help->lines.size());
    EXPECT_TRUE(text_utils::is_found(help->lines[help->selectedLine].find(
        "`:help commands`")));

    sm.dispatch(keyCode(control::ControlKey::ENTER));
    help = sm.getState<HelpMode>();
    ASSERT_NE(help, nullptr);
    EXPECT_EQ(help->topic, "commands");

    dispatch_command(sm, "help");
    help = sm.getState<HelpMode>();
    ASSERT_NE(help, nullptr);
    EXPECT_TRUE(help->topic.empty());

    sm.dispatch(keyCode(command::CommandKey::KEY_SLASH));
    help = sm.getState<HelpMode>();
    ASSERT_NE(help, nullptr);
    EXPECT_TRUE(help->searchActive);

    for(int i = 0; i < 8; ++i)
        sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    help = sm.getState<HelpMode>();
    ASSERT_NE(help, nullptr);
    EXPECT_GT(help->searchCursor, 0);
    EXPECT_GT(help->searchOffset, 0);

    sm.dispatch(keyCode(control::ControlKey::CTRL_K));
    help = sm.getState<HelpMode>();
    ASSERT_NE(help, nullptr);
    EXPECT_GT(help->searchCursor, 0);

    for(char c : std::string("key"))
        sm.dispatch(c);

    help = sm.getState<HelpMode>();
    ASSERT_NE(help, nullptr);
    EXPECT_EQ(help->searchQuery, "key");

    while(!help->searchQuery.empty())
    {
        sm.dispatch(keyCode(control::ControlKey::BACKSPACE));
        help = sm.getState<HelpMode>();
        ASSERT_NE(help, nullptr);
    }

    for(char c : std::string("git stage"))
        sm.dispatch(c);

    help = sm.getState<HelpMode>();
    ASSERT_NE(help, nullptr);
    ASSERT_FALSE(help->searchMatches.empty());
    EXPECT_TRUE(std::any_of(help->searchMatches.begin(),
                            help->searchMatches.end(),
                            [](const HelpMode::HelpSearchMatch& match)
                            { return !match.topicOnly; }));

    sm.dispatch(keyCode(control::ControlKey::CTRL_I));
    help = sm.getState<HelpMode>();
    ASSERT_NE(help, nullptr);
    EXPECT_FALSE(help->searchDocumentation);
    EXPECT_TRUE(std::none_of(help->searchMatches.begin(),
                             help->searchMatches.end(),
                             [](const HelpMode::HelpSearchMatch& match)
                             { return !match.topicOnly; }));

    sm.dispatch(keyCode(control::ControlKey::CTRL_I));
    help = sm.getState<HelpMode>();
    ASSERT_NE(help, nullptr);
    EXPECT_TRUE(help->searchDocumentation);

    sm.dispatch(keyCode(control::ControlKey::ENTER));

    help = sm.getState<HelpMode>();
    ASSERT_NE(help, nullptr);
    EXPECT_FALSE(help->searchActive);
    EXPECT_FALSE(help->lines.empty());
    EXPECT_GE(help->scrollOffset, 0);
    EXPECT_TRUE(help->jumpHighlight);
    EXPECT_GE(help->selectedLine, help->scrollOffset);
    const int jumpedLine = help->selectedLine;
    sm.dispatch(keyCode(typed::TypedKey::KEY_J));
    help = sm.getState<HelpMode>();
    ASSERT_NE(help, nullptr);
    EXPECT_EQ(help->selectedLine, jumpedLine + 1);
    sm.dispatch(keyCode(typed::TypedKey::KEY_K));
    help = sm.getState<HelpMode>();
    ASSERT_NE(help, nullptr);
    EXPECT_EQ(help->selectedLine, jumpedLine);

    bool visibleMatch = false;
    const int last =
        std::min((int)help->lines.size(), help->scrollOffset + 8);
    for(int i = help->scrollOffset; i < last; ++i)
    {
        if(text_utils::is_found(help->lines[i].find("git stage")) ||
           text_utils::is_found(help->lines[i].find("Git stage")) ||
           text_utils::is_found(help->lines[i].find(":git stage")))
        {
            visibleMatch = true;
            break;
        }
    }
    EXPECT_TRUE(visibleMatch);
}

TEST(RealModeTransitionsTest, HelpCommandPopupSelectsPlainHelpFirst)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(':');
    for(char c : std::string("help"))
        sm.dispatch(c);

    ASSERT_TRUE(editor.isCommandPopupActive());
    auto selection = editor.commandPopupSelection();
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(*selection, "help");

    editor.moveCommandPopupCursor(1);
    selection = editor.commandPopupSelection();
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(*selection, "help commands");
    editor.moveCommandPopupCursor(-1);

    sm.dispatch(keyCode(control::ControlKey::ENTER));

    ASSERT_STREQ(sm.currentStateName(), "HELP");
    auto* help = sm.getState<HelpMode>();
    ASSERT_NE(help, nullptr);
    EXPECT_TRUE(help->topic.empty());
}

TEST(RealModeTransitionsTest, TopicHelpStartsAtTopAndBrowsesRows)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "h commands");

    ASSERT_STREQ(sm.currentStateName(), "HELP");
    auto* help = sm.getState<HelpMode>();
    ASSERT_NE(help, nullptr);
    EXPECT_EQ(help->topic, "commands");
    EXPECT_EQ(help->selectedLine, 0);
    ASSERT_FALSE(help->lines.empty());
    EXPECT_EQ(help->lines.front(), "# Command Reference");

    sm.dispatch(keyCode(typed::TypedKey::KEY_J));
    help = sm.getState<HelpMode>();
    ASSERT_NE(help, nullptr);
    EXPECT_EQ(help->selectedLine, 1);

    sm.dispatch(keyCode(typed::TypedKey::KEY_K));
    help = sm.getState<HelpMode>();
    ASSERT_NE(help, nullptr);
    EXPECT_EQ(help->selectedLine, 0);
}

TEST(RealModeTransitionsTest, HelpCommandReferenceMatchesCurrentCommands)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "h commands");
    auto* commands = sm.getState<HelpMode>();
    ASSERT_NE(commands, nullptr);

    EXPECT_FALSE(contains_help_text(commands->lines, ":Ex"));
    EXPECT_FALSE(contains_help_text(commands->lines, ":Explore"));
    EXPECT_FALSE(contains_help_text(commands->lines, ":only"));
    EXPECT_FALSE(contains_help_text(commands->lines, ":tabc"));
    EXPECT_FALSE(contains_help_text(commands->lines, ":tabclose"));
    EXPECT_TRUE(contains_help_text(commands->lines, ":e ."));
    EXPECT_TRUE(contains_help_text(commands->lines, ":edit!%"));
    EXPECT_TRUE(contains_help_text(commands->lines, ":qw!"));
    EXPECT_TRUE(contains_help_text(commands->lines, ":se"));
    EXPECT_TRUE(contains_help_text(commands->lines, ":clo"));
    EXPECT_TRUE(contains_help_text(commands->lines, ":format"));
    EXPECT_TRUE(contains_help_text(commands->lines, ":lspinfo"));
    EXPECT_TRUE(contains_help_text(commands->lines, ":toolinfo"));
    EXPECT_TRUE(contains_help_text(commands->lines, ":git stash pop"));
    EXPECT_TRUE(contains_help_text(commands->lines, ":set"));
    EXPECT_TRUE(contains_help_text(commands->lines, ":mv"));
    EXPECT_TRUE(contains_help_text(commands->lines, ":new <name>"));
    EXPECT_TRUE(contains_help_text(commands->lines, ":run <command>"));

    dispatch_command(sm, "h files");
    auto* files = sm.getState<HelpMode>();
    ASSERT_NE(files, nullptr);
    EXPECT_FALSE(contains_help_text(files->lines, ":Ex"));
    EXPECT_FALSE(contains_help_text(files->lines, ":Explore"));
    EXPECT_TRUE(contains_help_text(files->lines, ":e ."));

    dispatch_command(sm, "h filebrowser");
    auto* filebrowser = sm.getState<HelpMode>();
    ASSERT_NE(filebrowser, nullptr);
    EXPECT_FALSE(contains_help_text(filebrowser->lines, ":Ex"));
    EXPECT_FALSE(contains_help_text(filebrowser->lines, ":Explore"));
    EXPECT_TRUE(contains_help_text(filebrowser->lines, ":vs"));
}

TEST(RealModeTransitionsTest, HelpModeRunsEditorCommandsAndKeepsHelpCommands)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "help");
    ASSERT_STREQ(sm.currentStateName(), "HELP");

    dispatch_command(sm, "loctotal");
    // :loctotal counts in the background; apply it like the main loop does.
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while(!editor.tasks.idle() && std::chrono::steady_clock::now() < deadline)
    {
        editor.tasks.pollCompletions();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    editor.tasks.pollCompletions();
    EXPECT_TRUE(text_utils::is_found(editor.locMessage.find("LOC total ")));
    EXPECT_TRUE(editor.statusMessage.empty());
    EXPECT_STREQ(sm.currentStateName(), "HELP");

    dispatch_command(sm, "h commands");
    ASSERT_STREQ(sm.currentStateName(), "HELP");
    auto* help = sm.getState<HelpMode>();
    ASSERT_NE(help, nullptr);
    EXPECT_EQ(help->topic, "commands");
}

TEST(RealModeTransitionsTest, HelpToFileBrowserEscWithoutFileReturnsWelcome)
{
    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, WelcomeMode{});

    dispatch_command(sm, "help");
    ASSERT_STREQ(sm.currentStateName(), "HELP");

    dispatch_command(sm, "e .");
    ASSERT_STREQ(sm.currentStateName(), "BROWSE");

    sm.dispatch(keyCode(control::ControlKey::ESC));
    EXPECT_STREQ(sm.currentStateName(), "WELCOME");
}

TEST(RealModeTransitionsTest, HelpToFileBrowserQWithoutFileReturnsWelcome)
{
    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, WelcomeMode{});

    dispatch_command(sm, "help");
    ASSERT_STREQ(sm.currentStateName(), "HELP");

    dispatch_command(sm, "e .");
    ASSERT_STREQ(sm.currentStateName(), "BROWSE");

    sm.dispatch(keyCode(typed::TypedKey::KEY_Q));
    EXPECT_STREQ(sm.currentStateName(), "WELCOME");
}

TEST(RealModeTransitionsTest, CtrlAOpensGrepSearchFromVerticalSplit)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});
    editor.enableSplit(true);

    sm.dispatch(keyCode(control::ControlKey::CTRL_A));

    EXPECT_TRUE(editor.splitActive);
    EXPECT_STREQ(sm.currentStateName(), "GREP");
}

TEST(RealModeTransitionsTest, CtrlAOpensGrepSearchFromHorizontalSplit)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});
    editor.enableSplit(false);

    sm.dispatch(keyCode(control::ControlKey::CTRL_A));

    EXPECT_TRUE(editor.splitActive);
    EXPECT_STREQ(sm.currentStateName(), "GREP");
}

#ifdef UVIM_ENABLE_MODERN_KEYBINDINGS
#ifdef UVIM_ENABLE_MULTI_PANE_SPLITS
TEST(RealModeTransitionsTest, ShiftCtrlHLCyclesVerticalSplitPanes)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});
    editor.enableSplit(true);
    editor.switchPaneDirection(1, 0);
    ASSERT_TRUE(editor.splitActive);
    ASSERT_TRUE(editor.splitVertical);
    ASSERT_EQ(editor.activePane, 1);

    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_H));
    EXPECT_EQ(editor.activePane, 0);

    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_L));
    EXPECT_EQ(editor.activePane, 1);
}

TEST(RealModeTransitionsTest, ShiftCtrlJKCyclesHorizontalSplitPanes)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});
    editor.enableSplit(false);
    editor.switchPaneDirection(0, 1);
    ASSERT_TRUE(editor.splitActive);
    ASSERT_FALSE(editor.splitVertical);
    ASSERT_EQ(editor.activePane, 1);

    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_K));
    EXPECT_EQ(editor.activePane, 0);

    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_J));
    EXPECT_EQ(editor.activePane, 1);
}

TEST(RealModeTransitionsTest, ShiftCtrlKeysNavigateFileBrowserSplitPanes)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    set_buffer_filename(editor, "/tmp/uvim_browser_split_test.txt");
    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "ve");
    ASSERT_STREQ(sm.currentStateName(), "BROWSE");
    ASSERT_TRUE(editor.splitActive);
    ASSERT_TRUE(editor.splitVertical);
    ASSERT_EQ(editor.activePane, 1);

    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_H));
    EXPECT_EQ(editor.activePane, 0);

    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_L));
    EXPECT_EQ(editor.activePane, 1);
}

TEST(RealModeTransitionsTest, SplitPanesKeepSeparateSelectedBuffers)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"left"};
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"right"};
    editor.switchToBuffer(0);
    editor.enableSplit(true);
    editor.switchPaneDirection(1, 0);
    editor.switchToBufferInActivePane(1);
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_H));
    EXPECT_EQ(editor.activePane, 0);
    EXPECT_EQ(editor.currentBufferIndex, 0);
    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "left");

    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_L));
    EXPECT_EQ(editor.activePane, 1);
    EXPECT_EQ(editor.currentBufferIndex, 1);
    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "right");
}
#endif

#ifdef UVIM_ENABLE_MULTI_PANE_SPLITS
TEST(RealModeTransitionsTest, CtrlHLNavigateBuffersWithoutSplit)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.createNewBuffer();
    ASSERT_EQ(editor.currentBufferIndex, 1);
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::CTRL_H));
    EXPECT_EQ(editor.currentBufferIndex, 0);

    sm.dispatch(keyCode(control::ControlKey::CTRL_L));
    EXPECT_EQ(editor.currentBufferIndex, 1);
}
#endif
#endif

TEST(RealModeTransitionsTest, VsOpensVerticalEditorSplit)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "vs");

    EXPECT_TRUE(editor.splitActive);
    EXPECT_TRUE(editor.splitVertical);
    EXPECT_EQ(editor.splitPanes.size(), 2u);
    EXPECT_GT(editor.getPaneLayout(1).x, editor.getPaneLayout(0).x);
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, HsOpensHorizontalEditorSplit)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "hs");

    EXPECT_TRUE(editor.splitActive);
    EXPECT_FALSE(editor.splitVertical);
    EXPECT_EQ(editor.splitPanes.size(), 2u);
    EXPECT_GT(editor.getPaneLayout(1).y, editor.getPaneLayout(0).y);
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

#ifdef UVIM_ENABLE_MULTI_PANE_SPLITS
TEST(RealModeTransitionsTest, SplittingActivePaneCreatesNestedLayout)
{
    Editor editor = Editor::createForTests();
    editor.screenRows = 24;
    editor.screenCols = 80;
    editor.createNewBuffer();

    editor.enableSplit(true);
    ASSERT_TRUE(editor.splitActive);
    ASSERT_EQ(editor.splitPanes.size(), 2u);
    ASSERT_EQ(editor.activePane, 1);

    editor.switchPaneDirection(-1, 0);
    ASSERT_EQ(editor.activePane, 0);
    editor.enableSplit(false);

    ASSERT_EQ(editor.splitPanes.size(), 3u);
    EXPECT_EQ(editor.activePane, 2);

    Editor::PaneLayout topLeft = editor.getPaneLayout(0);
    Editor::PaneLayout right = editor.getPaneLayout(1);
    Editor::PaneLayout bottomLeft = editor.getPaneLayout(2);

    EXPECT_EQ(topLeft.x, 0);
    EXPECT_EQ(bottomLeft.x, 0);
    EXPECT_GT(bottomLeft.y, topLeft.y);
    EXPECT_GT(right.x, topLeft.x);
    EXPECT_EQ(right.y, 0);
    EXPECT_EQ(right.rows, editor.screenRows);
}

TEST(RealModeTransitionsTest, DirectionalPaneJumpUsesNestedLayout)
{
    Editor editor = Editor::createForTests();
    editor.screenRows = 24;
    editor.screenCols = 80;
    editor.createNewBuffer();

    editor.enableSplit(true);
    editor.switchPaneDirection(-1, 0);
    editor.enableSplit(false);
    ASSERT_EQ(editor.activePane, 2);

    editor.switchPaneDirection(0, -1);
    EXPECT_EQ(editor.activePane, 0);

    editor.switchPaneDirection(1, 0);
    EXPECT_EQ(editor.activePane, 1);

    editor.switchPaneDirection(-1, 0);
    EXPECT_EQ(editor.activePane, 0);
}

TEST(RealModeTransitionsTest, LeaderWcClosesOnlyActiveNestedPane)
{
    Editor editor = Editor::createForTests();
    editor.screenRows = 24;
    editor.screenCols = 80;
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    editor.enableSplit(true);
    editor.switchPaneDirection(-1, 0);
    editor.enableSplit(false);
    ASSERT_TRUE(editor.splitActive);
    ASSERT_EQ(editor.splitPanes.size(), 3u);
    ASSERT_EQ(editor.activePane, 2);

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_W));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));

    EXPECT_TRUE(editor.splitActive);
    EXPECT_EQ(editor.splitPanes.size(), 2u);
    EXPECT_EQ(editor.activePane, 0);
    EXPECT_EQ(editor.getPaneLayout(0).x, 0);
    EXPECT_GT(editor.getPaneLayout(1).x, editor.getPaneLayout(0).x);
}
#else
TEST(RealModeTransitionsTest, SplitDoesNotCreateNestedLayoutWhenDisabled)
{
    Editor editor = Editor::createForTests();
    editor.screenRows = 24;
    editor.screenCols = 80;
    editor.createNewBuffer();

    editor.enableSplit(true);
    ASSERT_TRUE(editor.splitActive);
    ASSERT_EQ(editor.splitPanes.size(), 2u);
    ASSERT_EQ(editor.activePane, 1);

    editor.enableSplit(false);

    EXPECT_TRUE(editor.splitActive);
    EXPECT_EQ(editor.splitPanes.size(), 2u);
    EXPECT_EQ(editor.activePane, 1);
    EXPECT_GT(editor.getPaneLayout(1).y, editor.getPaneLayout(0).y);
}

TEST(RealModeTransitionsTest, CtrlHLNavigateBuffersWhenMultiPaneSplitsDisabled)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.createNewBuffer();
    editor.createNewBuffer();
    ASSERT_EQ(editor.currentBufferIndex, 2);
    auto sm = makeMachine(editor, NormalMode{});

    editor.enableSplit(true);
    ASSERT_TRUE(editor.splitActive);

    sm.dispatch(keyCode(control::ControlKey::CTRL_H));
    EXPECT_EQ(editor.currentBufferIndex, 1);

    sm.dispatch(keyCode(control::ControlKey::CTRL_L));
    EXPECT_EQ(editor.currentBufferIndex, 2);
}
#endif

TEST(RealModeTransitionsTest, LeaderXOpensFileBrowserAtCurrentFile)
{
    const auto root = make_temp_dir("uvim_leader_x_current_");
    write_file(root / "alpha.txt", "alpha\n");
    write_file(root / "target.txt", "target\n");
    write_file(root / "zeta.txt", "zeta\n");

    Editor editor = Editor::createForTests();
    editor.openFile((root / "target.txt").string());
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_X));

    ASSERT_STREQ(sm.currentStateName(), "BROWSE");
    auto* fb = sm.getState<FileBrowserMode>();
    ASSERT_NE(fb, nullptr);
    ASSERT_GE(fb->browserCursor, 0);
    ASSERT_LT(fb->browserCursor, static_cast<int>(fb->fileList.size()));
    EXPECT_EQ(fb->fileList[fb->browserCursor].name, "target.txt");
}

TEST(RealModeTransitionsTest, LeaderXXOpensFileBrowserAtDirectoryTop)
{
    const auto root = make_temp_dir("uvim_leader_xx_top_");
    write_file(root / "alpha.txt", "alpha\n");
    write_file(root / "target.txt", "target\n");

    Editor editor = Editor::createForTests();
    editor.openFile((root / "target.txt").string());
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_X));
    ASSERT_STREQ(sm.currentStateName(), "BROWSE");
    sm.dispatch(keyCode(typed::TypedKey::KEY_X));

    ASSERT_STREQ(sm.currentStateName(), "BROWSE");
    auto* fb = sm.getState<FileBrowserMode>();
    ASSERT_NE(fb, nullptr);
    ASSERT_FALSE(fb->fileList.empty());
    EXPECT_EQ(fb->browserCursor, 0);
}

TEST(RealModeTransitionsTest, LspInfoQuitWithNoBufferReturnsWelcome)
{
    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, LspInfoMode{});

    sm.dispatch('q');

    EXPECT_STREQ(sm.currentStateName(), "WELCOME");
}

TEST(RealModeTransitionsTest, ToolInfoCommandOpensViewAndQuitReturnsWelcome)
{
    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, WelcomeMode{});

    dispatch_command(sm, "toolinfo");

    EXPECT_STREQ(sm.currentStateName(), "TOOL INFO");
    EXPECT_FALSE(editor.toolInfoLines.empty());
    EXPECT_TRUE(contains_help_text(editor.toolInfoLines, "fzf:"));
    EXPECT_TRUE(contains_help_text(editor.toolInfoLines, "rg/ripgrep:"));
#ifdef UVIM_ENABLE_FORMATTERS
    EXPECT_TRUE(contains_help_text(editor.toolInfoLines, "clang-format:"));
    EXPECT_TRUE(contains_help_text(editor.toolInfoLines, "mlang-format:"));
    EXPECT_TRUE(
        contains_help_text(editor.toolInfoLines, "Python formatter (ruff):"));
    EXPECT_TRUE(contains_help_text(editor.toolInfoLines, "robocop (Robot):"));
    EXPECT_TRUE(
        contains_help_text(editor.toolInfoLines, "python (JSON/YAML):"));
#else
    EXPECT_TRUE(contains_help_text(editor.toolInfoLines,
                                   "formatters: not compiled"));
#endif

    sm.dispatch('q');

    EXPECT_STREQ(sm.currentStateName(), "WELCOME");
}

TEST(RealModeTransitionsTest, DoubleEscClearsStatusMessageInWelcome)
{
    Editor editor = Editor::createForTests();
    editor.setStatusMessage("Message");
    auto sm = makeMachine(editor, WelcomeMode{});

    sm.dispatch(keyCode(control::ControlKey::ESC));
    EXPECT_EQ(editor.statusMessage, "Message");

    sm.dispatch(keyCode(control::ControlKey::ESC));
    EXPECT_TRUE(editor.statusMessage.empty());
}

TEST(RealModeTransitionsTest, ColonBdClosesCurrentBuffer)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.createNewBuffer();
    ASSERT_EQ(editor.buffers.size(), 2u);
    ASSERT_EQ(editor.currentBufferIndex, 1);

    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "bd");

    EXPECT_EQ(editor.buffers.size(), 1u);
    EXPECT_EQ(editor.currentBufferIndex, 0);
    EXPECT_TRUE(editor.commandBuffer.empty());
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, BMovesBackwardImmediatelyAndDoesNotSwallowNextKey)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"one two three"};
    *editor.cursorY = 0;
    *editor.cursorX = 8;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch('b');
    EXPECT_EQ(*editor.cursorX, 4);
    EXPECT_TRUE(editor.commandBuffer.empty());
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");

    sm.dispatch('x');
    EXPECT_EQ(editor.currentBuffer->lines[0], "one wo three");
    EXPECT_EQ(*editor.cursorX, 4);
}

TEST(RealModeTransitionsTest, RepeatedAndCountedBApplyEveryWordMotion)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"one two three four"};
    *editor.cursorX = 14;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch('b');
    EXPECT_EQ(*editor.cursorX, 8);
    sm.dispatch('b');
    EXPECT_EQ(*editor.cursorX, 4);
    EXPECT_EQ(*editor.wantedX, 4);

    *editor.cursorX = 14;
    sm.dispatch('2');
    sm.dispatch('b');
    EXPECT_EQ(*editor.cursorX, 4);
    EXPECT_EQ(editor.repeatCount, 0);
    EXPECT_TRUE(editor.commandBuffer.empty());
    sm.dispatch('b');
    EXPECT_EQ(*editor.cursorX, 0);
}

TEST(RealModeTransitionsTest, BDoesNotTurnFollowingDeleteIntoBufferCommand)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"one two three"};
    *editor.cursorX = 8;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch('b');
    sm.dispatch('d');

    EXPECT_EQ(editor.buffers.size(), 2u);
    EXPECT_EQ(editor.currentBufferIndex, 1);
    EXPECT_EQ(*editor.cursorX, 4);
    EXPECT_STREQ(sm.currentStateName(), "OP_PENDING");
}

TEST(RealModeTransitionsTest, NormalNavigationKeysDoNotOverlapWithFollowingInput)
{
    struct MotionCase
    {
        const char* label;
        std::vector<int> keys;
        int startY, startX, endY, endX;
    };
    const MotionCase cases[] = {
        {"h", {'h'}, 1, 7, 1, 6},
        {"j", {'j'}, 0, 4, 1, 4},
        {"k", {'k'}, 1, 7, 0, 7},
        {"l", {'l'}, 1, 7, 1, 8},
        {"w", {'w'}, 1, 6, 1, 7},
        {"W", {'W'}, 1, 2, 1, 7},
        {"b", {'b'}, 1, 7, 1, 2},
        {"B", {'B'}, 1, 7, 1, 2},
        {"e", {'e'}, 1, 7, 1, 10},
        {"E", {'E'}, 1, 7, 1, 10},
        {"0", {'0'}, 1, 7, 1, 0},
        {"^", {'^'}, 1, 7, 1, 2},
        {"$", {'$'}, 1, 7, 1, 14},
        {"G", {'G'}, 0, 0, 4, 0},
        {"{", {'{'}, 1, 7, 0, 0},
        {"}", {'}'}, 1, 7, 3, 0},
        {"left", {keyCode(navigation::NavigationKey::ARROW_LEFT)}, 1, 7, 1, 6},
        {"down", {keyCode(navigation::NavigationKey::ARROW_DOWN)}, 0, 4, 1, 4},
        {"up", {keyCode(navigation::NavigationKey::ARROW_UP)}, 1, 7, 0, 7},
        {"right", {keyCode(navigation::NavigationKey::ARROW_RIGHT)}, 1, 7, 1, 8},
        {"2h", {'2', 'h'}, 1, 7, 1, 5},
        {"2l", {'2', 'l'}, 1, 7, 1, 9},
        {"2j", {'2', 'j'}, 0, 4, 2, 0},
        {"2k", {'2', 'k'}, 4, 7, 2, 0},
        {"2b", {'2', 'b'}, 1, 12, 1, 2},
        {"2B", {'2', 'B'}, 1, 12, 1, 2},
        {"2e", {'2', 'e'}, 1, 2, 1, 10},
        {"2E", {'2', 'E'}, 1, 2, 1, 10},
        {"3G", {'3', 'G'}, 0, 0, 2, 0},
    };
    for(const auto& motion : cases)
    {
        SCOPED_TRACE(motion.label);
        Editor editor = Editor::createForTests();
        editor.createNewBuffer();
        *editor.lines = {"  one two three", "  four five six", "",
                         "  seven eight nine", "  ten eleven twelve"};
        *editor.cursorY = motion.startY;
        *editor.cursorX = *editor.wantedX = motion.startX;
        auto sm = makeMachine(editor, NormalMode{});
        for(int key : motion.keys)
            sm.dispatch(key);
        EXPECT_EQ(*editor.cursorY, motion.endY);
        EXPECT_EQ(*editor.cursorX, motion.endX);
        EXPECT_TRUE(editor.commandBuffer.empty());
        EXPECT_EQ(editor.repeatCount, 0);
        EXPECT_STREQ(sm.currentStateName(), "NORMAL");
        sm.dispatch('i');
        EXPECT_STREQ(sm.currentStateName(), "INSERT");
    }
}

TEST(RealModeTransitionsTest, ScreenAndScrollNavigationDoNotConsumeFollowingInput)
{
    struct MotionCase { int key, endY; };
    const MotionCase cases[] = {
        {'H', 10}, {'M', 19}, {'L', 28},
        {keyCode(control::ControlKey::CTRL_D), 30},
        {keyCode(control::ControlKey::CTRL_U), 10},
        {keyCode(control::ControlKey::CTRL_F), 39},
        {keyCode(control::ControlKey::CTRL_B), 1},
        {keyCode(navigation::NavigationKey::PAGE_DOWN), 39},
        {keyCode(navigation::NavigationKey::PAGE_UP), 1},
    };
    for(const auto& motion : cases)
    {
        SCOPED_TRACE(motion.key);
        Editor editor = Editor::createForTests();
        editor.createNewBuffer();
        *editor.lines = std::vector<std::string>(60, "  word more");
        editor.screenRows = 21;
        editor.showTabs = false;
        *editor.cursorY = 20;
        *editor.cursorX = *editor.wantedX = 4;
        *editor.offsetY = 10;
        auto sm = makeMachine(editor, NormalMode{});
        sm.dispatch(motion.key);
        EXPECT_EQ(*editor.cursorY, motion.endY);
        EXPECT_TRUE(editor.commandBuffer.empty());
        EXPECT_STREQ(sm.currentStateName(), "NORMAL");
        sm.dispatch('i');
        EXPECT_STREQ(sm.currentStateName(), "INSERT");
    }
}

TEST(RealModeTransitionsTest, VerticalNavigationKeepsColumnsAcrossUtf8AndShortLines)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    *editor.lines = {"abcdefgh", "aéz", "abcdefgh", "abé", "", "abcdefgh"};
    editor.utf8Mode = true;
    *editor.cursorX = *editor.wantedX = 2;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch('j');
    EXPECT_EQ(*editor.cursorX, 1); // byte 2 is inside é, so use its start
    EXPECT_EQ(*editor.wantedX, 2);
    sm.dispatch('j');
    EXPECT_EQ(*editor.cursorX, 2); // restore the wanted column on ASCII
    sm.dispatch('k');
    EXPECT_EQ(*editor.cursorX, 1);
    sm.dispatch('k');
    EXPECT_EQ(*editor.cursorX, 2);

    *editor.cursorX = *editor.wantedX = 5;
    sm.dispatch('3');
    sm.dispatch('j');
    EXPECT_EQ(*editor.cursorY, 3);
    EXPECT_EQ(*editor.cursorX, 2); // last whole character on a shorter line
    sm.dispatch('j');
    EXPECT_EQ(*editor.cursorX, 0); // empty line
    sm.dispatch('j');
    EXPECT_EQ(*editor.cursorX, 5);
    EXPECT_EQ(*editor.wantedX, 5);
}

TEST(RealModeTransitionsTest, CharacterSearchConsumesOnlyItsTargetKey)
{
    struct MotionCase { char key; int startX, endX; };
    for(const auto& motion : {MotionCase{'f', 0, 2}, MotionCase{'F', 6, 2},
                              MotionCase{'t', 0, 1}, MotionCase{'T', 6, 3}})
    {
        SCOPED_TRACE(motion.key);
        Editor editor = Editor::createForTests();
        editor.createNewBuffer();
        *editor.lines = {"abc abc"};
        *editor.cursorX = motion.startX;
        auto sm = makeMachine(editor, NormalMode{});
        const int queued = Terminal::bufferedKeyCount();
        Terminal::unreadKey('i');
        Terminal::unreadKey('c');
        sm.dispatch(motion.key);
        EXPECT_EQ(*editor.cursorX, motion.endX);
        EXPECT_EQ(Terminal::bufferedKeyCount(), queued + 1);
        const int next = Terminal::readKeyTimeout(0);
        EXPECT_EQ(next, 'i');
        sm.dispatch(next);
        EXPECT_STREQ(sm.currentStateName(), "INSERT");
    }
}

TEST(RealModeTransitionsTest, WMovesForwardImmediately)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"one two three"};
    *editor.cursorX = 0;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch('w');

    EXPECT_EQ(*editor.cursorX, 3);
    EXPECT_TRUE(editor.commandBuffer.empty());
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, LeaderWcClosesSelectedPane)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.createNewBuffer();
    editor.switchToBuffer(0);
    editor.enableSplit(true);
    editor.switchPaneDirection(1, 0);
    editor.switchToBufferInActivePane(1);
    ASSERT_TRUE(editor.splitActive);
    ASSERT_EQ(editor.activePane, 1);
    ASSERT_EQ(editor.currentBufferIndex, 1);
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_W));
    sm.dispatch(keyCode(typed::TypedKey::KEY_C));

    EXPECT_FALSE(editor.splitActive);
    EXPECT_EQ(editor.activePane, 0);
    EXPECT_EQ(editor.currentBufferIndex, 0);
    EXPECT_TRUE(editor.commandBuffer.empty());
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, CloseCommandClosesSelectedPane)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.enableSplit(false);
    ASSERT_TRUE(editor.splitActive);
    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "close");

    EXPECT_FALSE(editor.splitActive);
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, MarkJumpReopensMarkedFile)
{
    auto dir = make_temp_dir("uvim_marks_");
    auto first = dir / "first.txt";
    auto second = dir / "second.txt";
    write_file(first, "one\ntwo\nthree\n");
    write_file(second, "alpha\nbeta\n");

    Editor editor = Editor::createForTests();
    editor.openFile(first.string());
    *editor.cursorY = 2;
    *editor.cursorX = 3;
    editor.setMark('a');

    editor.openFile(second.string());
    ASSERT_EQ(std::filesystem::canonical(second).string(), *editor.filename);

    editor.jumpToMark('a');

    EXPECT_EQ(std::filesystem::canonical(first).string(), *editor.filename);
    EXPECT_EQ(*editor.cursorY, 2);
    EXPECT_EQ(*editor.cursorX, 3);
}

TEST(RealModeTransitionsTest, NormalCtrlJAndCtrlKMoveCurrentLineWithUndoRedo)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"one", "two", "three"};
    set_buffer_filename(editor, "/tmp/uvim_move_line_test.cpp");
    *editor.cursorY = 1;
    *editor.cursorX = 1;
    editor.saveState();
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_EQ(editor.currentBuffer->lines,
              (std::vector<std::string>{"one", "three", "two"}));
    EXPECT_EQ(*editor.cursorY, 2);

    sm.dispatch(keyCode(typed::TypedKey::KEY_U));
    EXPECT_EQ(editor.currentBuffer->lines,
              (std::vector<std::string>{"one", "two", "three"}));
    EXPECT_EQ(*editor.cursorY, 1);

    sm.dispatch(keyCode(control::ControlKey::CTRL_R));
    EXPECT_EQ(editor.currentBuffer->lines,
              (std::vector<std::string>{"one", "three", "two"}));
    EXPECT_EQ(*editor.cursorY, 2);

    sm.dispatch(keyCode(control::ControlKey::CTRL_K));
    EXPECT_EQ(editor.currentBuffer->lines,
              (std::vector<std::string>{"one", "two", "three"}));
    EXPECT_EQ(*editor.cursorY, 1);
}

TEST(RealModeTransitionsTest, VisualLineCtrlJAndCtrlKMoveSelection)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"a", "b", "c", "d", "e"};
    set_buffer_filename(editor, "/tmp/uvim_move_visual_line_test.cpp");
    *editor.cursorY = 1;
    *editor.cursorX = 0;
    editor.saveState();
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_CAP_V));
    sm.dispatch(keyCode(typed::TypedKey::KEY_J));
    ASSERT_STREQ(sm.currentStateName(), "VISUAL LINE");
    ASSERT_EQ(editor.currentBuffer->visualStartY, 1);
    ASSERT_EQ(editor.currentBuffer->visualEndY, 2);

    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_EQ(editor.currentBuffer->lines,
              (std::vector<std::string>{"a", "d", "b", "c", "e"}));
    EXPECT_EQ(editor.currentBuffer->visualStartY, 2);
    EXPECT_EQ(editor.currentBuffer->visualEndY, 3);
    EXPECT_EQ(*editor.cursorY, 3);
    EXPECT_STREQ(sm.currentStateName(), "VISUAL LINE");

    sm.dispatch(keyCode(control::ControlKey::CTRL_K));
    EXPECT_EQ(editor.currentBuffer->lines,
              (std::vector<std::string>{"a", "b", "c", "d", "e"}));
    EXPECT_EQ(editor.currentBuffer->visualStartY, 1);
    EXPECT_EQ(editor.currentBuffer->visualEndY, 2);
    EXPECT_EQ(*editor.cursorY, 2);
    EXPECT_STREQ(sm.currentStateName(), "VISUAL LINE");
}

TEST(RealModeTransitionsTest, VisualLineMoveCanUndoAndRedo)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"a", "b", "c", "d"};
    set_buffer_filename(editor, "/tmp/uvim_move_visual_line_undo_test.cpp");
    *editor.cursorY = 1;
    *editor.cursorX = 0;
    editor.saveState();
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(typed::TypedKey::KEY_CAP_V));
    sm.dispatch(keyCode(typed::TypedKey::KEY_J));
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_EQ(editor.currentBuffer->lines,
              (std::vector<std::string>{"a", "d", "b", "c"}));

    sm.dispatch(keyCode(control::ControlKey::ESC));
    sm.dispatch(keyCode(typed::TypedKey::KEY_U));
    EXPECT_EQ(editor.currentBuffer->lines,
              (std::vector<std::string>{"a", "b", "c", "d"}));

    sm.dispatch(keyCode(control::ControlKey::CTRL_R));
    EXPECT_EQ(editor.currentBuffer->lines,
              (std::vector<std::string>{"a", "d", "b", "c"}));
}

#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
TEST(RealModeTransitionsTest, EnterRunsTypedCommandWhenItIsComplete)
{
    // ":q" with "q!" highlighted used to force quit. Same shape without
    // exiting the test: ":ls" with "lspinfo" highlighted must run :ls.
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(':');
    sm.dispatch('l');
    sm.dispatch('s');
    ASSERT_TRUE(editor.commandPopupActive);
    int lspinfo = -1;
    for(int i = 0; i < (int)editor.commandPopupFiltered.size(); ++i)
    {
        if(editor.commandPopupAll[editor.commandPopupFiltered[i]] == "lspinfo")
            lspinfo = i;
    }
    ASSERT_GE(lspinfo, 0);
    editor.commandPopupCursor = lspinfo;

    sm.dispatch(keyCode(control::ControlKey::ENTER));
    ASSERT_FALSE(editor.commandHistory.empty());
    EXPECT_EQ(editor.commandHistory.back(), "ls");
}
#endif

namespace
{
// Set while a test expects that nothing exits the process.
bool exitIsAFailure = false;

void failUnexpectedExit()
{
    if(exitIsAFailure)
    {
        std::fputs("FAILURE: the editor exited (std::exit) during a test\n",
                   stderr);
        std::_Exit(1);
    }
}
} // namespace

TEST(RealModeTransitionsTest, QuitRefusesWithUnsavedChanges)
{
    // A wrong :q std::exit()s this test binary; make that a failure instead
    // of a silent exit 0.
    static const bool registered = std::atexit(failUnexpectedExit) == 0;
    ASSERT_TRUE(registered);
    exitIsAFailure = true;
    auto root = make_temp_dir("uvim_quit_dirty_");
    write_file(root / "sample.txt", "hello");
    Editor editor = Editor::createForTests();
    editor.openFile((root / "sample.txt").string());
    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('i');
    sm.dispatch('x');
    sm.dispatch(keyCode(control::ControlKey::ESC));
    ASSERT_TRUE(editor.currentBuffer->dirty);

    for(const std::string command : {"q", "qa", "qall"})
    {
        sm.dispatch(':');
        for(char c : command)
            sm.dispatch(c);
        sm.dispatch(keyCode(control::ControlKey::ENTER));
        EXPECT_STREQ(sm.currentStateName(), "NORMAL") << command;
        EXPECT_TRUE(text_utils::is_found(editor.statusMessage.find("unsaved")) ||
                    text_utils::is_found(
                        editor.statusMessage.find("No write since")))
            << command << ": " << editor.statusMessage;
    }
    exitIsAFailure = false;
}

TEST(RealModeTransitionsTest, SetIsKittyTogglesAltChords)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});
    auto run = [&](const std::string& command)
    {
        sm.dispatch(':');
        for(char c : command)
            sm.dispatch(c);
        sm.dispatch(keyCode(control::ControlKey::ENTER));
    };

    run("set iskitty=false");
    EXPECT_FALSE(editor.isKitty);
    EXPECT_TRUE(Terminal::altChords());
    EXPECT_STREQ(Terminal::shiftChordLabel(), "Alt");
    run("set iskitty?");
    EXPECT_TRUE(text_utils::is_found(editor.statusMessage.find("iskitty=false")))
        << editor.statusMessage;

    run("set iskitty=true");
    EXPECT_TRUE(editor.isKitty);
    EXPECT_FALSE(Terminal::altChords());
    EXPECT_STREQ(Terminal::shiftChordLabel(), "Ctrl+Shift");

    run("set iskitty=maybe");
    EXPECT_TRUE(editor.isKitty);
    EXPECT_TRUE(
        text_utils::is_found(editor.statusMessage.find("expected true/false")));
}

#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
namespace
{
std::vector<std::string> paletteTitles(
    const std::vector<command_palette::Entry>& entries, const std::string& query)
{
    std::vector<std::string> out;
    for(const auto& match : command_palette::rank(entries, query))
        out.push_back(entries[match.entry].title);
    return out;
}

// Opens the palette, types `query` and presses Enter on the best match.
void runPalette(ModeStateMachine& sm, Editor& editor, const std::string& query)
{
    sm.dispatch(' ');
    sm.dispatch(' ');
    ASSERT_NE(sm.getState<CommandPaletteMode>(), nullptr);
    // The app keeps currentMode in sync with the state machine; commands
    // run from the palette must not see it and reopen the palette.
    editor.currentMode = COMMAND_PALETTE;
    for(char c : query)
        sm.dispatch(c);
    ASSERT_FALSE(sm.getState<CommandPaletteMode>()->matches.empty()) << query;
    sm.dispatch(keyCode(control::ControlKey::ENTER));
}
} // namespace

TEST(CommandPaletteTest, ActionsAndExCommandsRankByMeaning)
{
    auto entries = command_palette::builtinActions();
    command_palette::addExCommands(
        entries, {"w", "wa", "qa", "qall", "pwd", "tabn", "tabnext"},
        [](std::string_view command) -> std::string
        {
            if(command == "qa" || command == "qall")
                return "Quit all windows";
            if(command == "tabn" || command == "tabnext")
                return "Next tab";
            return "";
        });

    // Ex commands an action already covers are not repeated; the rest are
    // grouped by description, shortcut listing the spellings.
    const auto quitAll = std::find_if(entries.begin(), entries.end(),
                                      [](const auto& entry)
                                      { return entry.title == "Quit all windows"; });
    ASSERT_NE(quitAll, entries.end());
    EXPECT_EQ(quitAll->shortcut, ":qa, :qall");
    EXPECT_EQ(std::count_if(entries.begin(), entries.end(),
                            [](const auto& entry)
                            { return entry.title == "Show working directory"; }),
              1);

    EXPECT_EQ(paletteTitles(entries, "file br").front(), "Open file browser");
    EXPECT_EQ(paletteTitles(entries, "change buf").front(), "Change buffer");
    EXPECT_EQ(paletteTitles(entries, "wa").front(), "Save all files");
    EXPECT_EQ(paletteTitles(entries, "next tab").front(), "Next tab");
    EXPECT_EQ(paletteTitles(entries, "git log").front(), "Git: log");
    EXPECT_TRUE(paletteTitles(entries, "zzzzqq").empty());
    EXPECT_EQ(paletteTitles(entries, "").size(), entries.size());
}

TEST(CommandPaletteTest, ActionsRunTheirKeysAndCommands)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    {
        auto sm = makeMachine(editor, NormalMode{});
        runPalette(sm, editor, "change buffer"); // Ctrl-W
        EXPECT_STREQ(sm.currentStateName(), "BUFFERS");
    }
    {
        auto sm = makeMachine(editor, NormalMode{});
        runPalette(sm, editor, "open file browser"); // Space x
        EXPECT_STREQ(sm.currentStateName(), "BROWSE");
    }
    {
        auto sm = makeMachine(editor, NormalMode{});
        runPalette(sm, editor, "find references"); // Space g r reads its next key
        EXPECT_EQ(Terminal::bufferedKeyCount(), 0) << "replayed keys all used";
    }
    {
        auto sm = makeMachine(editor, NormalMode{});
        runPalette(sm, editor, "working directory"); // :pwd
        ASSERT_FALSE(editor.commandHistory.empty());
        EXPECT_EQ(editor.commandHistory.back(), "pwd");
        EXPECT_STREQ(sm.currentStateName(), "NORMAL") << "palette closed";
    }
    {
        auto sm = makeMachine(editor, NormalMode{});
        runPalette(sm, editor, "split vert"); // :vs
        EXPECT_TRUE(editor.splitActive);
        EXPECT_STREQ(sm.currentStateName(), "NORMAL") << "palette closed";
    }
}

TEST(CommandPaletteTest, CommandWithArgumentOpensCommandLine)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(':');
    for(char c : std::string("palette"))
        sm.dispatch(c);
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    ASSERT_NE(sm.getState<CommandPaletteMode>(), nullptr);

    for(char c : std::string("open file"))
        sm.dispatch(c);
    auto* palette = sm.getState<CommandPaletteMode>();
    ASSERT_NE(palette, nullptr);
    ASSERT_FALSE(palette->matches.empty());
    EXPECT_TRUE(palette->entries[palette->matches[0].entry].takesArgument);

    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_STREQ(sm.currentStateName(), "COMMAND");
    EXPECT_EQ(editor.commandBuffer, ":e ");
}

TEST(CommandPaletteTest, NavigationAndLeaving)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(' ');
    sm.dispatch(' ');
    auto* palette = sm.getState<CommandPaletteMode>();
    ASSERT_NE(palette, nullptr);
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    sm.dispatch(keyCode(control::ControlKey::CTRL_K));
    EXPECT_EQ(sm.getState<CommandPaletteMode>()->cursor, 1);

    sm.dispatch(keyCode(control::ControlKey::ESC));
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");

    sm.dispatch(' ');
    sm.dispatch(' ');
    sm.dispatch('x');
    sm.dispatch(keyCode(control::ControlKey::BACKSPACE));
    ASSERT_NE(sm.getState<CommandPaletteMode>(), nullptr);
    sm.dispatch(keyCode(control::ControlKey::BACKSPACE));
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}
#endif

#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
TEST(CommandPaletteTest, SpaceSpaceOpensPaletteFromVisualModes)
{
    for(char visualKey : {'v', 'V', static_cast<char>(keyCode(control::ControlKey::CTRL_V))})
    {
        Editor editor = Editor::createForTests();
        editor.createNewBuffer();
        auto sm = makeMachine(editor, NormalMode{});
        sm.dispatch(visualKey);
        ASSERT_STRNE(sm.currentStateName(), "NORMAL") << int(visualKey);
        sm.dispatch(' ');
        sm.dispatch(' ');
        EXPECT_NE(sm.getState<CommandPaletteMode>(), nullptr) << int(visualKey);
    }
}
#endif

TEST(RealModeTransitionsTest, TabTakesHighlightedCommandPopupItem)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});
    auto type = [&](const std::string& text)
    {
        for(char c : text)
            sm.dispatch(c);
    };

    // A fuzzy match: Tab puts it on the command line, Enter is unchanged.
    sm.dispatch(':');
    type("lcto");
    ASSERT_TRUE(editor.commandPopupActive);
    ASSERT_TRUE(editor.commandPopupSelection().has_value());
    EXPECT_EQ(*editor.commandPopupSelection(), "loctotal");
    sm.dispatch(keyCode(control::ControlKey::TAB));
    EXPECT_EQ(editor.commandBuffer, ":loctotal");
    sm.dispatch(keyCode(control::ControlKey::ESC));

    // Picked with Ctrl-J first (the prompt already shows it): Tab keeps it
    // instead of completing an argument.
    sm.dispatch(':');
    type("lcto");
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    const std::string picked = editor.commandBuffer;
    sm.dispatch(keyCode(control::ControlKey::TAB));
    EXPECT_EQ(editor.commandBuffer, picked);
    sm.dispatch(keyCode(control::ControlKey::ESC));

#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
    sm.dispatch(':');
    type("tsks");
    sm.dispatch(keyCode(control::ControlKey::TAB));
    EXPECT_EQ(editor.commandBuffer, ":tasks");
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    ASSERT_FALSE(editor.commandHistory.empty());
    EXPECT_EQ(editor.commandHistory.back(), "tasks");
    sm.dispatch(keyCode(control::ControlKey::ESC));
#endif

    // A command taking an argument keeps its space: continue typing.
    auto sm2 = makeMachine(editor, NormalMode{});
    sm2.dispatch(':');
    for(char c : std::string("edi"))
        sm2.dispatch(c);
    sm2.dispatch(keyCode(control::ControlKey::TAB));
    EXPECT_EQ(editor.commandBuffer, ":edit ");
}

TEST(RealModeTransitionsTest, TabTakesHighlightedItemInWelcomeCommandPrompt)
{
    // The welcome screen, file browser and help use CommandPrompt, a
    // separate command line implementation.
    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, WelcomeMode{});
    sm.dispatch(':');
    auto prompt = editor.commandPrompt;
    ASSERT_TRUE(prompt && prompt->isActive());
    for(char c : std::string("lcto"))
        sm.dispatch(c);
    ASSERT_TRUE(editor.commandPopupSelection().has_value());
    sm.dispatch(keyCode(control::ControlKey::TAB));
    EXPECT_EQ(prompt->getInput(), *editor.commandPopupSelection());
    EXPECT_EQ(prompt->getInput(), "loctotal");
}
