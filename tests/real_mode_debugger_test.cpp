#include "json_utils.h"
#include "real_mode_test_utils.h"
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
using namespace uvim_test;

TEST(RealModeTransitionsTest, DebuggerCommandsSelectRunAndAttach)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});
    dispatch_command(
        sm, "dbgrun --backend gdb './app with spaces' -- arg1 'two words'");
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    EXPECT_FALSE(debug->attach);
    EXPECT_EQ(debug->backend, "gdb");
    EXPECT_EQ(
        debug->target,
        EditorPathUtilities::resolveEditorPath("./app with spaces").string());
    EXPECT_EQ(debug->programArguments,
              (std::vector<std::string>{"arg1", "two words"}));
    EXPECT_EQ(modeForState(sm.state()), DEBUGGER);
    EXPECT_FALSE(debug->started);
    sm.dispatch('q');
    dispatch_command(sm, "dbgattach 1234");
    debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    EXPECT_TRUE(debug->attach);
    EXPECT_EQ(debug->target, "1234");
}

TEST(RealModeTransitionsTest, DebuggerRejectsInvalidLaunchRequests)
{
    for(const auto& command :
        {"dbgattach 0", "dbgattach -3", "dbgattach 12x",
         "dbgattach 99999999999999999999999", "dbgattach 3 extra", "dbgrun",
         "dbgrun --backend unknown app", "dbgrun 'unclosed"})
    {
        Editor editor = Editor::createForTests();
        editor.createNewBuffer();
        auto sm = makeMachine(editor, NormalMode{});
        dispatch_command(sm, command);
        auto* debug = sm.getState<DebuggerMode>();
        ASSERT_NE(debug, nullptr) << command;
        EXPECT_EQ(debug->state, "error") << command;
        EXPECT_FALSE(debug->output.empty());
    }
}

TEST(RealModeTransitionsTest, DebuggerStoppedPanesAndPromptNavigation)
{
    auto dir = make_temp_dir("uvim_debugger_");
    auto file = dir / "debug source.cpp";
    write_file(file,
               "int main() {\n    int count = 42;\n    return count;\n}\n");
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, DebuggerMode{"app", false});
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    std::string quoted;
    json_utils::append_escaped(quoted, file.string());
    debug->applyMessage("{\"state\":\"stopped\",\"path\":" + quoted +
                        ",\"line\":2,\"variables\":[\"count = 42\",\"other = "
                        "3\"],\"threads\":[{\"id\":\"1\",\"label\":\"main\"},{"
                        "\"id\":\"2\",\"label\":\"worker\"}]}");
    EXPECT_EQ(debug->source.size(), 4u);
    EXPECT_EQ(debug->executionLine, 2);
    EXPECT_EQ(debug->cursor[0], 1);
    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_L));
    EXPECT_EQ(debug->pane, 2);
    sm.dispatch('j');
    EXPECT_EQ(debug->cursor[2], 1);
    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_J));
    EXPECT_EQ(debug->pane, 3);
    sm.dispatch('j');
    EXPECT_EQ(debug->cursor[3], 1);
    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_K));
    EXPECT_EQ(debug->pane, 2);
    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_H));
    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_J));
    EXPECT_EQ(debug->pane, 1);
    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_H));
    EXPECT_EQ(debug->pane, 1);
    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_L));
    EXPECT_EQ(debug->pane, 3);
    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_L));
    EXPECT_EQ(debug->pane, 3);
    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_K));
    EXPECT_EQ(debug->pane, 2);
    sm.dispatch(':');
    sm.dispatch('j');
    EXPECT_EQ(debug->prompt, "j");
    EXPECT_EQ(debug->cursor[2], 1);
    sm.dispatch(27);
    EXPECT_FALSE(debug->promptActive);
    EXPECT_NE(sm.getState<DebuggerMode>(), nullptr);
    debug->applyMessage(
        "{\"state\":\"running\",\"variables\":[],\"threads\":[]}");
    EXPECT_EQ(debug->pane, 0);
    EXPECT_TRUE(debug->variables.empty());
    sm.dispatch('q');
    EXPECT_EQ(sm.getState<DebuggerMode>(), nullptr);
    std::filesystem::remove_all(dir);
}

TEST(RealModeTransitionsTest,
     DebuggerBreakpointCompletionPreservesLineAndDoesNotExecute)
{
    auto dir = make_temp_dir("uvim_debugger_completion_");
    write_file(dir / "unique.cpp", "int value;\n");
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, DebuggerMode{"app", false});
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    sm.dispatch(':');
    for(char c : "b " + (dir / "uni").string() + ":42")
        sm.dispatch(c);
    sm.dispatch('\t');
    EXPECT_EQ(debug->prompt, "b " + (dir / "unique.cpp").string() + ":42");
    EXPECT_TRUE(debug->promptActive);
    EXPECT_FALSE(debug->started);
    sm.dispatch(27);
    EXPECT_NE(sm.getState<DebuggerMode>(), nullptr);
    std::filesystem::remove_all(dir);
}

TEST(RealModeTransitionsTest, DebuggerDrawsSyntaxAndFocusedCompactPane)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.screenRows = 20;
    editor.screenCols = 120;
    auto sm = makeMachine(editor, DebuggerMode{"app", false});
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    debug->sourcePath = "test.cpp";
    debug->source = {"int main() { return 42; }"};
    debug->state = "stopped";
    debug->variables = {"count: int = 42"};
    debug->threads = {"main-thread"};
    debug->output = {"hello"};
    testing::internal::CaptureStdout();
    debug->draw(editor);
    const auto full = testing::internal::GetCapturedStdout();
    EXPECT_NE(full.find("Variables"), std::string::npos);
    EXPECT_NE(full.find("Threads"), std::string::npos);
    EXPECT_NE(full.find("count: int = 42"), std::string::npos);
    EXPECT_NE(full.find("main-thread"), std::string::npos);
    EXPECT_NE(full.find("hello"), std::string::npos);
    ASSERT_FALSE(debug->highlight.tokens.empty());
    EXPECT_FALSE(debug->highlight.tokens[0].empty());
    editor.screenCols = 30;
    debug->pane = 3;
    testing::internal::CaptureStdout();
    debug->draw(editor);
    const auto compact = testing::internal::GetCapturedStdout();
    EXPECT_NE(compact.find("main-thread"), std::string::npos);
    EXPECT_EQ(compact.find("count: int = 42"), std::string::npos);
}
#endif
