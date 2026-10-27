#include "ascii.h"
#include "json_utils.h"
#include "real_mode_test_utils.h"
#include <thread>
#ifdef UVIM_ENABLE_DEBUGGER
using namespace uvim_test;

TEST(RealModeTransitionsTest, DebuggerCommandsSelectRunAndAttach)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});
    dispatch_command(
        sm,
        "dbgrun --backend gdb './app with spaces' -- arg1 'two words'");
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    EXPECT_FALSE(debug->attach);
    EXPECT_EQ(debug->backend, "gdb");
    EXPECT_EQ(
        debug->target,
        EditorPathUtilities::resolveEditorPath("./app with spaces").string());
    EXPECT_EQ(
        debug->programArguments,
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

TEST(
    RealModeTransitionsTest,
    DebuggerFramePickerFiltersSymbolsAndInsertsFrameIdentity)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.screenRows = 30;
    editor.screenCols = 120;
    auto sm = makeMachine(editor, DebuggerMode{"app", false});
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    debug->applyMessage("{\"state\":\"stopped\"}");
    sm.dispatch(':');
    for(char key : std::string("frame --"))
        sm.dispatch(key);
    EXPECT_TRUE(debug->framePickerOpen);
    EXPECT_TRUE(debug->promptActive);
    const std::string frames =
        "{\"frames\":[{\"id\":\"0\",\"function\":\"__semwait_signal\","
        "\"address\":"
        "\"0x1000\"},"
        "{\"id\":\"4\",\"function\":\"main::entry_point\",\"path\":\"/tmp/"
        "app.cpp\",\"line\":42,\"address\":\"0x2000\",\"selected\":true},"
        "{\"id\":\"7\",\"function\":\"caller\",\"address\":\"0x3000\"},"
        "{\"id\":\"invalid\",\"function\":\"bad ID\"}]}";
    debug->applyMessage(frames);
    ASSERT_EQ(debug->frameMatches.size(), 3u);
    EXPECT_EQ(debug->framePickerCursor, 1);
    EXPECT_EQ(debug->stackFrames[1].id, "4");
    testing::internal::CaptureStdout();
    debug->draw(editor);
    const auto rendered = testing::internal::GetCapturedStdout();
    EXPECT_NE(rendered.find("Stack frames"), std::string::npos);
    EXPECT_NE(rendered.find("main::entry_point"), std::string::npos);
    EXPECT_NE(rendered.find("/tmp/app.cpp"), std::string::npos);
    // Query characters work immediately after --, without requiring a space.
    for(char key : std::string("mai"))
        sm.dispatch(key);
    ASSERT_FALSE(debug->frameMatches.empty());
    EXPECT_EQ(debug->stackFrames[debug->frameMatches[0]].id, "4");
    sm.dispatch(keyCode(control::ControlKey::CTRL_U));
    for(char key : std::string("entry"))
        sm.dispatch(key);
    EXPECT_EQ(debug->prompt, "frame -- entry");
    ASSERT_EQ(debug->frameMatches.size(), 1u);
    EXPECT_EQ(debug->stackFrames[debug->frameMatches[0]].id, "4");
    sm.dispatch(keyCode(control::ControlKey::CTRL_U));
    ASSERT_EQ(debug->frameMatches.size(), 3u);
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_EQ(debug->framePickerCursor, 1);
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_EQ(debug->framePickerCursor, 2);
    sm.dispatch(keyCode(control::ControlKey::CTRL_K));
    EXPECT_EQ(debug->framePickerCursor, 1);
    sm.dispatch(keyCode(control::ControlKey::TAB));
    EXPECT_FALSE(debug->framePickerOpen);
    EXPECT_TRUE(debug->promptActive);
    EXPECT_EQ(debug->prompt, "frame 4");
    EXPECT_FALSE(debug->followExecutionOnStop);
    sm.dispatch(27);
    EXPECT_FALSE(debug->promptActive);
    sm.dispatch(':');
    for(char key : std::string("frame --zzz"))
        sm.dispatch(key);
    debug->applyMessage(frames);
    EXPECT_TRUE(debug->frameMatches.empty());
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_TRUE(debug->framePickerOpen);
    sm.dispatch(keyCode(control::ControlKey::CTRL_U));
    sm.dispatch('7');
    ASSERT_FALSE(debug->frameMatches.empty());
    EXPECT_EQ(debug->stackFrames[debug->frameMatches[0]].id, "7");
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_FALSE(debug->framePickerOpen);
    EXPECT_FALSE(debug->promptActive);
    EXPECT_NE(sm.getState<DebuggerMode>(), nullptr);
}

TEST(
    RealModeTransitionsTest,
    DebuggerFramePickerPreviewsSourceAndLibraryAssemblyWithoutSelecting)
{
    auto dir = make_temp_dir("uvim_frame_preview_");
    auto original = dir / "original.cpp";
    auto other = dir / "preview.cpp";
    write_file(original, "int main() {\n  return 0;\n}\n");
    write_file(
        other,
        "int other() {\n  int value = 7;\n  // "
        "PREVIEW_TARGET_LINE\n  return value;\n}\n");
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.screenRows = 32;
    editor.screenCols = 120;
    auto sm = makeMachine(editor, DebuggerMode{"app", false});
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    std::string originalPath, previewPath;
    json_utils::append_escaped(originalPath, original.string());
    json_utils::append_escaped(previewPath, other.string());
    debug->applyMessage(
        "{\"state\":\"stopped\",\"path\":" + originalPath +
        ",\"line\":2,\"frame\":0,\"variables\":[\"original value\"]}");
    const auto source = debug->source;
    const auto output = debug->output;
    const auto cursor = debug->cursor;
    sm.dispatch(':');
    for(char key : std::string("frame --"))
        sm.dispatch(key);
    debug->applyMessage(
        "{\"frames\":[{\"id\":\"0\",\"function\":\"main\",\"path\":" +
        originalPath +
        ",\"line\":2,\"returnType\":\"int\",\"selected\":true},{\"id\":\"7\","
        "\"function\":\"other\","
        "\"path\":" +
        previewPath +
        ",\"line\":3},{\"id\":\"9\",\"function\":\"library_call\",\"module\":"
        "\"libexample.so\",\"address\":\"0x3000\"}]}");
    EXPECT_NE(debug->stackFrames[0].label.find("int main"), std::string::npos);
    const auto& spans = debug->stackFrames[0].colors;
    EXPECT_TRUE(
        std::any_of(
            spans.begin(),
            spans.end(),
            [](const Token& token) { return token.type == TOKEN_TYPE; }));
    EXPECT_TRUE(
        std::any_of(
            spans.begin(),
            spans.end(),
            [](const Token& token) { return token.type == TOKEN_FUNCTION; }));
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_EQ(debug->framePreviewId, "7");
    EXPECT_TRUE(debug->framePreviewSourceAvailable);
    EXPECT_EQ(debug->framePreviewLine, 3);
    testing::internal::CaptureStdout();
    debug->draw(editor);
    const auto rendered = testing::internal::GetCapturedStdout();
    EXPECT_NE(rendered.find("Preview #7 preview.cpp:3"), std::string::npos);
    EXPECT_NE(rendered.find("PREVIEW_TARGET_LINE"), std::string::npos);
    EXPECT_NE(rendered.find(">3 "), std::string::npos);
    EXPECT_EQ(debug->source, source);
    EXPECT_EQ(debug->sourcePath, original.string());
    EXPECT_EQ(debug->cursor, cursor);
    EXPECT_EQ(debug->frameIndex, 0);
    EXPECT_EQ(debug->variables, (std::vector<std::string>{"original value"}));
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_FALSE(debug->framePreviewSourceAvailable);
    EXPECT_NE(debug->framePreviewLines[0].find("Ctrl+D"), std::string::npos);
    sm.dispatch(keyCode(control::ControlKey::CTRL_D));
    EXPECT_TRUE(debug->framePreviewAssembly);
    const auto token = debug->framePreviewToken;
    debug->applyMessage(
        "{\"framePreview\":{\"id\":\"9\",\"token\":" +
        std::to_string(token - 1) + ",\"lines\":[\"stale response\"]}}");
    EXPECT_NE(debug->framePreviewLines[0], "stale response");
    debug->applyMessage(
        "{\"framePreview\":{\"id\":\"9\",\"token\":" + std::to_string(token) +
        ",\"lines\":[\"0x3000 nop\",\"0x3001 ret\"]}}");
    EXPECT_EQ(debug->framePreviewLines[0], "0x3000 nop");
    testing::internal::CaptureStdout();
    debug->draw(editor);
    const auto assembly = testing::internal::GetCapturedStdout();
    EXPECT_NE(
        assembly.find("Assembly preview #9 libexample.so"),
        std::string::npos);
    EXPECT_NE(assembly.find("0x3000"), std::string::npos);
    EXPECT_EQ(debug->output, output);
    EXPECT_EQ(debug->frameIndex, 0);
    std::string instructions;
    for(int i = 0; i < 80; ++i)
    {
        if(i)
            instructions += ",";
        instructions += "\"0x" + std::to_string(3000 + i) + " nop\"";
    }
    debug->applyMessage(
        "{\"framePreview\":{\"id\":\"9\",\"token\":" + std::to_string(token) +
        ",\"lines\":[" + instructions + "]}}");
    const auto picked = debug->framePickerCursor;
    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_J));
    EXPECT_EQ(debug->framePreviewCursor, 1);
    EXPECT_EQ(debug->framePickerCursor, picked);
    EXPECT_EQ(debug->frameIndex, 0);
    EXPECT_EQ(debug->cursor, cursor);
    for(int i = 0; i < 200; ++i)
        sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_J));
    const auto bottom = debug->framePreviewScroll;
    EXPECT_GT(bottom, 0);
    EXPECT_LT(bottom, 80);
    EXPECT_EQ(debug->framePreviewCursor, 79);
    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_K));
    EXPECT_EQ(debug->framePreviewCursor, 78);
    for(int i = 0; i < 200; ++i)
        sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_K));
    EXPECT_EQ(debug->framePreviewScroll, 0);
    sm.dispatch(27);
    EXPECT_FALSE(debug->framePickerOpen);
    EXPECT_EQ(debug->source, source);
    testing::internal::CaptureStdout();
    debug->draw(editor);
    const auto restored = testing::internal::GetCapturedStdout();
    EXPECT_EQ(restored.find("Assembly preview"), std::string::npos);
    EXPECT_EQ(restored.find(Terminal::ESC_CLEAR_SCREEN), std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST(
    RealModeTransitionsTest,
    DebuggerInstructionHelpPreservesFrameSearchAndTracksAssemblyRow)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.screenRows = 32;
    editor.screenCols = 120;
    editor.asmDocsApi = false;
    auto sm = makeMachine(editor, DebuggerMode{"app", false});
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    debug->started = true; // Exercise inspection without starting a debugger.
    debug->applyMessage("{\"state\":\"stopped\"}");
    sm.dispatch(':');
    for(char key : std::string("frame --"))
        sm.dispatch(key);
    debug->applyMessage(
        "{\"frames\":[{\"id\":\"0\",\"function\":\"library_"
        "call\",\"address\":\"0x3001\"}]}");
    sm.dispatch('i');
    EXPECT_EQ(debug->prompt, "frame -- i");
    EXPECT_FALSE(debug->assemblyHelpOpen);
    sm.dispatch(keyCode(control::ControlKey::CTRL_U));
    sm.dispatch(keyCode(control::ControlKey::CTRL_D));
    debug->applyMessage(
        "{\"framePreview\":{\"id\":\"0\",\"token\":" +
        std::to_string(debug->framePreviewToken) +
        ",\"lines\":[\"0x3000 movq %rdi, %rax  [48 89 f8]\",\"0x3001 addq $1, "
        "%rax  [48 83 c0 01]\",\"0x3005 retq  [c3]\"]}}");
    EXPECT_EQ(debug->framePreviewCursor, 1);
    const auto prompt = debug->prompt;
    const auto picked = debug->framePickerCursor;
    sm.dispatch(keyCode(control::ControlKey::CTRL_I));
    EXPECT_TRUE(debug->assemblyHelpOpen);
    EXPECT_TRUE(debug->framePickerOpen);
    EXPECT_EQ(debug->prompt, prompt);
#ifdef UVIM_ENABLE_ASM_DOCS
    EXPECT_EQ(debug->assemblyHelp->docsTitle, "add (x86)");
    EXPECT_NE(
        debug->assemblyHelp->docsText.find("addq $1, %rax"),
        std::string::npos);
    EXPECT_EQ(debug->assemblyHelp->docsText.find("[48 83"), std::string::npos);
#else
    EXPECT_NE(
        debug->assemblyHelp->docsText.find("not compiled"),
        std::string::npos);
#endif
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_EQ(debug->assemblyHelpOffset, 1);
    EXPECT_EQ(debug->framePickerCursor, picked);
    sm.dispatch(keyCode(control::ControlKey::CTRL_K));
    EXPECT_EQ(debug->assemblyHelpOffset, 0);
    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_J));
    EXPECT_EQ(debug->framePreviewCursor, 2);
    EXPECT_EQ(debug->framePickerCursor, picked);
    EXPECT_EQ(debug->prompt, prompt);
    std::this_thread::sleep_for(std::chrono::milliseconds(320));
    debug->processIdle(editor);
#ifdef UVIM_ENABLE_ASM_DOCS
    EXPECT_EQ(debug->assemblyHelp->docsTitle, "ret (x86)");
#endif
    testing::internal::CaptureStdout();
    debug->draw(editor);
    const auto rendered = testing::internal::GetCapturedStdout();
    EXPECT_NE(rendered.find("Inspect:"), std::string::npos);
    EXPECT_NE(rendered.find("Ctrl-I/Esc: close"), std::string::npos);
    sm.dispatch(27);
    EXPECT_FALSE(debug->assemblyHelpOpen);
    EXPECT_TRUE(debug->framePickerOpen);
    EXPECT_EQ(debug->prompt, prompt);
    // In assembly preview, Tab/Ctrl-I share a key code and both toggle help.
    sm.dispatch(keyCode(control::ControlKey::TAB));
    EXPECT_TRUE(debug->assemblyHelpOpen);
    sm.dispatch(keyCode(control::ControlKey::CTRL_I));
    EXPECT_FALSE(debug->assemblyHelpOpen);
    sm.dispatch(27);
    sm.dispatch(27);
    debug->pane = 1;
    debug->output = {"0x4000 movq %rdi, %rax  [48 89 f8]", "0x4003 retq  [c3]"};
    debug->assemblyOutputLines = {true, true};
    debug->cursor[1] = 0;
    sm.dispatch('i');
    EXPECT_TRUE(debug->assemblyHelpOpen);
#ifdef UVIM_ENABLE_ASM_DOCS
    EXPECT_EQ(debug->assemblyHelp->docsTitle, "mov (x86)");
#endif
    sm.dispatch('j');
    EXPECT_EQ(debug->cursor[1], 1);
    testing::internal::CaptureStdout();
    debug->draw(editor);
    testing::internal::GetCapturedStdout();
    sm.dispatch('i');
    EXPECT_FALSE(debug->assemblyHelpOpen);
    debug->output = {"ordinary process output"};
    debug->assemblyOutputLines = {false};
    debug->cursor[1] = 0;
    sm.dispatch('i');
    EXPECT_FALSE(debug->assemblyHelpOpen);
}

TEST(RealModeTransitionsTest, DebuggerFramePickerCancelsAndShowsErrors)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, DebuggerMode{"app", false});
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    debug->applyMessage("{\"state\":\"stopped\"}");
    sm.dispatch(':');
    for(char key : std::string("frame --"))
        sm.dispatch(key);
    debug->applyMessage(
        "{\"frames\":[],\"framesError\":\"No selected thread\"}");
    EXPECT_EQ(debug->framePickerError, "No selected thread");
    sm.dispatch(keyCode(control::ControlKey::CTRL_R));
    EXPECT_FALSE(debug->framePickerLoading);
    EXPECT_EQ(debug->framePickerError, "Debugger is not connected");
    sm.dispatch(27);
    EXPECT_FALSE(debug->framePickerOpen);
    EXPECT_TRUE(debug->promptActive);
    EXPECT_EQ(debug->prompt, "frame ");
    debug->applyMessage("{\"frames\":[{\"id\":\"0\",\"function\":\"late\"}]}");
    EXPECT_FALSE(debug->framePickerOpen);
    sm.dispatch(27);
    sm.dispatch(':');
    for(char key : std::string("frame --"))
        sm.dispatch(key);
    debug->applyMessage("{\"frames\":[{\"id\":\"0\",\"function\":\"main\"}]}");
    debug->applyMessage("{\"state\":\"running\"}");
    EXPECT_TRUE(debug->frameMatches.empty());
    EXPECT_EQ(
        debug->framePickerError,
        "Pause execution before browsing stack frames");
}

TEST(RealModeTransitionsTest, DebuggerRejectsInvalidLaunchRequests)
{
    for(const auto& command :
        {"dbgattach 0",
         "dbgattach -3",
         "dbgattach 12x",
         "dbgattach 99999999999999999999999",
         "dbgattach 3 extra",
         "dbgrun",
         "dbgrun --backend unknown app",
         "dbgrun 'unclosed"})
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

TEST(
    RealModeTransitionsTest,
    DebuggerFrameSelectionReturnsToUnchangedExecutionLine)
{
    auto dir = make_temp_dir("uvim_debugger_frame_");
    auto file = dir / "recursive.cpp";
    write_file(
        file,
        "int recurse(int depth) {\n  return recurse(depth - 1);\n}\n");
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, DebuggerMode{"app", false});
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    std::string path;
    json_utils::append_escaped(path, file.string());
    const auto location = "\"path\":" + path + ",\"line\":2";
    debug->applyMessage("{\"state\":\"stopped\",\"frame\":0," + location + "}");
    ASSERT_TRUE(debug->selectSourceFile(file.string(), 3));
    EXPECT_EQ(debug->cursor[0], 2);
    debug->followExecutionOnStop = true;
    debug->applyMessage("{\"state\":\"stopped\",\"frame\":1," + location + "}");
    EXPECT_EQ(debug->frameIndex, 1);
    EXPECT_FALSE(debug->sourcePinned);
    EXPECT_EQ(debug->cursor[0], 1);
    std::filesystem::remove_all(dir);
}

TEST(RealModeTransitionsTest, DebuggerStoppedPanesAndPromptNavigation)
{
    auto dir = make_temp_dir("uvim_debugger_");
    auto file = dir / "debug source.cpp";
    write_file(
        file,
        "int main() {\n    int count = 42;\n    return count;\n}\n");
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, DebuggerMode{"app", false});
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    std::string quoted;
    json_utils::append_escaped(quoted, file.string());
    debug->applyMessage(
        "{\"state\":\"stopped\",\"path\":" + quoted +
        ",\"line\":2,\"variables\":[\"count = 42\",\"other = "
        "3\"],\"threads\":[{\"id\":\"1\",\"label\":\"main\"},{"
        "\"id\":\"2\",\"label\":\"worker\"}]}");
    EXPECT_EQ(debug->source.size(), 4u);
    EXPECT_EQ(debug->executionLine, 2);
    EXPECT_EQ(debug->cursor[0], 1);
    sm.dispatch(keyCode(control::ControlKey::TAB));
    EXPECT_EQ(debug->pane, 1);
    sm.dispatch(keyCode(control::ControlKey::TAB));
    EXPECT_EQ(debug->pane, 2);
    sm.dispatch('j');
    EXPECT_EQ(debug->cursor[2], 1);
    sm.dispatch(keyCode(control::ControlKey::TAB));
    EXPECT_EQ(debug->pane, 3);
    sm.dispatch('j');
    EXPECT_EQ(debug->cursor[3], 1);
    sm.dispatch(keyCode(control::ControlKey::TAB));
    EXPECT_EQ(debug->pane, 0);
    sm.dispatch(keyCode(control::ControlKey::SHIFT_TAB));
    EXPECT_EQ(debug->pane, 3);
    sm.dispatch(keyCode(control::ControlKey::SHIFT_TAB));
    EXPECT_EQ(debug->pane, 2);
    for(auto key :
        {control::ControlKey::SHIFT_CTRL_H,
         control::ControlKey::SHIFT_CTRL_J,
         control::ControlKey::SHIFT_CTRL_K,
         control::ControlKey::SHIFT_CTRL_L})
    {
        sm.dispatch(keyCode(key));
        EXPECT_EQ(debug->pane, 2);
    }
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

TEST(
    RealModeTransitionsTest,
    DebuggerInspectionShowsNewResultsAndKeepsVariableIdentity)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, DebuggerMode{"app", false});
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    debug->applyMessage(
        "{\"state\":\"stopped\",\"variables\":[\"callback: int "
        "(*)(int) = 0x1000\"],"
        "\"variableExpressions\":[\"callback\"]}");
    EXPECT_EQ(
        debug->variableExpressions,
        (std::vector<std::string>{"callback"}));
    sm.dispatch('v');
    const auto start = static_cast<int>(debug->output.size());
    debug->applyMessage(
        "{\"inspection\":true,\"output\":[\"Disassembly: "
        "callback\",\"0x1000 ret\"]}");
    EXPECT_EQ(debug->pane, 1);
    EXPECT_EQ(debug->offset[1], start);
    EXPECT_EQ(debug->cursor[1], start);
    EXPECT_EQ(debug->state, "stopped");
    debug->applyMessage(
        "{\"state\":\"running\",\"variables\":[],\"variableExpressions\":[]}");
    EXPECT_EQ(
        debug->variableExpressions,
        (std::vector<std::string>{"callback"}));
    debug->applyMessage("{\"state\":\"stopped\",\"variables\":[]}");
    EXPECT_TRUE(debug->variableExpressions.empty());
}

TEST(RealModeTransitionsTest, DebuggerSizePopupScrollsAndRestoresVariableFocus)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.screenRows = 32;
    editor.screenCols = 100;
    auto sm = makeMachine(editor, DebuggerMode{"app", false});
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    debug->applyMessage(
        "{\"state\":\"stopped\",\"variables\":[\"item\"],"
        "\"variableExpressions\":[\"item\"]}");
    sm.dispatch('v');
    const auto output = debug->output;
    debug->applyMessage(
        "{\"layoutTitle\":\"Size and padding: "
        "item\",\"layoutLines\":[\"Storage: item, 12 "
        "bytes\",\"Padding: 6 bytes\"]}");
    EXPECT_TRUE(debug->sizePopupOpen);
    EXPECT_EQ(debug->pane, 2);
    EXPECT_EQ(debug->output, output);
    testing::internal::CaptureStdout();
    debug->draw(editor);
    const auto popup = testing::internal::GetCapturedStdout();
    EXPECT_NE(popup.find("Size and padding: item"), std::string::npos);
    EXPECT_NE(popup.find("Storage: item, 12 bytes"), std::string::npos);
    EXPECT_NE(
        popup.find(ascii::utf8(ascii::BOX_ROUNDED_TOP_LEFT)),
        std::string::npos);
    for(int i = 0; i < 40; ++i)
        debug->sizePopupLines.push_back("  member " + std::to_string(i));
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_EQ(debug->sizePopupCursor, 1);
    EXPECT_EQ(debug->cursor[2], 0);
    sm.dispatch(keyCode(control::ControlKey::CTRL_K));
    EXPECT_EQ(debug->sizePopupCursor, 0);
    sm.dispatch(keyCode(navigation::NavigationKey::PAGE_DOWN));
    EXPECT_GT(debug->sizePopupOffset, 0);
    sm.dispatch('G');
    EXPECT_EQ(debug->sizePopupCursor, 41);
    sm.dispatch('g');
    EXPECT_EQ(debug->sizePopupCursor, 0);
    sm.dispatch('q');
    EXPECT_FALSE(debug->sizePopupOpen);
    EXPECT_EQ(debug->pane, 2);
    EXPECT_NE(sm.getState<DebuggerMode>(), nullptr);
    testing::internal::CaptureStdout();
    debug->draw(editor);
    const auto restored = testing::internal::GetCapturedStdout();
    EXPECT_EQ(restored.find(Terminal::ESC_CLEAR_SCREEN), std::string::npos);
    EXPECT_EQ(restored.find("Size and padding: item"), std::string::npos);
    // Delayed GDB field output must not reopen a dismissed popup.
    debug->applyMessage(
        "{\"layoutTitle\":\"Size and padding: "
        "item\",\"layoutAppend\":true,\"layoutLines\":[\"late field\"]}");
    EXPECT_FALSE(debug->sizePopupOpen);
    debug->applyMessage(
        "{\"layoutTitle\":\"Size and padding: "
        "item\",\"layoutLines\":[\"Storage: item\"]}");
    debug->applyMessage(
        "{\"layoutTitle\":\"Size and padding: "
        "item\",\"layoutAppend\":true,\"layoutLines\":[\"Padding: 6 bytes\"]}");
    EXPECT_EQ(debug->sizePopupLines.size(), 2u);
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_FALSE(debug->sizePopupOpen);
}

TEST(
    RealModeTransitionsTest,
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
    EXPECT_NE(
        full.find(ascii::utf8(ascii::BOX_ROUNDED_TOP_LEFT)),
        std::string::npos);
    EXPECT_NE(
        full.find(ascii::utf8(ascii::BOX_ROUNDED_BOTTOM_RIGHT)),
        std::string::npos);
    EXPECT_NE(
        full.find(ascii::utf8(ascii::BOX_LIGHT_VERTICAL)),
        std::string::npos);
    EXPECT_NE(
        full.find(ascii::utf8(ascii::BOX_LIGHT_HORIZONTAL)),
        std::string::npos);
    EXPECT_NE(full.find("n next (finish pending call)"), std::string::npos);
    EXPECT_NE(full.find("Variables"), std::string::npos);
    EXPECT_NE(full.find("Threads"), std::string::npos);
    EXPECT_NE(
        full.find(editor.theme.syntax(TOKEN_FUNCTION) + "count"),
        std::string::npos);
    EXPECT_NE(
        full.find(editor.theme.syntax(TOKEN_TYPE) + "int"),
        std::string::npos);
    EXPECT_NE(
        full.find(editor.theme.syntax(TOKEN_NUMBER) + "42"),
        std::string::npos);
    EXPECT_EQ(
        full.find(
            editor.theme.debuggerRow(Theme::DebuggerRow::Variables, true)),
        std::string::npos);
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

TEST(RealModeTransitionsTest, DebuggerVariablesUseThemeColorsWithoutStriping)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.screenRows = 32;
    editor.screenCols = 120;
    auto sm = makeMachine(editor, DebuggerMode{"app", false});
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    debug->state = "stopped";
    debug->pane = 2;
    debug->variables =
        {"name: string = \"Ada\"",
         "ready: bool = true",
         "callback: void * = 0x1000"};

    testing::internal::CaptureStdout();
    debug->draw(editor);
    const auto rendered = testing::internal::GetCapturedStdout();
    EXPECT_NE(
        rendered.find(editor.theme.syntax(TOKEN_STRING) + "\"Ada\""),
        std::string::npos);
    EXPECT_NE(
        rendered.find(editor.theme.syntax(TOKEN_KEYWORD) + "true"),
        std::string::npos);
    EXPECT_NE(
        rendered.find(editor.theme.syntax(TOKEN_COMMENT) + "0x1000"),
        std::string::npos);
    EXPECT_EQ(
        rendered.find(
            editor.theme.debuggerRow(Theme::DebuggerRow::Variables, true)),
        std::string::npos);
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
    EXPECT_NE(
        stopped.find(editor.theme.syntax(TOKEN_FUNCTION) + "count"),
        std::string::npos);
    EXPECT_NE(
        stopped.find(editor.theme.syntax(TOKEN_NUMBER) + "43"),
        std::string::npos);
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

TEST(
    RealModeTransitionsTest,
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
    return {
        {{1234, "debugger-cpp", "/tmp/My App/debugger-cpp"},
         {444, "compiler", "/bin/compiler"},
         {12, "helper-1234", "/bin/helper-1234"}},
        {}};
}
} // namespace

TEST(
    RealModeTransitionsTest,
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
    EXPECT_NE(
        popup.find(ascii::utf8(ascii::BOX_ROUNDED_TOP_LEFT)),
        std::string::npos);
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    EXPECT_TRUE(debug->attach);
    EXPECT_EQ(debug->target, "1234");
    EXPECT_FALSE(editor.commandPopupActive);
    EXPECT_TRUE(editor.commandPopupTitle.empty());
}

TEST(
    RealModeTransitionsTest,
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

TEST(
    RealModeTransitionsTest,
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
            return DebuggerProcessList{
                {},
                "Cannot list processes: access denied"};
        return recordedDebuggerProcesses();
    };
    auto sm = makeMachine(editor, std::move(command));
    EXPECT_TRUE(editor.commandPopupFiltered.empty());
    EXPECT_NE(
        editor.commandPopupEmptyMessage.find("access denied"),
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

TEST(
    RealModeTransitionsTest,
    DebuggerBreakpointMarkersTrackBackendLocationsAndDeletion)
{
    auto dir = make_temp_dir("uvim_breakpoints_");
    auto file = dir / "demo.cpp";
    write_file(
        file,
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
    debug->applyMessage(
        "{\"state\":\"stopped\",\"path\":" + path +
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
    EXPECT_NE(rendered.find(color::rgbBg(105, 38, 50)), std::string::npos);
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
    EXPECT_EQ(rendered.find(color::rgbBg(105, 38, 50)), std::string::npos);
    sm.dispatch(27);
    EXPECT_FALSE(debug->breakpointListOpen);
    EXPECT_NE(sm.getState<DebuggerMode>(), nullptr);
    std::filesystem::remove_all(dir);
}

TEST(
    RealModeTransitionsTest,
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
    write_file(
        main,
        "int main() {\n    int count = 42;\n    return count;\n}\n");
    write_file(
        helper,
        "inline int helper() {\n    int value = 7;\n    return value;\n}\n");
    {
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
        debug->applyMessage(
            "{\"state\":\"stopped\",\"path\":" + mainPath + ",\"line\":2}");
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
        EXPECT_EQ(
            std::filesystem::weakly_canonical(debug->sourcePath),
            std::filesystem::weakly_canonical(helper));
        EXPECT_EQ(debug->cursor[0], 2);
        EXPECT_EQ(debug->pane, 0);
        EXPECT_EQ(debug->state, "stopped");
        EXPECT_EQ(editor.currentBuffer, buffer);
        EXPECT_EQ(
            editor.currentBuffer->lines,
            (std::vector<std::string>{"untouched editor buffer"}));
        debug->applyMessage(
            "{\"state\":\"stopped\",\"path\":" + mainPath +
            ",\"line\":2,\"breakpoints\":[{\"id\":\"1\","
            "\"enabled\":true,\"locations\":[{\"path\":" +
            helperPath + ",\"line\":3}]}]}");
        EXPECT_EQ(
            std::filesystem::weakly_canonical(debug->sourcePath),
            std::filesystem::weakly_canonical(helper));
        EXPECT_EQ(debug->cursor[0], 2);
        EXPECT_TRUE(debug->breakpointLines.contains(3));
        testing::internal::CaptureStdout();
        debug->draw(editor);
        const auto rendered = testing::internal::GetCapturedStdout();
        EXPECT_EQ(rendered.find(">2 "), std::string::npos);
        EXPECT_NE(rendered.find("Source (browse)"), std::string::npos);
        debug->followExecutionOnStop = true;
        debug->applyMessage(
            "{\"state\":\"stopped\",\"path\":" + mainPath + ",\"line\":3}");
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
    }
    std::filesystem::remove_all(dir);
}
#endif

TEST(
    RealModeTransitionsTest,
    DebuggerBreakpointListFollowsFilesRowsAndMetadataRefreshes)
{
    auto dir = make_temp_dir("uvim_breakpoint_follow_");
    const auto original = dir / "running.cpp";
    const auto first = dir / "first.cpp";
    const auto second = dir / "second.cpp";
    write_file(original, "int main() {\nreturn 0;\n}\n");
    std::string code;
    for(int row = 1; row <= 80; ++row)
        code += "int first_" + std::to_string(row) + " = 1;\n";
    write_file(first, code);
    write_file(
        second,
        "one\ntwo\nthree\nFOUR_TARGET\nfive\nsix\nSEVEN_"
        "TARGET\neight\nnine\nTEN_TARGET\n");
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.screenRows = 32;
    editor.screenCols = 160;
    auto sm = makeMachine(editor, DebuggerMode{"app", false});
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    std::string runningPath, firstPath, secondPath, missingPath;
    json_utils::append_escaped(runningPath, original.string());
    json_utils::append_escaped(firstPath, first.string());
    json_utils::append_escaped(secondPath, second.string());
    json_utils::append_escaped(missingPath, (dir / "missing.cpp").string());
    debug->applyMessage(
        "{\"state\":\"stopped\",\"path\":" + runningPath +
        ",\"line\":2,\"frame\":7,\"variables\":[\"count = "
        "42\"],\"breakpoints\":["
        "{\"id\":\"1\",\"locations\":[{\"path\":" +
        firstPath +
        ",\"line\":3}]},"
        "{\"id\":\"2\",\"locations\":[{\"path\":" +
        firstPath +
        ",\"line\":60}]},"
        "{\"id\":\"3\",\"enabled\":false,\"locations\":[{\"path\":" +
        secondPath +
        ",\"line\":4}]},"
        "{\"id\":\"4\",\"pending\":true,\"locations\":[]},"
        "{\"id\":\"5\",\"locations\":[{\"path\":" +
        missingPath + ",\"line\":9}]}]}");
    debug->pane = 2;
    dispatch_command(sm, "bps");
    EXPECT_TRUE(debug->breakpointListOpen);
    EXPECT_TRUE(debug->sourcePinned);
    EXPECT_EQ(
        debug->sourcePath,
        EditorPathUtilities::resolveEditorPath(first.string()).string());
    EXPECT_EQ(debug->cursor[0], 2);
    EXPECT_EQ(debug->pane, 0);
    const auto firstSource = debug->source;
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_EQ(debug->cursor[0], 59);
    EXPECT_EQ(debug->source, firstSource);
    EXPECT_GT(debug->offset[0], 0);
    testing::internal::CaptureStdout();
    debug->draw(editor);
    const auto rendered = testing::internal::GetCapturedStdout();
    EXPECT_NE(rendered.find("+60 "), std::string::npos);
    EXPECT_NE(rendered.find("first_60"), std::string::npos);
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_EQ(
        debug->sourcePath,
        EditorPathUtilities::resolveEditorPath(second.string()).string());
    EXPECT_EQ(debug->cursor[0], 3);
    EXPECT_EQ(debug->source[3], "FOUR_TARGET");
    EXPECT_EQ(debug->frameIndex, 7);
    EXPECT_EQ(debug->executionPath, original.string());
    EXPECT_EQ(debug->executionLine, 2);
    EXPECT_EQ(debug->variables, (std::vector<std::string>{"count = 42"}));
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_FALSE(debug->breakpointSourceError.empty());
    EXPECT_EQ(debug->source[3], "FOUR_TARGET");
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_NE(
        debug->breakpointSourceError.find("Source unavailable"),
        std::string::npos);
    EXPECT_EQ(debug->cursor[0], 3);
    // Deleting a selected breakpoint follows the replacement list selection.
    debug->applyMessage(
        "{\"breakpoints\":[{\"id\":\"1\",\"locations\":[{\"path\":" +
        firstPath + ",\"line\":3}]},{\"id\":\"3\",\"locations\":[{\"path\":" +
        secondPath + ",\"line\":7}]}]}");
    EXPECT_EQ(debug->breakpointCursor, 1);
    EXPECT_EQ(debug->cursor[0], 6);
    EXPECT_EQ(debug->source[6], "SEVEN_TARGET");
    EXPECT_TRUE(debug->breakpointSourceError.empty());
    // Preserve the selected ID through reordering and follow its new row.
    debug->applyMessage(
        "{\"breakpoints\":[{\"id\":\"3\",\"locations\":[{\"path\":" +
        secondPath + ",\"line\":10}]},{\"id\":\"1\",\"locations\":[{\"path\":" +
        firstPath + ",\"line\":3}]}]}");
    EXPECT_EQ(debug->breakpointCursor, 0);
    EXPECT_EQ(debug->cursor[0], 9);
    sm.dispatch(27);
    EXPECT_FALSE(debug->breakpointListOpen);
    EXPECT_TRUE(debug->sourcePinned);
    EXPECT_EQ(debug->cursor[0], 9);
    debug->followExecutionOnStop = true;
    debug->applyMessage(
        "{\"state\":\"stopped\",\"path\":" + runningPath + ",\"line\":3}");
    EXPECT_FALSE(debug->sourcePinned);
    EXPECT_EQ(debug->sourcePath, original.string());
    EXPECT_EQ(debug->cursor[0], 2);
    std::filesystem::remove_all(dir);
}

TEST(
    RealModeTransitionsTest,
    DebuggerBreakpointListFollowsLocationsArrivingAfterOpening)
{
    auto dir = make_temp_dir("uvim_breakpoint_late_");
    const auto file = dir / "late.cpp";
    write_file(file, "one\ntwo\nthree\n");
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, DebuggerMode{"app", false});
    auto* debug = sm.getState<DebuggerMode>();
    debug->state = "stopped";
    dispatch_command(sm, "bps");
    EXPECT_TRUE(debug->breakpointListOpen);
    EXPECT_TRUE(debug->source.empty());
    std::string path;
    json_utils::append_escaped(path, file.string());
    debug->applyMessage(
        "{\"breakpoints\":[{\"id\":\"1\",\"locations\":[{\"path\":" + path +
        ",\"line\":3}]}]}");
    EXPECT_EQ(
        debug->sourcePath,
        EditorPathUtilities::resolveEditorPath(file.string()).string());
    EXPECT_EQ(debug->cursor[0], 2);
    sm.dispatch(keyCode(control::ControlKey::CTRL_K));
    EXPECT_EQ(debug->cursor[0], 2);
    std::filesystem::remove_all(dir);
}

TEST(
    RealModeTransitionsTest,
    DebuggerSourceReadersStripCRLFWithoutRemovingRealSpaces)
{
    auto dir = make_temp_dir("uvim_debugger_crlf_");
    const auto sourceFile = dir / "source.cpp";
    {
        std::ofstream file(sourceFile, std::ios::binary);
        ASSERT_TRUE(file.good());
        file << "one \r\ntwo\r\nthree\r\n";
    }
    const std::vector<std::string> expected{"one ", "two", "three"};
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, DebuggerMode{"app", false});
    auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    std::string path;
    json_utils::append_escaped(path, sourceFile.string());
    debug->applyMessage(
        "{\"state\":\"stopped\",\"path\":" + path + ",\"line\":2}");
    EXPECT_EQ(debug->source, expected);
    ASSERT_TRUE(debug->selectSourceFile(sourceFile.string(), 2));
    EXPECT_EQ(debug->source, expected);
    debug->applyMessage(
        "{\"breakpoints\":[{\"id\":\"1\",\"locations\":[{\"path\":" + path +
        ",\"line\":3}]}]}");
    dispatch_command(sm, "bps");
    EXPECT_EQ(debug->cursor[0], 2);
    EXPECT_EQ(debug->source, expected);
    sm.dispatch(27);
    sm.dispatch(':');
    for(char key : std::string("frame --"))
        sm.dispatch(key);
    debug->applyMessage(
        "{\"frames\":[{\"id\":\"0\",\"function\":\"main\",\"path\":" + path +
        ",\"line\":2,\"selected\":true}]}");
    EXPECT_TRUE(debug->framePreviewSourceAvailable);
    EXPECT_EQ(debug->framePreviewLines, expected);
    std::filesystem::remove_all(dir);
}

TEST(
    RealModeTransitionsTest,
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

TEST(RealModeTransitionsTest, DebuggerFrameAndProcessPickersAcceptRegexQueries)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    {
        auto sm = makeMachine(editor, DebuggerMode{"app", false});
        auto* debug = sm.getState<DebuggerMode>();
        debug->applyMessage("{\"state\":\"stopped\"}");
        sm.dispatch(':');
        for(char key : std::string("frame -- re:^(main|worker)$"))
            sm.dispatch(key);
        debug->applyMessage(
            "{\"frames\":[{\"id\":\"0\",\"function\":\"main\"},{\"id\":\"1\","
            "\"function\":\"other\"},{\"id\":\"2\",\"function\":\"worker\"}]}");
        ASSERT_EQ(debug->frameMatches.size(), 2u);
        for(int index : debug->frameMatches)
            EXPECT_NE(debug->stackFrames[index].symbol, "other");
    }
    DebuggerProcessPicker picker;
    picker.loader = []
    {
        return DebuggerProcessList{
            {{12, "cpp", "/tmp/cpp"},
             {34, "python", "/usr/bin/python"},
             {56, "mlang", "/tmp/mlang"}},
            {}};
    };
    auto ctx = createModeContext(&editor);
    EXPECT_TRUE(picker.update(ctx, ":dbgattach -- re:(cpp|mlang)$"));
    EXPECT_EQ(editor.commandPopupFiltered.size(), 2u);
    EXPECT_TRUE(picker.update(ctx, ":dbgattach -- re:["));
    EXPECT_TRUE(editor.commandPopupFiltered.empty());
    EXPECT_NE(
        editor.commandPopupEmptyMessage.find("Invalid regex"),
        std::string::npos);
    picker.close(ctx);
}

TEST(RealModeTransitionsTest, DebuggerProcessRegexShorthandFiltersWhileTyping)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    int loads = 0;
    CommandMode command;
    command.processPicker.loader = [&]
    {
        ++loads;
        return recordedDebuggerProcesses();
    };
    auto sm = makeMachine(editor, std::move(command));
    for(char key : std::string("dbgattach re:"))
        sm.dispatch(key);
    ASSERT_TRUE(editor.commandPopupActive);
    EXPECT_EQ(editor.commandPopupQuery, "re:");
    EXPECT_EQ(editor.commandPopupFiltered.size(), 3u);
    for(char key : std::string("^compiler$"))
        sm.dispatch(key);
    ASSERT_EQ(editor.commandPopupFiltered.size(), 1u);
    EXPECT_EQ(editor.commandPopupFiltered[0], 1);
    EXPECT_EQ(editor.commandBuffer, ":dbgattach re:^compiler$");
    EXPECT_EQ(loads, 1);
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_EQ(editor.commandBuffer, ":dbgattach re:^compiler$");
    sm.dispatch(keyCode(control::ControlKey::CTRL_U));
    EXPECT_EQ(editor.commandBuffer, ":dbgattach re:");
    EXPECT_EQ(editor.commandPopupFiltered.size(), 3u);
    for(char key : std::string("^[A-Za-z]+$"))
        sm.dispatch(key);
    ASSERT_EQ(editor.commandPopupFiltered.size(), 1u);
    EXPECT_EQ(editor.commandPopupFiltered[0], 1);
    sm.dispatch(keyCode(control::ControlKey::CTRL_U));
    for(char key : std::string("^[0-9]{4}$"))
        sm.dispatch(key);
    ASSERT_EQ(editor.commandPopupFiltered.size(), 1u);
    EXPECT_EQ(editor.commandPopupFiltered[0], 0);
    sm.dispatch(keyCode(control::ControlKey::TAB));
    EXPECT_FALSE(editor.commandPopupActive);
    EXPECT_EQ(editor.commandBuffer, ":dbgattach 1234");
}

TEST(
    RealModeTransitionsTest,
    DebuggerProcessRegexShorthandPreservesBackendAndRecoversFromInvalidClass)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    CommandMode command;
    command.initialInput = "dbgattach --backend gdb re:";
    command.processPicker.loader = recordedDebuggerProcesses;
    auto sm = makeMachine(editor, std::move(command));
    ASSERT_TRUE(editor.commandPopupActive);
    sm.dispatch('[');
    EXPECT_TRUE(editor.commandPopupFiltered.empty());
    EXPECT_NE(
        editor.commandPopupEmptyMessage.find("Invalid regex"),
        std::string::npos);
    for(char key : std::string("A-Za-z0-9]"))
        sm.dispatch(key);
    EXPECT_EQ(editor.commandPopupFiltered.size(), 3u);
    sm.dispatch(keyCode(control::ControlKey::CTRL_U));
    EXPECT_EQ(editor.commandBuffer, ":dbgattach --backend gdb re:");
    for(char key : std::string("^debugger-[a-z]+$"))
        sm.dispatch(key);
    ASSERT_EQ(editor.commandPopupFiltered.size(), 1u);
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    const auto* debug = sm.getState<DebuggerMode>();
    ASSERT_NE(debug, nullptr);
    EXPECT_EQ(debug->backend, "gdb");
    EXPECT_EQ(debug->target, "1234");
}

#else
using namespace uvim_test;

TEST(RealModeTransitionsTest, DebuggerCommandsAreUnavailableWhenDisabled)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});
    dispatch_command(sm, "dbgrun ./app");
    EXPECT_EQ(modeForState(sm.state()), NORMAL);
    dispatch_command(sm, "dbgattach 1234");
    EXPECT_EQ(modeForState(sm.state()), NORMAL);
    EXPECT_TRUE(editor.getCommandCompletions("dbg").empty());
}

#endif
