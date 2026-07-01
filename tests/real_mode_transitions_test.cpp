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
    EXPECT_TRUE(editor.splitVertical);
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
    EXPECT_FALSE(editor.splitVertical);
    EXPECT_EQ(editor.activePane, 1);
    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
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

TEST(RealModeTransitionsTest, CtrlHLCyclesVerticalSplitPanes)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});
    editor.enableSplit(true);
    editor.switchPaneDirection(1, 0);
    ASSERT_TRUE(editor.splitActive);
    ASSERT_TRUE(editor.splitVertical);
    ASSERT_EQ(editor.activePane, 1);

    sm.dispatch(keyCode(control::ControlKey::CTRL_H));
    EXPECT_EQ(editor.activePane, 0);

    sm.dispatch(keyCode(control::ControlKey::CTRL_L));
    EXPECT_EQ(editor.activePane, 1);
}

TEST(RealModeTransitionsTest, CtrlJKCyclesHorizontalSplitPanes)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});
    editor.enableSplit(false);
    editor.switchPaneDirection(0, 1);
    ASSERT_TRUE(editor.splitActive);
    ASSERT_FALSE(editor.splitVertical);
    ASSERT_EQ(editor.activePane, 1);

    sm.dispatch(keyCode(control::ControlKey::CTRL_K));
    EXPECT_EQ(editor.activePane, 0);

    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
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

    sm.dispatch(keyCode(control::ControlKey::CTRL_H));
    EXPECT_EQ(editor.activePane, 0);
    EXPECT_EQ(editor.currentBufferIndex, 0);
    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "left");

    sm.dispatch(keyCode(control::ControlKey::CTRL_L));
    EXPECT_EQ(editor.activePane, 1);
    EXPECT_EQ(editor.currentBufferIndex, 1);
    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "right");
}

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
    EXPECT_EQ(editor.currentBufferIndex, 1);
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
