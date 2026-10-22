#include "assembly_compiler.h"
#include "executable_lookup.h"
#include "real_mode_test_utils.h"

#include <chrono>
#include <thread>

using namespace uvim_test;
using namespace std::chrono_literals;

namespace
{
bool waitFor(Editor& editor, const std::function<bool()>& ready)
{
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while(!ready())
    {
        editor.serviceTasks();
        if(std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(5ms);
    }
    return true;
}

std::string fakeCommand(const std::filesystem::path& root,
                        std::string flags = "")
{
    return "asm . --compiler \"" UVIM_TEST_ASSEMBLY_COMPILER
           "\" --uvim-test-dir \"" +
           root.string() + "\" " + flags;
}

class AssemblyViewTest : public testing::Test
{
protected:
    std::filesystem::path root = make_temp_dir("uvim_assembly_view_");
    Editor editor = Editor::createForTests();

    void SetUp() override
    {
        editor.createNewBuffer();
        set_buffer_filename(editor, (root / "source.cpp").string());
        *editor.lines = {"int changed(int x) { return x + 7; }"};
        *editor.dirty = true;
        editor.useSystemClipboard = false;
        editor.screenRows = 22;
        editor.screenCols = 100;
    }

    void TearDown() override
    {
        write_file(root / "release", "");
        EXPECT_TRUE(waitFor(editor, [&] { return editor.tasks.idle(); }));
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};
} // namespace

TEST(AssemblyArgumentsTest, PreservesCompilerFlagsAndQuotedPaths)
{
    assembly_compiler::Request request;
    std::string error;
    ASSERT_TRUE(assembly_compiler::parse(
        R"("my source.cpp" --compiler "C:\Program Files\LLVM\bin\clang++.exe" -O2 -I "include directory" -DVALUE=7)",
        request, error));
    EXPECT_EQ(request.path, "my source.cpp");
    EXPECT_EQ(request.compiler, R"(C:\Program Files\LLVM\bin\clang++.exe)");
    EXPECT_EQ(request.flags,
              (std::vector<std::string>{"-O2", "-I", "include directory",
                                        "-DVALUE=7"}));
    ASSERT_TRUE(assembly_compiler::parse("-O3 -I include", request, error));
    EXPECT_EQ(request.path, ".");
    EXPECT_EQ(request.flags,
              (std::vector<std::string>{"-O3", "-I", "include"}));
    ASSERT_TRUE(assembly_compiler::parse(". --raw -- -O2 -g", request, error));
    EXPECT_TRUE(request.raw);
    EXPECT_EQ(request.flags, (std::vector<std::string>{"-O2", "-g"}));
    EXPECT_FALSE(assembly_compiler::parse(". --compiler", request, error));
    EXPECT_FALSE(assembly_compiler::parse("\"unclosed", request, error));
}

TEST_F(AssemblyViewTest,
       CompilesSnapshotAndPassesArgumentsWithoutChangingBuffer)
{
    write_file(root / "source.cpp", "disk version must stay unchanged\n");
    auto* source = editor.currentBuffer;
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});
    const auto start = std::chrono::steady_clock::now();
    write_file(root / "hold", "");
    dispatch_command(sm, fakeCommand(root, "-O2 -I \"include directory\""));
    EXPECT_LT(std::chrono::steady_clock::now() - start, 500ms);
    ASSERT_STREQ(sm.currentStateName(), "ASM");
    auto* view = sm.getState<AssemblyMode>();
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(waitFor(editor, [&]
                        { return std::filesystem::exists(root / "started"); }));
    EXPECT_TRUE(view->content->compiling);
    write_file(root / "release", "");
    ASSERT_TRUE(waitFor(editor, [&] { return !view->content->compiling; }));
    ASSERT_TRUE(view->content->result.success)
        << view->content->result.diagnostics;
    const auto args = read_file(root / "arguments");
    EXPECT_NE(args.find("-O2\n-I\ninclude directory\n"), std::string::npos);
    EXPECT_EQ(args.find("-O0"), std::string::npos);
    EXPECT_NE(read_file(root / "source").find("return x + 7"),
              std::string::npos);
    EXPECT_EQ(editor.currentBuffer, source);
    EXPECT_EQ(editor.buffers.size(), 1u);
    EXPECT_TRUE(*editor.dirty);
    EXPECT_EQ(*editor.cursorX, 4);
    EXPECT_EQ(read_file(root / "source.cpp"),
              "disk version must stay unchanged\n");
    sm.dispatch('j');
    EXPECT_EQ(view->cursor, 1);
    sm.dispatch('k');
    EXPECT_EQ(view->cursor, 0);
    sm.dispatch('G');
    EXPECT_EQ(view->cursor,
              static_cast<int>(view->content->result.lines.size()) - 1);
    sm.dispatch('g');
    sm.dispatch('l');
    EXPECT_GT(view->horizontal, 0);
    sm.dispatch('h');
    EXPECT_EQ(view->horizontal, 0);
    sm.dispatch('q');
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    EXPECT_EQ(editor.currentBuffer, source);
    EXPECT_EQ(*editor.cursorX, 4);
}

TEST_F(AssemblyViewTest, DoubleDashOffersFlagsWithoutStartingCompilation)
{
    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch(':');
    const std::string input = fakeCommand(root) + "--";
    for(char c : input)
        sm.dispatch(c);
    ASSERT_STREQ(sm.currentStateName(), "COMMAND");
    ASSERT_TRUE(sm.getState<CommandMode>()->assemblyFlagPopup);
    EXPECT_TRUE(editor.commandPopupActive);
    EXPECT_TRUE(editor.tasks.idle());
    ASSERT_TRUE(editor.commandPopupSelection().has_value());
    EXPECT_EQ(*editor.commandPopupSelection(), "-O2");
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_EQ(*editor.commandPopupSelection(), "-O0");
    sm.dispatch(keyCode(control::ControlKey::CTRL_K));
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    EXPECT_STREQ(sm.currentStateName(), "COMMAND");
    EXPECT_FALSE(editor.commandPopupActive);
    EXPECT_TRUE(editor.tasks.idle());
    EXPECT_NE(editor.commandBuffer.find("-O2 "), std::string::npos);
    sm.dispatch(keyCode(control::ControlKey::ENTER));
    ASSERT_STREQ(sm.currentStateName(), "ASM");
    auto* view = sm.getState<AssemblyMode>();
    ASSERT_TRUE(waitFor(editor, [&] { return !view->content->compiling; }));
    EXPECT_TRUE(view->content->result.success)
        << view->content->result.diagnostics;
    EXPECT_EQ(view->content->result.options.find("-O0"), std::string::npos);
    EXPECT_NE(read_file(root / "arguments").find("-O2\n"), std::string::npos);
    sm.dispatch('q');
}

TEST_F(AssemblyViewTest, EscapeCancelsFlagPickerAndCommandWithoutCompiling)
{
    auto sm = makeMachine(editor, NormalMode{});
    const auto source = *editor.lines;
    sm.dispatch(':');
    for(char c : std::string("asm . --"))
        sm.dispatch(c);
    ASSERT_TRUE(editor.commandPopupActive);
    EXPECT_TRUE(sm.getState<CommandMode>()->assemblyFlagPopup);
    sm.dispatch(keyCode(control::ControlKey::ESC));
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    EXPECT_FALSE(editor.commandPopupActive);
    EXPECT_TRUE(editor.tasks.idle());
    EXPECT_TRUE(editor.commandBuffer.empty());
    EXPECT_EQ(*editor.lines, source);

    // Double dashes inside a quoted file name are literal, and full options
    // such as --compiler remain freely typeable after the picker appears.
    sm.dispatch(':');
    for(char c : std::string("asm \"name --"))
        sm.dispatch(c);
    EXPECT_FALSE(sm.getState<CommandMode>()->assemblyFlagPopup);
    sm.dispatch(keyCode(control::ControlKey::ESC));
    sm.dispatch(':');
    for(char c : std::string("asm . --compiler gcc -O2"))
        sm.dispatch(c);
    EXPECT_FALSE(sm.getState<CommandMode>()->assemblyFlagPopup);
    EXPECT_EQ(editor.commandBuffer, ":asm . --compiler gcc -O2");
    sm.dispatch(keyCode(control::ControlKey::ESC));
}

TEST_F(AssemblyViewTest, CanExitWhileCompilerIsBlocked)
{
    write_file(root / "hold", "");
    auto sm = makeMachine(editor, NormalMode{});
    dispatch_command(sm, fakeCommand(root));
    ASSERT_NE(sm.getState<AssemblyMode>(), nullptr);
    auto state = sm.getState<AssemblyMode>()->content;
    ASSERT_TRUE(waitFor(editor, [&]
                        { return std::filesystem::exists(root / "started"); }));
    const auto start = std::chrono::steady_clock::now();
    sm.dispatch(keyCode(control::ControlKey::ESC));
    EXPECT_LT(std::chrono::steady_clock::now() - start, 500ms);
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    EXPECT_FALSE(state->active);
    auto normal = makeMachine(editor, NormalMode{});
    normal.dispatch('i');
    normal.dispatch('X');
    EXPECT_EQ(editor.lines->front().front(), 'X');
    write_file(root / "release", "");
    ASSERT_TRUE(waitFor(editor, [&] { return editor.tasks.idle(); }));
    EXPECT_FALSE(state->result.success);
}

TEST_F(AssemblyViewTest, DisplaysCompilerErrorsAndMissingExecutable)
{
    auto sm = makeMachine(editor, NormalMode{});
    dispatch_command(sm, fakeCommand(root, "--uvim-test-error"));
    auto* view = sm.getState<AssemblyMode>();
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(waitFor(editor, [&] { return !view->content->compiling; }));
    EXPECT_FALSE(view->content->result.success);
    EXPECT_NE(view->content->result.diagnostics.find(
                  "compiler error: invalid source"),
              std::string::npos);
    testing::internal::CaptureStdout();
    view->draw(editor);
    const auto output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("compiler error: invalid source"), std::string::npos);
    sm.dispatch('q');
    dispatch_command(sm, "asm . --compiler \"" +
                             (root / "missing compiler").string() + "\"");
    view = sm.getState<AssemblyMode>();
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(waitFor(editor, [&] { return !view->content->compiling; }));
    EXPECT_NE(view->content->result.diagnostics.find("compiler not found"),
              std::string::npos);
    sm.dispatch('q');
}

TEST_F(AssemblyViewTest, RendersNumberedHighlightedRowsAndInspection)
{
    auto sm = makeMachine(editor, NormalMode{});
    dispatch_command(sm, fakeCommand(root));
    auto* view = sm.getState<AssemblyMode>();
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(waitFor(editor, [&] { return !view->content->compiling; }));
    ASSERT_TRUE(view->content->result.success);
    // Stable architecture-independent fixture for code colors and docs.
    view->content->result.lines = {"example:", "    movq %rdi, %rax",
                                   "    retq"};
    testing::internal::CaptureStdout();
    view->draw(editor);
    const auto frame = testing::internal::GetCapturedStdout();
    EXPECT_NE(frame.find("Assembly:"), std::string::npos);
    EXPECT_NE(frame.find("ASM"), std::string::npos);
    EXPECT_NE(frame.find(editor.theme.uiGutter() + ">1 "), std::string::npos);
    EXPECT_NE(frame.find(editor.theme.syntax(TOKEN_KEYWORD) + "movq"),
              std::string::npos);
    sm.dispatch('j');
    asm_documentation::setFetchOriginalDocs(false);
    set_env_var("UVIM_ASM_DOCS_CACHE_DIR", root.string());
    sm.dispatch('i');
    ASSERT_TRUE(view->inspect);
    ASSERT_TRUE(waitFor(editor, [&] { return !view->content->docsLoading; }));
#ifdef UVIM_ENABLE_ASM_DOCS
    EXPECT_NE(view->content->docsText.find("Copies the source operand"),
              std::string::npos);
    EXPECT_EQ(view->content->docsTitle, "mov (x86)");
    sm.dispatch('j');
    ASSERT_TRUE(waitFor(editor, [&] { return !view->content->docsLoading; }));
    EXPECT_EQ(view->content->docsTitle, "ret (x86)");
#else
    EXPECT_NE(view->content->docsText.find("not compiled"), std::string::npos);
#endif
    testing::internal::CaptureStdout();
    view->draw(editor);
    const auto inspected = testing::internal::GetCapturedStdout();
    EXPECT_NE(inspected.find("Inspect:"), std::string::npos);
    sm.dispatch('i');
    EXPECT_FALSE(view->inspect);
    sm.dispatch('i');
    sm.dispatch('q'); // Exits the view even when inspection is open.
    ASSERT_TRUE(waitFor(editor, [&] { return editor.tasks.idle(); }));
    unset_env_var("UVIM_ASM_DOCS_CACHE_DIR");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(AssemblyViewCompilerTest, CompilesDiskPathWithClangAndGccFlags)
{
    const auto root = make_temp_dir("uvim_asm_compiler_");
    write_file(root / "local.h", "#define VALUE 7\n");
    const auto source = root / "file with spaces.cpp";
    write_file(
        source,
        "#ifndef __OPTIMIZE__\n#error expected optimization\n#endif\n#include "
        "\"local.h\"\nint actual(int x) { return x + VALUE; }\n");
    int tested = 0;
    for(const std::string compiler : {"clang", "gcc"})
    {
        if(!executable_lookup::find(compiler == "clang" ? "clang++" : "g++")
                .found)
            continue;
        assembly_compiler::Request request;
        request.path = source.string();
        request.compiler = compiler;
        request.flags = {"-O2", "-DANOTHER_FLAG=1"};
        auto result = assembly_compiler::compile(request, [] { return false; });
        EXPECT_TRUE(result.success) << result.diagnostics;
        EXPECT_EQ(result.options, "-O2 -DANOTHER_FLAG=1");
        std::string assembly;
        for(const auto& line : result.lines)
            assembly += line + '\n';
        EXPECT_NE(assembly.find("actual"), std::string::npos);
        ++tested;
    }
    std::filesystem::remove_all(root);
    if(!tested)
        GTEST_SKIP() << "Clang/GCC not installed";
}

TEST(AssemblyViewStandaloneTest, ExplicitPathCanBeOpenedFromWelcomeWithoutCreatingBuffer)
{
    const auto root = make_temp_dir("uvim_asm_path_");
    const auto path = root / "file with spaces.c";
    write_file(path, "int from_disk(void) { return 3; }\n");
    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, WelcomeMode{});
    dispatch_command(sm, "asm \"" + path.string() +
                             "\" --compiler \"" UVIM_TEST_ASSEMBLY_COMPILER
                             "\" -O3");
    auto* view = sm.getState<AssemblyMode>();
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(waitFor(editor, [&] { return !view->content->compiling; }));
    EXPECT_TRUE(view->content->result.success)
        << view->content->result.diagnostics;
    EXPECT_FALSE(editor.hasBuffer());
    EXPECT_TRUE(editor.buffers.empty());
    sm.dispatch(keyCode(control::ControlKey::ESC));
    EXPECT_STREQ(sm.currentStateName(), "WELCOME");
    EXPECT_FALSE(editor.hasBuffer());
    std::filesystem::remove_all(root);
}
