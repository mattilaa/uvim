#include "real_mode_test_utils.h"
#include "widgets/command_popup.h"

#include <algorithm>

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
    EXPECT_TRUE(contains_command(editor.getCommandCompletions("Hex"), "Hex"));
    EXPECT_TRUE(
        contains_command(editor.getCommandCompletions("Hex"), "Hexplore"));
}

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

TEST(RealModeTransitionsTest, CommandPopupDocumentsVhAsHorizontalSplit)
{
    Editor editor = Editor::createForTests();
    std::vector<std::string> entries = {"vh"};
    std::vector<int> filtered = {0};
    widgets::CommandPopupView view{
        widgets::PopupFrameView{editor.theme, 20, 80}, entries, filtered, 0, 0};

    std::string output;
    widgets::drawCommandPopup(output, view);

    EXPECT_TRUE(text_utils::is_found(output.find("Split horizontally")));
    EXPECT_FALSE(text_utils::is_found(output.find("Split vertically")));
}

TEST(RealModeTransitionsTest, VexOpensFileBrowserInVerticalSplit)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "Vex");

    EXPECT_TRUE(editor.splitActive);
    EXPECT_FALSE(editor.splitVertical);
    EXPECT_EQ(editor.activePane, 1);
    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
}

TEST(RealModeTransitionsTest, HexOpensFileBrowserInHorizontalSplit)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "Hex");

    EXPECT_TRUE(editor.splitActive);
    EXPECT_TRUE(editor.splitVertical);
    EXPECT_EQ(editor.activePane, 1);
    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
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
    EXPECT_TRUE(contains_help_text(commands->lines, ":Sex"));
    EXPECT_TRUE(contains_help_text(commands->lines, ":format"));
    EXPECT_TRUE(contains_help_text(commands->lines, ":lspinfo"));
    EXPECT_TRUE(contains_help_text(commands->lines, ":git stash pop"));

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
    EXPECT_TRUE(contains_help_text(filebrowser->lines, ":Vex"));
}

TEST(RealModeTransitionsTest, HelpModeRunsEditorCommandsAndKeepsHelpCommands)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "help");
    ASSERT_STREQ(sm.currentStateName(), "HELP");

    dispatch_command(sm, "loctotal");
    EXPECT_TRUE(text_utils::is_found(editor.locMessage.find("LOC total ")));
    EXPECT_TRUE(editor.statusMessage.empty());
    EXPECT_STREQ(sm.currentStateName(), "HELP");

    dispatch_command(sm, "h commands");
    ASSERT_STREQ(sm.currentStateName(), "HELP");
    auto* help = sm.getState<HelpMode>();
    ASSERT_NE(help, nullptr);
    EXPECT_EQ(help->topic, "commands");
}

TEST(RealModeTransitionsTest, CtrlSOpensGrepSearchFromVerticalSplit)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});
    editor.enableSplit(true);

    sm.dispatch(keyCode(control::ControlKey::CTRL_S));

    EXPECT_TRUE(editor.splitActive);
    EXPECT_STREQ(sm.currentStateName(), "GREP");
}

TEST(RealModeTransitionsTest, CtrlSOpensGrepSearchFromHorizontalSplit)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});
    editor.enableSplit(false);

    sm.dispatch(keyCode(control::ControlKey::CTRL_S));

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

TEST(RealModeTransitionsTest, LeaderHLNavigateBuffers)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.createNewBuffer();
    editor.createNewBuffer();
    ASSERT_EQ(editor.currentBufferIndex, 2);
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_H));
    EXPECT_EQ(editor.currentBufferIndex, 1);

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    sm.dispatch(keyCode(typed::TypedKey::KEY_L));
    EXPECT_EQ(editor.currentBufferIndex, 2);
}

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

TEST(RealModeTransitionsTest, LeaderHsOpensHorizontalSplit)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    Terminal::unreadKey(keyCode(typed::TypedKey::KEY_S));
    sm.dispatch(keyCode(typed::TypedKey::KEY_H));

    EXPECT_TRUE(editor.splitActive);
    EXPECT_TRUE(editor.splitVertical);
    EXPECT_EQ(editor.splitPanes.size(), 2u);
    EXPECT_GT(editor.getPaneLayout(1).x, editor.getPaneLayout(0).x);
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, LeaderVsOpensVerticalSplit)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    Terminal::unreadKey(keyCode(typed::TypedKey::KEY_S));
    sm.dispatch(keyCode(typed::TypedKey::KEY_V));

    EXPECT_TRUE(editor.splitActive);
    EXPECT_FALSE(editor.splitVertical);
    EXPECT_EQ(editor.splitPanes.size(), 2u);
    EXPECT_GT(editor.getPaneLayout(1).y, editor.getPaneLayout(0).y);
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}
#endif

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

TEST(RealModeTransitionsTest, WxClosesOnlyActiveNestedPane)
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

    sm.dispatch(keyCode(typed::TypedKey::KEY_W));
    sm.dispatch(keyCode(typed::TypedKey::KEY_X));

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
    Terminal::unreadKey(keyCode(typed::TypedKey::KEY_X));
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

TEST(RealModeTransitionsTest, BdClosesCurrentBuffer)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.createNewBuffer();
    ASSERT_EQ(editor.buffers.size(), 2u);
    ASSERT_EQ(editor.currentBufferIndex, 1);

    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch('b');
    sm.dispatch('d');

    EXPECT_EQ(editor.buffers.size(), 1u);
    EXPECT_EQ(editor.currentBufferIndex, 0);
    EXPECT_TRUE(editor.commandBuffer.empty());
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, BThenOtherKeyKeepsWordBackwardMotion)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"one two three"};
    *editor.cursorY = 0;
    *editor.cursorX = 8;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch('b');
    sm.dispatch('x');

    EXPECT_EQ(*editor.cursorX, 4);
    EXPECT_TRUE(editor.commandBuffer.empty());
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, WxClosesSelectedPane)
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

    sm.dispatch('w');
    sm.dispatch('x');

    EXPECT_FALSE(editor.splitActive);
    EXPECT_EQ(editor.activePane, 0);
    EXPECT_EQ(editor.currentBufferIndex, 0);
    EXPECT_TRUE(editor.commandBuffer.empty());
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
