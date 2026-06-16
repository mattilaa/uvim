#include "real_mode_test_utils.h"

using namespace uvim_test;

TEST(RealModeTransitionsTest, EmitAsmCommandCreatesAssemblyBufferWithFlags)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"#ifndef __OPTIMIZE__",
                                   "#error expected optimization",
                                   "#endif",
                                   "int square(int value)",
                                   "{",
                                   "    return value * value;",
                                   "}"};
    set_buffer_filename(editor, "/tmp/uvim_emit_asm_test.cpp");
    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "emitasm -O2");

    ASSERT_TRUE(editor.currentBuffer);
    EXPECT_TRUE(text_utils::is_found(
        editor.currentBuffer->filename.find("uvim_emit_asm_test.cpp.s")));
    EXPECT_FALSE(editor.currentBuffer->dirty);
    EXPECT_TRUE(editor.isFileType<FileType::Asm>());
    std::string output;
    for(const std::string& line : editor.currentBuffer->lines)
    {
        output += line;
        output += '\n';
    }
    EXPECT_TRUE(text_utils::is_found(output.find("square")));
    EXPECT_TRUE(text_utils::is_not_found(output.find("uvim_emit_asm_anchor")));
    EXPECT_EQ(editor.statusMessage, "emitasm: wrote assembly buffer");
}

TEST(RealModeTransitionsTest, EmitAsmCommandAnchorsHeaderTypes)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"#include <string>",
                                   "struct HeaderType",
                                   "{",
                                   "    HeaderType() : value(\"ok\") {}",
                                   "    std::string value;",
                                   "};"};
    set_buffer_filename(editor, "/tmp/uvim_emit_asm_header_test.h");
    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "emitasm -O2");

    ASSERT_TRUE(editor.currentBuffer);
    std::string output;
    for(const std::string& line : editor.currentBuffer->lines)
    {
        output += line;
        output += '\n';
    }
    EXPECT_TRUE(text_utils::is_found(output.find("uvim_emit_asm_anchor")));
    EXPECT_TRUE(text_utils::is_found(
        editor.currentBuffer->filename.find("uvim_emit_asm_header_test.h.s")));
    EXPECT_TRUE(editor.isFileType<FileType::Asm>());
}

TEST(RealModeTransitionsTest, EmitAsmCommandAnchorsDeclarationOnlyCppSource)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"struct SomeStruct", "{",
                                   "    int value = 42;", "};"};
    set_buffer_filename(editor, "/tmp/uvim_emit_asm_struct_only.cpp");
    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "emitasm -O2");

    ASSERT_TRUE(editor.currentBuffer);
    std::string output;
    for(const std::string& line : editor.currentBuffer->lines)
    {
        output += line;
        output += '\n';
    }
    EXPECT_TRUE(text_utils::is_found(output.find("uvim_emit_asm_anchor")));
    EXPECT_TRUE(text_utils::is_found(editor.currentBuffer->filename.find(
        "uvim_emit_asm_struct_only.cpp.s")));
    EXPECT_TRUE(editor.isFileType<FileType::Asm>());
}

TEST(RealModeTransitionsTest, EmitAsmCommandAnchorsStandaloneFunction)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"static inline int add(int a, int b)", "{",
                                   "    return a + b;", "}"};
    set_buffer_filename(editor, "/tmp/uvim_emit_asm_function_only.cpp");
    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "emitasm -O2");

    ASSERT_TRUE(editor.currentBuffer);
    std::string output;
    for(const std::string& line : editor.currentBuffer->lines)
    {
        output += line;
        output += '\n';
    }
    EXPECT_TRUE(text_utils::is_found(output.find("add")));
    EXPECT_TRUE(
        text_utils::is_not_found(output.find("uvim_emit_asm_function_anchor")));
    EXPECT_TRUE(text_utils::is_not_found(output.find(".build_version")));
    EXPECT_TRUE(text_utils::is_found(editor.currentBuffer->filename.find(
        "uvim_emit_asm_function_only.cpp.s")));
    EXPECT_TRUE(editor.isFileType<FileType::Asm>());
}

TEST(RealModeTransitionsTest, EmitAsmCommandRawOutputKeepsClangAssembler)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int add(int a, int b)", "{",
                                   "    return a + b;", "}"};
    set_buffer_filename(editor, "/tmp/uvim_emit_asm_raw.cpp");
    auto sm = makeMachine(editor, NormalMode{});

    dispatch_command(sm, "emitasm --raw -O2");

    ASSERT_TRUE(editor.currentBuffer);
    std::string output;
    for(const std::string& line : editor.currentBuffer->lines)
    {
        output += line;
        output += '\n';
    }
    EXPECT_TRUE(text_utils::is_found(output.find("add")));
    EXPECT_TRUE(text_utils::is_found(output.find(".globl")) ||
                text_utils::is_found(output.find(".global")) ||
                text_utils::is_found(output.find("__TEXT,__text")) ||
                text_utils::is_found(output.find(".text")));
    EXPECT_TRUE(editor.isFileType<FileType::Asm>());
}

#ifdef UVIM_ENABLE_ASM_DOCS

TEST(RealModeTransitionsTest, GdOnX86AssemblyInstructionOpensDocs)
{
    auto docsRoot = make_temp_dir("uvim_asm_docs_");
    set_env_var("UVIM_ASM_DOCS_CACHE_DIR", docsRoot.string());

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    movq %rdi, %rax"};
    set_buffer_filename(editor, "/tmp/uvim_asm_doc_test.s");
    *editor.cursorY = 0;
    *editor.cursorX = 4;
    editor.goToDefinition();

    ASSERT_TRUE(editor.currentBuffer);
    EXPECT_TRUE(
        text_utils::is_found(editor.currentBuffer->filename.find("x86.md")));
    ASSERT_GE(*editor.cursorY, 0);
    ASSERT_LT(*editor.cursorY, (int)editor.currentBuffer->lines.size());
    EXPECT_EQ(editor.currentBuffer->lines[*editor.cursorY], "## mov");
    std::string output;
    for(const std::string& line : editor.currentBuffer->lines)
    {
        output += line;
        output += '\n';
    }
    EXPECT_TRUE(text_utils::is_found(
        output.find("Documentation: Copies the source operand")));
    EXPECT_TRUE(
        text_utils::is_found(editor.statusMessage.find("gd (asm x86)")));

    unset_env_var("UVIM_ASM_DOCS_CACHE_DIR");
}

TEST(RealModeTransitionsTest, GdOnAarch64AssemblyInstructionOpensDocs)
{
    auto docsRoot = make_temp_dir("uvim_asm_docs_");
    set_env_var("UVIM_ASM_DOCS_CACHE_DIR", docsRoot.string());

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    ldr x0, [sp]"};
    set_buffer_filename(editor, "/tmp/uvim_asm_doc_test.s");
    *editor.cursorY = 0;
    *editor.cursorX = 4;
    editor.goToDefinition();

    ASSERT_TRUE(editor.currentBuffer);
    EXPECT_TRUE(text_utils::is_found(
        editor.currentBuffer->filename.find("aarch64.md")));
    ASSERT_GE(*editor.cursorY, 0);
    ASSERT_LT(*editor.cursorY, (int)editor.currentBuffer->lines.size());
    EXPECT_EQ(editor.currentBuffer->lines[*editor.cursorY], "## ldr");
    EXPECT_TRUE(
        text_utils::is_found(editor.statusMessage.find("gd (asm aarch64)")));

    unset_env_var("UVIM_ASM_DOCS_CACHE_DIR");
}

TEST(RealModeTransitionsTest, GdOnAssemblyInstructionCanFetchOriginalDocs)
{
#ifdef _WIN32
    GTEST_SKIP() << "fake curl PATH test is POSIX-only";
#else
    auto root = make_temp_dir("uvim_asm_docs_fetch_");
    auto docsRoot = root / "cache";
    auto binDir = root / "bin";
    std::filesystem::create_directories(binDir);
    auto curlPath = binDir / "curl";
    write_file(curlPath,
               "#!/bin/sh\n"
               "printf '%s\\n' '{\"html\":\"<p>Compiler Explorer move "
               "documentation.</p>\",\"tooltip\":\"Move data\","
               "\"url\":\"https://www.felixcloutier.com/x86/mov\"}'\n");
    std::filesystem::permissions(curlPath,
                                 std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add);

    const char* oldPathEnv = std::getenv("PATH");
    std::string oldPath = oldPathEnv ? oldPathEnv : "";
    set_env_var("PATH", binDir.string() + ":" + oldPath);
    set_env_var("UVIM_ASM_DOCS_CACHE_DIR", docsRoot.string());
    asm_documentation::setFetchOriginalDocs(true);

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    movq %rdi, %rax"};
    set_buffer_filename(editor, "/tmp/uvim_asm_doc_fetch_test.s");
    *editor.cursorY = 0;
    *editor.cursorX = 4;
    editor.goToDefinition();

    ASSERT_TRUE(editor.currentBuffer);
    EXPECT_TRUE(text_utils::is_found(editor.currentBuffer->filename.find(
        "fetched/compiler-explorer/x86/mov.md")));
    std::string output;
    for(const std::string& line : editor.currentBuffer->lines)
    {
        output += line;
        output += '\n';
    }
    EXPECT_TRUE(
        text_utils::is_found(output.find("Compiler Explorer documentation:")));
    EXPECT_TRUE(text_utils::is_found(
        output.find("Compiler Explorer move documentation.")));
    EXPECT_TRUE(text_utils::is_found(
        output.find("API: https://godbolt.org/api/asm/amd64/mov")));

    asm_documentation::setFetchOriginalDocs(false);
    set_env_var("PATH", oldPath);
    unset_env_var("UVIM_ASM_DOCS_CACHE_DIR");
#endif
}

TEST(RealModeTransitionsTest, LeaderGaShowsAssemblyDocsPopup)
{
    auto docsRoot = make_temp_dir("uvim_asm_docs_popup_");
    set_env_var("UVIM_ASM_DOCS_CACHE_DIR", docsRoot.string());
    asm_documentation::setFetchOriginalDocs(false);

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    movq %rdi, %rax"};
    set_buffer_filename(editor, "/tmp/uvim_asm_doc_popup_test.s");
    *editor.cursorY = 0;
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    Terminal::unreadKey('a');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_TRUE(editor.symbolPopupModal);
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_NE(editor.symbolPopupText.find("## mov"), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("Documentation: Copies the source"),
              std::string::npos);

    unset_env_var("UVIM_ASM_DOCS_CACHE_DIR");
}

TEST(RealModeTransitionsTest, LeaderGaAssemblyDocsPopupScrollsAndCloses)
{
    auto docsRoot = make_temp_dir("uvim_asm_docs_popup_scroll_");
    set_env_var("UVIM_ASM_DOCS_CACHE_DIR", docsRoot.string());
    asm_documentation::setFetchOriginalDocs(false);

    Editor editor = Editor::createForTests();
    editor.screenRows = 5;
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"    movq %rdi, %rax"};
    set_buffer_filename(editor, "/tmp/uvim_asm_doc_popup_scroll_test.s");
    *editor.cursorY = 0;
    *editor.cursorX = 4;
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(keyCode(control::ControlKey::SPACE));
    Terminal::unreadKey('a');
    sm.dispatch('g');
    ASSERT_TRUE(editor.symbolPopupActive);

    editor.needsFullRedraw = false;
    sm.dispatch('j');
    EXPECT_GT(editor.symbolPopupScroll, 0);
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_TRUE(editor.needsFullRedraw);

    sm.dispatch('q');
    EXPECT_FALSE(editor.symbolPopupActive);
    EXPECT_FALSE(editor.symbolPopupModal);

    unset_env_var("UVIM_ASM_DOCS_CACHE_DIR");
}
#endif
