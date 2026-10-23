#include "ascii.h"
#include "json_utils.h"
#include "real_mode_test_utils.h"
#include <thread>
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
    EXPECT_EQ(debug->pane, 2);
    EXPECT_FALSE(debug->variables.empty());
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
    debug->frameIndex = 4;
    debug->frameFunction = "main";
    debug->stopFunction = "sleep";
    debug->source = {"int main() { return 42; }"};
    debug->state = "stopped";
    debug->variables = {"count: int = 42"};
    debug->threads = {"main-thread"};
    debug->output = {"hello"};
    testing::internal::CaptureStdout();
    debug->draw(editor);
    const auto full = testing::internal::GetCapturedStdout();
    EXPECT_NE(full.find(ascii::utf8(ascii::BOX_ROUNDED_TOP_LEFT)),
              std::string::npos);
    EXPECT_NE(full.find(ascii::utf8(ascii::BOX_ROUNDED_BOTTOM_RIGHT)),
              std::string::npos);
    EXPECT_NE(full.find(ascii::utf8(ascii::BOX_LIGHT_VERTICAL)),
              std::string::npos);
    EXPECT_NE(full.find(ascii::utf8(ascii::BOX_LIGHT_HORIZONTAL)),
              std::string::npos);
    EXPECT_NE(full.find("n next (finish pending call)"), std::string::npos);
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

TEST(RealModeTransitionsTest, DebuggerScrollsEachPaneUsingItsOwnHeight)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.screenRows = 32;
    editor.screenCols = 120;
    auto sm = makeMachine(editor, DebuggerMode{"app", false});
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    debug->state = "stopped";
    debug->sourcePath = "test.cpp";
    debug->executionLine = 1;
    for(int line = 0; line < 100; ++line)
    {
        debug->source.push_back("int value = 42;");
        debug->variables.push_back("value = 42");
        debug->threads.push_back("thread " + std::to_string(line));
    }
    for(int i = 0; i < 10; ++i)
        sm.dispatch('j');
    EXPECT_EQ(debug->cursor[0], 10);
    EXPECT_EQ(debug->offset[0], 0);
    testing::internal::CaptureStdout();
    debug->draw(editor);
    testing::internal::GetCapturedStdout();
    EXPECT_EQ(debug->offset[0], 0);
    debug->applyMessage(
        "{\"state\":\"stopped\",\"path\":\"test.cpp\",\"line\":1}");
    EXPECT_EQ(debug->cursor[0], 10);
    EXPECT_EQ(debug->offset[0], 0);
    sm.dispatch('v');
    EXPECT_EQ(debug->pane, 2);
    for(int i = 0; i < 10; ++i)
        sm.dispatch('j');
    EXPECT_EQ(debug->cursor[2], 10);
    EXPECT_EQ(debug->offset[2], 0);
    sm.dispatch('t');
    EXPECT_EQ(debug->pane, 3);
    for(int i = 0; i < 10; ++i)
        sm.dispatch('j');
    EXPECT_EQ(debug->cursor[3], 10);
    EXPECT_EQ(debug->offset[3], 3);
    sm.dispatch(keyCode(control::ControlKey::TAB));
    EXPECT_EQ(debug->pane, 0);
    sm.dispatch(keyCode(control::ControlKey::SHIFT_TAB));
    EXPECT_EQ(debug->pane, 3);
    sm.dispatch('k');
    EXPECT_EQ(debug->cursor[3], 9);
}

TEST(RealModeTransitionsTest, DebuggerKeepsLastStopAndViewportWhileStepping)
{
    auto dir = make_temp_dir("uvim_debugger_stable_");
    auto file = dir / "demo.cpp";
    std::string code;
    for(int row = 0; row < 70; ++row)
        code += "int value = 42;\n";
    write_file(file, code);
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.screenRows = 32;
    editor.screenCols = 120;
    auto sm = makeMachine(editor, DebuggerMode{"app", false});
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    std::string path;
    json_utils::append_escaped(path, file.string());
    debug->applyMessage(
        "{\"state\":\"stopped\",\"path\":" + path +
        ",\"line\":30,\"frame\":4,\"function\":\"main\",\"variables\":[\"count "
        "= 42\",\"ratio = "
        "1.5\"],\"threads\":[{\"id\":\"1\",\"label\":\"main\"}]}");
    sm.dispatch('v');
    const auto source = debug->source;
    const auto offset = debug->offset[0];
    testing::internal::CaptureStdout();
    debug->draw(editor);
    testing::internal::GetCapturedStdout();
    debug->applyMessage(
        "{\"state\":\"running\",\"variables\":[],\"threads\":[],\"path\":\"\","
        "\"line\":0,\"frame\":0,\"function\":\"\"}");
    EXPECT_EQ(debug->source, source);
    EXPECT_EQ(debug->sourcePath, file.string());
    EXPECT_EQ(debug->variables.size(), 2u);
    EXPECT_EQ(debug->threads.size(), 1u);
    EXPECT_EQ(debug->frameIndex, 4);
    EXPECT_EQ(debug->frameFunction, "main");
    EXPECT_EQ(debug->executionLine, 30);
    EXPECT_EQ(debug->pane, 2);
    EXPECT_EQ(debug->offset[0], offset);
    testing::internal::CaptureStdout();
    debug->draw(editor);
    const auto running = testing::internal::GetCapturedStdout();
    EXPECT_TRUE(running.empty());
    debug->applyMessage(
        "{\"state\":\"stopped\",\"path\":" + path +
        ",\"line\":31,\"variables\":[\"count = 43\",\"ratio = 1.5\"]}");
    EXPECT_EQ(debug->offset[0], offset);
    EXPECT_EQ(debug->cursor[0], 30);
    EXPECT_EQ(debug->pane, 2);
    testing::internal::CaptureStdout();
    debug->draw(editor);
    const auto stopped = testing::internal::GetCapturedStdout();
    EXPECT_NE(stopped.find("count = 43"), std::string::npos);
    EXPECT_EQ(stopped.find("No readable source"), std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST(RealModeTransitionsTest, DebuggerDelaysRunningHintsAndRefreshesWhileIdle)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.screenRows = 32;
    editor.screenCols = 120;
    auto sm = makeMachine(editor, DebuggerMode{"app", false});
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    // Exercise the idle refresh without starting a real debugger process.
    debug->started = true;
    debug->applyMessage("{\"state\":\"stopped\"}");
    testing::internal::CaptureStdout();
    debug->draw(editor);
    testing::internal::GetCapturedStdout();
    debug->applyMessage("{\"state\":\"running\"}");
    EXPECT_EQ(debug->state, "running");
    testing::internal::CaptureStdout();
    debug->draw(editor);
    EXPECT_TRUE(testing::internal::GetCapturedStdout().empty());

    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    // Repeated running notifications must not restart the delay.
    debug->applyMessage("{\"state\":\"running\"}");
    editor.needsFullRedraw = false;
    debug->processIdle(editor);
    EXPECT_TRUE(editor.needsFullRedraw);
    testing::internal::CaptureStdout();
    debug->draw(editor);
    const auto running = testing::internal::GetCapturedStdout();
    EXPECT_NE(running.find("Debugger [lldb] running"), std::string::npos);
    EXPECT_NE(running.find("Variables (last stop)"), std::string::npos);
    EXPECT_NE(running.find("Ctrl-C interrupts"), std::string::npos);

    debug->applyMessage("{\"state\":\"stopped\"}");
    testing::internal::CaptureStdout();
    debug->draw(editor);
    const auto stopped = testing::internal::GetCapturedStdout();
    EXPECT_NE(stopped.find("Debugger [lldb] stopped"), std::string::npos);
    EXPECT_NE(stopped.find("n next"), std::string::npos);

    debug->applyMessage("{\"state\":\"running\"}");
    debug->applyMessage("{\"state\":\"error\"}");
    testing::internal::CaptureStdout();
    debug->draw(editor);
    EXPECT_NE(
        testing::internal::GetCapturedStdout().find("Debugger [lldb] error"),
        std::string::npos);
}

TEST(RealModeTransitionsTest, DebuggerCommandMenuOnlyRepaintsCoveredRows)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.screenRows = 32;
    editor.screenCols = 120;
    auto sm = makeMachine(editor, DebuggerMode{"app", false});
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    debug->state = "stopped";
    debug->sourcePath = "demo.cpp";
    debug->source = {"int main() { return 42; }"};
    debug->variables = {"count = 42"};
    debug->threads = {"main-thread"};
    testing::internal::CaptureStdout();
    debug->draw(editor);
    testing::internal::GetCapturedStdout();
    testing::internal::CaptureStdout();
    debug->draw(editor);
    EXPECT_TRUE(testing::internal::GetCapturedStdout().empty());
    sm.dispatch(':');
    sm.dispatch('n');
    testing::internal::CaptureStdout();
    debug->draw(editor);
    const auto prompt = testing::internal::GetCapturedStdout();
    EXPECT_NE(prompt.find(":n"), std::string::npos);
    EXPECT_EQ(prompt.find("Variables"), std::string::npos);
    EXPECT_NE(prompt.find("Debugger commands"), std::string::npos);
    EXPECT_EQ(prompt.find("int main"), std::string::npos);
}

TEST(RealModeTransitionsTest,
     DebuggerProcessListingPreservesNamesAndRejectsInvalidPids)
{
    const auto entries = parseDebuggerProcesses(
        " 234 /tmp/My App/debugger-cpp\n12 /bin/helper name\ninvalid row\n"
        "0 /bin/zero\n-1 /bin/negative\n99999999999999999 /bin/overflow\n234 "
        "duplicate\n");
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].pid, 12);
    EXPECT_EQ(entries[0].name, "helper name");
    EXPECT_EQ(entries[1].pid, 234);
    EXPECT_EQ(entries[1].name, "debugger-cpp");
    EXPECT_EQ(entries[1].executable, "/tmp/My App/debugger-cpp");
}

namespace
{
DebuggerProcessList recordedDebuggerProcesses()
{
    return {{{1234, "debugger-cpp", "/tmp/My App/debugger-cpp"},
             {444, "compiler", "/bin/compiler"},
             {12, "helper-1234", "/bin/helper-1234"}},
            {}};
}
} // namespace

TEST(RealModeTransitionsTest,
     DebuggerProcessPopupFiltersAndNavigatesWithoutReplacingQuery)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    int loads = 0;
    CommandMode command;
    command.initialInput = "dbgattach --";
    command.processPicker.loader = [&]
    {
        ++loads;
        return recordedDebuggerProcesses();
    };
    auto sm = makeMachine(editor, std::move(command));
    ASSERT_TRUE(editor.commandPopupActive);
    ASSERT_EQ(editor.commandPopupFiltered.size(), 3u);
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_EQ(editor.commandPopupCursor, 1);
    EXPECT_EQ(editor.commandBuffer, ":dbgattach --");
    sm.dispatch(keyCode(control::ControlKey::CTRL_K));
    EXPECT_EQ(editor.commandPopupCursor, 0);
    for(char c : std::string("dbc"))
        sm.dispatch(c);
    EXPECT_EQ(editor.commandBuffer, ":dbgattach --dbc");
    ASSERT_EQ(editor.commandPopupFiltered.size(), 1u);
    EXPECT_EQ(editor.commandPopupFiltered[0], 0);
    EXPECT_EQ(loads, 1);
    std::string popup;
    editor.drawCommandPopup(popup);
    EXPECT_NE(popup.find("Processes"), std::string::npos);
    EXPECT_NE(popup.find("debugger-cpp"), std::string::npos);
    EXPECT_NE(popup.find(ascii::utf8(ascii::BOX_ROUNDED_TOP_LEFT)),
              std::string::npos);
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    EXPECT_TRUE(debug->attach);
    EXPECT_EQ(debug->target, "1234");
    EXPECT_FALSE(editor.commandPopupActive);
    EXPECT_TRUE(editor.commandPopupTitle.empty());
}

TEST(RealModeTransitionsTest,
     DebuggerProcessPopupPreservesBackendAndTabInsertsPid)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    CommandMode command;
    command.initialInput = "dbgattach --backend gdb -- 1234";
    command.processPicker.loader = recordedDebuggerProcesses;
    auto sm = makeMachine(editor, std::move(command));
    ASSERT_TRUE(editor.commandPopupActive);
    ASSERT_EQ(editor.commandPopupFiltered.size(), 2u);
    EXPECT_EQ(editor.commandPopupFiltered.front(), 0); // exact PID ranks first
    sm.dispatch(keyCode(control::ControlKey::TAB));
    EXPECT_FALSE(editor.commandPopupActive);
    EXPECT_EQ(editor.commandBuffer, ":dbgattach --backend gdb 1234");
    EXPECT_NE(sm.getState<CommandMode>(), nullptr);
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    EXPECT_EQ(debug->backend, "gdb");
    EXPECT_EQ(debug->target, "1234");
}

TEST(RealModeTransitionsTest,
     DebuggerProcessPopupHandlesEmptyResultsRefreshAndEscape)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    int loads = 0;
    CommandMode command;
    command.initialInput = "dbgattach --";
    command.processPicker.loader = [&]
    {
        if(loads++ == 0)
            return DebuggerProcessList{{},
                                       "Cannot list processes: access denied"};
        return recordedDebuggerProcesses();
    };
    auto sm = makeMachine(editor, std::move(command));
    EXPECT_TRUE(editor.commandPopupFiltered.empty());
    EXPECT_NE(editor.commandPopupEmptyMessage.find("access denied"),
              std::string::npos);
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_NE(sm.getState<CommandMode>(), nullptr);
    EXPECT_TRUE(editor.commandPopupActive);
    sm.dispatch(keyCode(control::ControlKey::CTRL_R));
    EXPECT_EQ(loads, 2);
    EXPECT_EQ(editor.commandPopupFiltered.size(), 3u);
    for(char c : std::string(" --no-such-process-xyz"))
        sm.dispatch(c);
    EXPECT_TRUE(editor.commandPopupFiltered.empty());
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_NE(sm.getState<CommandMode>(), nullptr);
    sm.dispatch(keyCode(control::ControlKey::CTRL_U));
    EXPECT_EQ(editor.commandPopupFiltered.size(), 3u);
    sm.dispatch(27);
    EXPECT_FALSE(editor.commandPopupActive);
    EXPECT_EQ(editor.commandBuffer, ":dbgattach ");
    EXPECT_NE(sm.getState<CommandMode>(), nullptr);
    sm.dispatch(27);
    EXPECT_NE(sm.getState<NormalMode>(), nullptr);
}

TEST(RealModeTransitionsTest, DebuggerProcessPickerWorksInSharedBrowserPrompt)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto ctx = createModeContext(&editor);
    CommandPrompt prompt;
    prompt.processPicker.loader = recordedDebuggerProcesses;
    std::optional<ModeState> next;
    std::string executed;
    auto execute = [&](std::string_view command) -> std::optional<ModeState>
    {
        executed = command;
        return {};
    };
    prompt.handle(ctx, ':', execute, next);
    for(char c : std::string("dbgattach -- 1234"))
        prompt.handle(ctx, c, execute, next);
    EXPECT_TRUE(editor.commandPopupActive);
    prompt.handle(ctx, keyCode(control::ControlKey::CTRL_J), execute, next);
    EXPECT_EQ(prompt.getInput(), "dbgattach -- 1234");
    prompt.handle(ctx, keyCode(control::ControlKey::CTRL_K), execute, next);
    prompt.handle(ctx, keyCode(control::ControlKey::ENTER), execute, next);
    EXPECT_EQ(executed, "dbgattach 1234");
    EXPECT_FALSE(prompt.isActive());
    EXPECT_FALSE(editor.commandPopupActive);
}

TEST(RealModeTransitionsTest,
     DebuggerBreakpointMarkersTrackBackendLocationsAndDeletion)
{
    auto dir = make_temp_dir("uvim_breakpoints_");
    auto file = dir / "demo.cpp";
    write_file(file,
               "int main() {\n    int count = 42;\n    return count;\n}\n");
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.screenRows = 30;
    editor.screenCols = 120;
    auto sm = makeMachine(editor, DebuggerMode{"app", false});
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    std::string path;
    json_utils::append_escaped(path, file.string());
    debug->applyMessage("{\"state\":\"stopped\",\"path\":" + path +
                        ",\"line\":2,\"breakpoints\":[{\"id\":\"1\","
                        "\"enabled\":true,\"locations\":[{\"path\":" +
                        path +
                        ",\"line\":2}]},{\"id\":\"2\",\"enabled\":false,"
                        "\"locations\":[{\"path\":" +
                        path + ",\"line\":3}]}]}");
    EXPECT_TRUE(debug->breakpointLines.contains(2));
    EXPECT_FALSE(debug->breakpointLines.contains(3));
    testing::internal::CaptureStdout();
    debug->draw(editor);
    auto rendered = testing::internal::GetCapturedStdout();
    EXPECT_NE(rendered.find(color::rgbBg(170, 35, 50)), std::string::npos);
    dispatch_command(sm, "bps");
    EXPECT_TRUE(debug->breakpointListOpen);
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_EQ(debug->breakpointCursor, 1);
    sm.dispatch(keyCode(control::ControlKey::CTRL_K));
    EXPECT_EQ(debug->breakpointCursor, 0);
    testing::internal::CaptureStdout();
    debug->draw(editor);
    rendered = testing::internal::GetCapturedStdout();
    EXPECT_NE(rendered.find("Breakpoints"), std::string::npos);
    EXPECT_NE(rendered.find("#1 enabled"), std::string::npos);
    EXPECT_NE(rendered.find("#2 disabled"), std::string::npos);
    debug->applyMessage("{\"breakpoints\":[]}");
    EXPECT_TRUE(debug->breakpoints.empty());
    EXPECT_TRUE(debug->breakpointLines.empty());
    testing::internal::CaptureStdout();
    debug->draw(editor);
    rendered = testing::internal::GetCapturedStdout();
    EXPECT_NE(rendered.find("No breakpoints"), std::string::npos);
    EXPECT_EQ(rendered.find(color::rgbBg(170, 35, 50)), std::string::npos);
    sm.dispatch(27);
    EXPECT_FALSE(debug->breakpointListOpen);
    EXPECT_NE(sm.getState<DebuggerMode>(), nullptr);
    std::filesystem::remove_all(dir);
}

TEST(RealModeTransitionsTest,
     DebuggerPromptCommandsFilterAndCompleteWithNavigation)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, DebuggerMode{"app", false});
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    sm.dispatch(':');
    ASSERT_GT(debug->promptCommands.size(), 3u);
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_EQ(debug->promptMenuCursor, 1);
    EXPECT_TRUE(debug->prompt.empty());
    sm.dispatch(keyCode(control::ControlKey::TAB));
    EXPECT_EQ(debug->prompt, "bps");
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_TRUE(debug->breakpointListOpen);
    sm.dispatch(27);
    sm.dispatch(':');
    for(char c : std::string("del"))
        sm.dispatch(c);
    ASSERT_FALSE(debug->promptCommands.empty());
    EXPECT_EQ(debug->promptCommands.front(), "delete ");
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_EQ(debug->prompt, "delete ");
    EXPECT_TRUE(debug->promptActive);
    EXPECT_TRUE(debug->promptCommands.empty());
    sm.dispatch(27);
    sm.dispatch(':');
    sm.dispatch('n');
    ASSERT_FALSE(debug->promptCommands.empty());
    EXPECT_EQ(debug->promptCommands.front(), "next");
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_FALSE(debug->promptActive);
    EXPECT_TRUE(debug->promptCommands.empty());
}
#ifdef UVIM_ENABLE_SEARCH_TOOLS
TEST(RealModeTransitionsTest, DebuggerFilePickerKeepsSessionAndPinsOtherSource)
{
    auto dir = make_temp_dir("uvim_debugger_pick_source_");
    auto main = dir / "main.cpp";
    auto helper = dir / "helper.hpp";
    write_file(main,
               "int main() {\n    int count = 42;\n    return count;\n}\n");
    write_file(
        helper,
        "inline int helper() {\n    int value = 7;\n    return value;\n}\n");
    ScopedCurrentPath cwd(dir);
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.useGitFileIndex = false;
    editor.currentBuffer->lines = {"untouched editor buffer"};
    const auto buffer = editor.currentBuffer;
    auto sm = makeMachine(editor, DebuggerMode{"program", false});
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    std::string mainPath, helperPath;
    json_utils::append_escaped(mainPath, main.string());
    json_utils::append_escaped(helperPath, helper.string());
    debug->applyMessage("{\"state\":\"stopped\",\"path\":" + mainPath +
                        ",\"line\":2}");
    sm.dispatch(keyCode(control::ControlKey::CTRL_P));
    ASSERT_NE(debug->filePicker, nullptr);
    EXPECT_TRUE(debug->filePicker->singleSelection);
    for(char c : std::string("helper.hpp:3"))
        sm.dispatch(c);
    ASSERT_EQ(debug->filePicker->matches.size(), 1u);
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_EQ(sm.getState<DebuggerMode>(), debug);
    EXPECT_EQ(debug->filePicker, nullptr);
    EXPECT_TRUE(debug->sourcePinned);
    EXPECT_EQ(std::filesystem::weakly_canonical(debug->sourcePath),
              std::filesystem::weakly_canonical(helper));
    EXPECT_EQ(debug->cursor[0], 2);
    EXPECT_EQ(debug->pane, 0);
    EXPECT_EQ(debug->state, "stopped");
    EXPECT_EQ(editor.currentBuffer, buffer);
    EXPECT_EQ(editor.currentBuffer->lines,
              (std::vector<std::string>{"untouched editor buffer"}));
    debug->applyMessage("{\"state\":\"stopped\",\"path\":" + mainPath +
                        ",\"line\":2,\"breakpoints\":[{\"id\":\"1\","
                        "\"enabled\":true,\"locations\":[{\"path\":" +
                        helperPath + ",\"line\":3}]}]}");
    EXPECT_EQ(std::filesystem::weakly_canonical(debug->sourcePath),
              std::filesystem::weakly_canonical(helper));
    EXPECT_EQ(debug->cursor[0], 2);
    EXPECT_TRUE(debug->breakpointLines.contains(3));
    testing::internal::CaptureStdout();
    debug->draw(editor);
    const auto rendered = testing::internal::GetCapturedStdout();
    EXPECT_EQ(rendered.find(">2 "), std::string::npos);
    EXPECT_NE(rendered.find("Source (browse)"), std::string::npos);
    debug->followExecutionOnStop = true;
    debug->applyMessage("{\"state\":\"stopped\",\"path\":" + mainPath +
                        ",\"line\":3}");
    EXPECT_FALSE(debug->sourcePinned);
    EXPECT_EQ(debug->sourcePath, main.string());
    EXPECT_EQ(debug->cursor[0], 2);
    const auto previous = debug->source;
    sm.dispatch(keyCode(control::ControlKey::CTRL_P));
    ASSERT_NE(debug->filePicker, nullptr);
    sm.dispatch(27);
    EXPECT_EQ(debug->filePicker, nullptr);
    EXPECT_EQ(debug->source, previous);
    EXPECT_EQ(debug->cursor[0], 2);
    EXPECT_TRUE(editor.fuzzyFileIndexInitialized);
    std::filesystem::remove_all(dir);
}
#endif

TEST(RealModeTransitionsTest,
     DebuggerSourceSelectionRejectsBinaryWithoutChangingSource)
{
    auto dir = make_temp_dir("uvim_debugger_bad_source_");
    auto file = dir / "binary.cpp";
    write_file(file, std::string("a\0b", 3));
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, DebuggerMode{"program", false});
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    debug->sourcePath = "original.cpp";
    debug->source = {"int main() {}"};
    EXPECT_FALSE(debug->selectSourceFile(file.string()));
    EXPECT_EQ(debug->sourcePath, "original.cpp");
    EXPECT_EQ(debug->source, (std::vector<std::string>{"int main() {}"}));
    EXPECT_FALSE(debug->sourcePinned);
    EXPECT_FALSE(debug->output.empty());
    std::filesystem::remove_all(dir);
}
#endif
