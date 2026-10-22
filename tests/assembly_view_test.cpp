#include "assembly_compiler.h"
#include "executable_lookup.h"
#include "json_utils.h"
#include "real_mode_test_utils.h"

#include <chrono>
#include <iomanip>
#include <sstream>
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

void writeDatabase(const std::filesystem::path& path,
                   const std::filesystem::path& directory,
                   const std::string& file,
                   const std::vector<std::string>& arguments,
                   const std::string& command = {})
{
    json_utils::Document database(json_utils::kArrayType);
    auto& alloc = database.GetAllocator();
    json_utils::Value entry(json_utils::kObjectType);
    entry.AddMember("directory", directory.string().c_str(), alloc);
    entry.AddMember("file", file.c_str(), alloc);
    if(!arguments.empty())
    {
        json_utils::Value args(json_utils::kArrayType);
        for(const auto& arg : arguments)
            args.PushBack(arg.c_str(), alloc);
        entry.AddMember("arguments", std::move(args), alloc);
    }
    entry.AddMember("command", command.c_str(), alloc);
    database.PushBack(std::move(entry), alloc);
    std::filesystem::create_directories(path.parent_path());
    write_file(path, json_utils::stringify(database));
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
    ASSERT_TRUE(assembly_compiler::parse(". --split -O2", request, error));
    EXPECT_TRUE(request.split);
    EXPECT_EQ(request.flags, (std::vector<std::string>{"-O2"}));
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

TEST_F(AssemblyViewTest, InheritsMatchingBuildDatabaseAndPromptOverridesIt)
{
    const auto build = root / "build";
    const auto object = build / "original.o";
    const auto dependencies = build / "original.d";
    // An unrelated nearer database must not supply another file's flags.
    writeDatabase(root / "compile_commands.json", root, "other.cpp",
                  {UVIM_TEST_ASSEMBLY_COMPILER, "-DWRONG_FILE=1"});
    writeDatabase(build / "compile_commands.json", build, "../source.cpp",
                  {UVIM_TEST_ASSEMBLY_COMPILER, "-O0", "-std=c++17",
                   "-DVALUE=1", "-D", "KEEP=7", "-I", "../project include",
                   "-c", "../source.cpp", "-o", object.string(), "-MMD", "-MF",
                   dependencies.string(), "-MToriginal.o", "-MJfragment.json",
                   "--uvim-test-dir", root.string()},
                  "this command must be ignored in favor of arguments");
    write_file(object, "original object");
    write_file(dependencies, "original dependencies");
    auto sm = makeMachine(editor, NormalMode{});
    dispatch_command(sm, "asm . --split -O2 -std=c++23 -DVALUE=2");
    auto* view = sm.getState<AssemblyMode>();
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(waitFor(editor, [&] { return !view->content->compiling; }));
    ASSERT_TRUE(view->content->result.success)
        << view->content->result.diagnostics;
    EXPECT_EQ(view->content->result.compilationDatabase,
              (build / "compile_commands.json").string());
    const auto args = read_file(root / "arguments");
    EXPECT_EQ(args.find("-O0\n"), std::string::npos);
    EXPECT_EQ(args.find("-std=c++17\n"), std::string::npos);
    EXPECT_EQ(args.find("-DVALUE=1\n"), std::string::npos);
    EXPECT_NE(args.find("-O2\n-std=c++23\n-DVALUE=2\n"), std::string::npos);
    EXPECT_NE(args.find("-D\nKEEP=7\n"), std::string::npos);
    EXPECT_NE(args.find("-I\n../project include\n"), std::string::npos);
    EXPECT_EQ(args.find("-c\n"), std::string::npos);
    EXPECT_EQ(args.find("../source.cpp\n"), std::string::npos);
    EXPECT_EQ(args.find("-MMD\n"), std::string::npos);
    EXPECT_EQ(args.find("fragment.json"), std::string::npos);
    EXPECT_EQ(read_file(object), "original object");
    EXPECT_EQ(read_file(dependencies), "original dependencies");
    EXPECT_TRUE(
        std::filesystem::equivalent(read_file(root / "directory"), build));
    EXPECT_NE(read_file(root / "source").find("return x + 7"),
              std::string::npos);
    sm.dispatch('q');
}

TEST_F(AssemblyViewTest, ReadsQuotedCommandResponseFilesAndExplicitCompiler)
{
    const auto build = root / "build directory";
    std::filesystem::create_directories(build);
    std::ostringstream response;
    response << "-O1\n-DVALUE=1\n--uvim-test-dir\n"
             << std::quoted(root.string()) << '\n';
    write_file(build / "flags file.rsp", response.str());
    const auto database = root / "custom database" / "compile_commands.json";
    std::ostringstream command;
    command << "ccache " << std::quoted((build / "missing compiler").string())
            << " @\"flags file.rsp\" -c ../source.cpp -o\"ignored object.o\"";
    writeDatabase(database, build, "../source.cpp", {}, command.str());
    editor.clangdLspCompileCommandsDir = database.parent_path().string();
    auto sm = makeMachine(editor, NormalMode{});
    dispatch_command(sm, fakeCommand(root, "-U VALUE"));
    auto* view = sm.getState<AssemblyMode>();
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(waitFor(editor, [&] { return !view->content->compiling; }));
    ASSERT_TRUE(view->content->result.success)
        << view->content->result.diagnostics;
    EXPECT_EQ(view->content->result.compilationDatabase, database.string());
    EXPECT_EQ(view->content->result.compiler, UVIM_TEST_ASSEMBLY_COMPILER);
    const auto args = read_file(root / "arguments");
    EXPECT_NE(args.find("-O1\n"), std::string::npos);
    EXPECT_EQ(args.find("-std="), std::string::npos);
    EXPECT_EQ(args.find("-O0\n"), std::string::npos);
    EXPECT_EQ(args.find("-DVALUE=1\n"), std::string::npos);
    EXPECT_NE(args.find("-U\nVALUE\n"), std::string::npos);
    EXPECT_EQ(args.find("ignored object.o"), std::string::npos);
    sm.dispatch('q');
}

TEST_F(AssemblyViewTest,
       UnmatchedDatabaseFallsBackAndMalformedDatabaseReportsError)
{
    writeDatabase(root / "compile_commands.json", root, "other.cpp",
                  {"missing-compiler", "-DWRONG_FILE=1"});
    auto sm = makeMachine(editor, NormalMode{});
    dispatch_command(sm, fakeCommand(root));
    auto* view = sm.getState<AssemblyMode>();
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(waitFor(editor, [&] { return !view->content->compiling; }));
    EXPECT_TRUE(view->content->result.success);
    EXPECT_TRUE(view->content->result.compilationDatabase.empty());
    EXPECT_EQ(read_file(root / "arguments").find("WRONG_FILE"),
              std::string::npos);
    sm.dispatch('q');
    write_file(root / "compile_commands.json", "[");
    dispatch_command(sm, fakeCommand(root));
    view = sm.getState<AssemblyMode>();
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(waitFor(editor, [&] { return !view->content->compiling; }));
    EXPECT_FALSE(view->content->result.success);
    EXPECT_NE(
        view->content->result.diagnostics.find("invalid compilation database"),
        std::string::npos);
    sm.dispatch('q');
}

TEST_F(AssemblyViewTest, RealCompilerUsesDatabaseAndPromptIncludeDirectories)
{
    const auto compiler = executable_lookup::find("clang++");
    if(!compiler.found)
        GTEST_SKIP() << "Clang not installed";
    const auto build = root / "build";
    std::filesystem::create_directories(build);
    std::filesystem::create_directories(root / "db include");
    std::filesystem::create_directories(root / "user include");
    write_file(root / "db include" / "shared.h", "#define FROM_INCLUDE 1\n");
    write_file(root / "user include" / "shared.h", "#define FROM_INCLUDE 7\n");
    write_file(root / "local.h", "#define LOCAL 9\n");
    write_file(root / "db include" / "local.h", "#define LOCAL 0\n");
    write_file(build / "config.h", "#define CONFIGURED 1\n");
    writeDatabase(build / "compile_commands.json", build, "../source.cpp",
                  {compiler.path, "-std=c++11", "-O0", "-Werror", "-DVALUE=1",
                   "-I", "../db include", "-iquote", "../db include",
                   "-include", "config.h", "-c", "../source.cpp", "-o",
                   "original.o", "-MMD", "-MF", "original.d"});
    assembly_compiler::Request request;
    request.path = (root / "source.cpp").string();
    request.directory = root.string();
    request.sourceLines = std::vector<std::string>{
        "#include <shared.h>",
        "#include \"local.h\"",
        "#if !defined(__OPTIMIZE__) || __cplusplus < 202002L",
        "#error expected prompt optimization and C++20",
        "#endif",
        "static_assert(VALUE == 7 && FROM_INCLUDE == 7 && LOCAL == 9 && "
        "CONFIGURED == 1);",
        "int configured(int x) { return x + VALUE; }"};
    request.flags = {"-O2", "-std=c++20", "-DVALUE=7", "-I", "user include"};
    const auto result =
        assembly_compiler::compile(request, [] { return false; });
    ASSERT_TRUE(result.success) << result.diagnostics;
    EXPECT_EQ(result.compilationDatabase,
              (build / "compile_commands.json").string());
    EXPECT_EQ(result.options.find("-O0"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(build / "original.o"));
    EXPECT_FALSE(std::filesystem::exists(build / "original.d"));
    EXPECT_TRUE(std::any_of(result.sourceLineForAssembly.begin(),
                            result.sourceLineForAssembly.end(),
                            [](int line) { return line > 0; }));
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
        EXPECT_EQ(result.sourceLineForAssembly.size(), result.lines.size());
        EXPECT_TRUE(std::any_of(result.sourceLineForAssembly.begin(),
                                result.sourceLineForAssembly.end(),
                                [](int line) { return line > 0; }))
            << compiler << " produced no source mapping";
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

TEST_F(AssemblyViewTest, SplitLinksSourceAndAssemblyInBothDirections)
{
    *editor.lines = {"int example(int x) {", "    x += 1;", "    return x;",
                     "} // no instructions"};
    const auto snapshot = *editor.lines;
    *editor.cursorY = 0;
    auto sm = makeMachine(editor, NormalMode{});
    dispatch_command(sm, fakeCommand(root, "--split -O2"));
    auto* view = sm.getState<AssemblyMode>();
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(waitFor(editor, [&] { return !view->content->compiling; }));
    ASSERT_TRUE(view->content->result.success);
    EXPECT_EQ(view->content->result.sourceLines, snapshot);
    // Two instructions map to line 1; an inlined header instruction must not.
    EXPECT_EQ(view->content->result.sourceLineForAssembly,
              (std::vector<int>{0, 1, 2, 1, 0, 3}));
    testing::internal::CaptureStdout();
    view->draw(editor);
    const auto frame = testing::internal::GetCapturedStdout();
    EXPECT_TRUE(view->sourcePane);
    EXPECT_EQ(view->cursor, 1);
    EXPECT_NE(frame.find("Source [active]"), std::string::npos);
    EXPECT_NE(frame.find(editor.theme.sourceMapping()), std::string::npos);
    sm.dispatch('j');
    EXPECT_EQ(view->sourceCursor, 1);
    EXPECT_EQ(view->cursor, 2);
    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_L));
    EXPECT_FALSE(view->sourcePane);
    sm.dispatch('j'); // Another instruction associated with source line 1.
    EXPECT_EQ(view->sourceCursor, 0);
    EXPECT_EQ(view->cursor, 3);
    sm.dispatch('j'); // Header code has no correspondence in this source pane.
    EXPECT_EQ(view->content->result.sourceLineForAssembly[view->cursor], 0);
    sm.dispatch('j');
    EXPECT_EQ(view->sourceCursor, 2);
    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_H));
    EXPECT_TRUE(view->sourcePane);
    sm.dispatch('j'); // Source line without emitted instructions.
    EXPECT_EQ(view->sourceCursor, 3);
    EXPECT_EQ(view->cursor, 5);
    sm.dispatch('i');
    EXPECT_NE(view->content->docsText.find("No assembly instruction"),
              std::string::npos);
    sm.dispatch('i');
    sm.dispatch('v');
    EXPECT_FALSE(view->split);
    EXPECT_FALSE(view->sourcePane);
    sm.dispatch('v');
    EXPECT_TRUE(view->split);
    sm.dispatch('q');
    EXPECT_EQ(*editor.lines, snapshot);
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_EQ(editor.buffers.size(), 1u);
}

TEST_F(AssemblyViewTest,
       RawAssemblyRetainsSourceMappingsAndSplitScrollsBothPanes)
{
    editor.lines->assign(40, "int value;");
    auto sm = makeMachine(editor, NormalMode{});
    dispatch_command(sm, fakeCommand(root, "--split --raw"));
    auto* view = sm.getState<AssemblyMode>();
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(waitFor(editor, [&] { return !view->content->compiling; }));
    const auto& result = view->content->result;
    ASSERT_EQ(result.sourceLineForAssembly.size(), result.lines.size());
    for(size_t row = 0; row < result.lines.size(); ++row)
    {
        if(result.lines[row].find("movq") != std::string::npos)
            EXPECT_EQ(result.sourceLineForAssembly[row], 1);
        if(result.lines[row].starts_with(".loc"))
            EXPECT_EQ(result.sourceLineForAssembly[row], 0);
    }
    sm.dispatch('G');
    EXPECT_EQ(view->sourceCursor, 39);
    EXPECT_GT(view->sourceOffset, 0);
    sm.dispatch('g');
    EXPECT_EQ(view->sourceCursor, 0);
    EXPECT_EQ(view->sourceOffset, 0);
    sm.dispatch('l');
    EXPECT_GT(view->sourceHorizontal, 0);
    EXPECT_EQ(view->horizontal, 0);
    // Narrow terminals show the focused pane without drawing outside it.
    editor.screenCols = 30;
    testing::internal::CaptureStdout();
    view->draw(editor);
    const auto narrow = testing::internal::GetCapturedStdout();
    EXPECT_NE(narrow.find("Source [active]"), std::string::npos);
    sm.dispatch(keyCode(control::ControlKey::SHIFT_CTRL_L));
    sm.dispatch('G');
    EXPECT_GT(view->offset, 0);
    sm.dispatch(keyCode(control::ControlKey::ESC));
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(AssemblyViewStandaloneTest,
     ExplicitPathCanBeOpenedFromWelcomeWithoutCreatingBuffer)
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
