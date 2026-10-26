#include "editor.h"
#include "syntax_highlighter.h"
#include "terminal.h"
#include "text_utils.h"
#include "widgets/completion_popup.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <thread>

namespace
{
bool waitForHeaderIndex(Editor& editor)
{
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while(!editor.syntaxHighlighter->isCppMemberIndexLoaded())
    {
        if(std::chrono::steady_clock::now() >= deadline)
            return false;
        editor.serviceTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

void setupEditorBuffer(Editor& editor)
{
    editor.buffers.push_back(std::make_unique<Buffer>());
    editor.currentBufferIndex = 0;
    editor.currentBuffer = editor.buffers[0].get();
    editor.lines = &editor.currentBuffer->lines;
    editor.filename = &editor.currentBuffer->filename;
    editor.dirty = &editor.currentBuffer->dirty;
    editor.cursorX = &editor.currentBuffer->cursorX;
    editor.cursorY = &editor.currentBuffer->cursorY;
    editor.wantedX = &editor.currentBuffer->wantedX;
    editor.offsetX = &editor.currentBuffer->offsetX;
    editor.offsetY = &editor.currentBuffer->offsetY;
}

std::filesystem::path makeTempRoot()
{
    auto base = std::filesystem::temp_directory_path();
    auto stamp = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    auto root = base / ("uvim_syntax_test_" + stamp);
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    return root;
}

bool hasTokenAt(
    const std::vector<Token>& tokens,
    int start,
    int length,
    TokenType type)
{
    for(const auto& token : tokens)
    {
        if(token.start == start && token.length == length && token.type == type)
            return true;
    }
    return false;
}

bool hasTokenType(const std::vector<Token>& tokens, TokenType type)
{
    for(const auto& token : tokens)
    {
        if(token.type == type)
            return true;
    }
    return false;
}
} // namespace

TEST(SyntaxHighlighterTest, HighlightsImplicitMembersInCppMethodDefinition)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);

    auto root = makeTempRoot();
    {
        std::ofstream header(root / "foo.h");
        header << "class Foo { public: int bar; void method(); };\n";
    }

    editor.setProjectRoot(root.string());
    *editor.filename = (root / "foo.cpp").string();
    editor.currentBuffer->lines = {"void Foo::method() {", "    bar = 1;", "}"};
    editor.syntaxCppHighlightImplicitMembers = true;
    editor.syntaxHighlighter->ensureCppMemberIndexLoaded();
    ASSERT_TRUE(waitForHeaderIndex(editor));

    std::string output;
    editor.renderLineWithSyntax(
        output,
        editor.currentBuffer->lines[1],
        0,
        (int)editor.currentBuffer->lines[1].size(),
        1);

    const std::string memberColor = editor.theme.syntax(TOKEN_MEMBER);
    EXPECT_TRUE(text_utils::is_found(output.find(memberColor)));
}

TEST(SyntaxHighlighterTest, HighlightsMemberDeclarationsInHeader)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);

    auto root = makeTempRoot();
    editor.setProjectRoot(root.string());
    *editor.filename = (root / "foo.h").string();
    editor.currentBuffer->lines = {"class Foo {", "public:", "int bar;", "};"};

    std::string output;
    editor.renderLineWithSyntax(
        output,
        editor.currentBuffer->lines[2],
        0,
        (int)editor.currentBuffer->lines[2].size(),
        2);

    const std::string memberColor = editor.theme.syntax(TOKEN_MEMBER);
    EXPECT_TRUE(text_utils::is_found(output.find(memberColor)));
}

TEST(SyntaxHighlighterTest, VisualSelectionKeepsUtf8GlyphBytesContiguous)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.cpp";
    editor.currentBuffer->filename = *editor.filename;
    editor.currentBuffer->fileTypeCacheValid = false;

    const std::string line =
        "std::cout << \"┌──────────────────────────────────┐\\n\";";
    editor.currentBuffer->lines = {line};
    editor.currentMode = VISUAL;
    editor.currentBuffer->visualStartY = 0;
    editor.currentBuffer->visualEndY = 0;
    editor.currentBuffer->visualStartX = (int)line.find("┌");
    editor.currentBuffer->visualEndX = (int)line.find("┐");
    *editor.cursorY = 0;
    *editor.cursorX = editor.currentBuffer->visualStartX;

    std::string output;
    editor.renderLineWithSyntax(output, line, 0, (int)line.size(), 0);

    EXPECT_TRUE(text_utils::is_found(output.find("┌")));
    EXPECT_TRUE(text_utils::is_found(output.find("─")));
    EXPECT_TRUE(text_utils::is_found(output.find("┐")));
}

TEST(SyntaxHighlighterTest, HighlightsAssemblyKeywordsAndRegisters)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.s";
    editor.currentBuffer->filename = *editor.filename;
    editor.currentBuffer->fileTypeCacheValid = false;
    editor.currentBuffer->lines = {"movq %rdi, %rax", "ldr x0, [sp]"};

    bool inBlockComment = false;
    bool inTomlMultiline = false;
    bool inMarkupFence = false;
    char tomlQuote = 0;
    char markupFenceChar = 0;
    const std::string line = "movq %rdi, %rax";
    const auto tokens = editor.syntaxHighlighter->tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    EXPECT_TRUE(hasTokenAt(tokens, 0, 4, TOKEN_KEYWORD));
    EXPECT_TRUE(hasTokenAt(tokens, 6, 3, TOKEN_TYPE));
    EXPECT_TRUE(hasTokenAt(tokens, 12, 3, TOKEN_TYPE));

    const std::string aarch64Line = "ldr x0, [sp]\n";
    const auto aarch64Tokens = editor.syntaxHighlighter->tokenizeLine(
        aarch64Line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    EXPECT_TRUE(hasTokenAt(aarch64Tokens, 0, 3, TOKEN_KEYWORD));
    EXPECT_TRUE(hasTokenAt(aarch64Tokens, 4, 2, TOKEN_TYPE));
    EXPECT_TRUE(hasTokenAt(aarch64Tokens, 9, 2, TOKEN_TYPE));

    std::string rendered;
    editor.renderLineWithSyntax(
        rendered,
        editor.currentBuffer->lines[0],
        0,
        (int)editor.currentBuffer->lines[0].size(),
        0);

    EXPECT_TRUE(
        text_utils::is_found(
            rendered.find(editor.theme.syntax(TOKEN_KEYWORD))));
}

TEST(SyntaxHighlighterTest, HighlightsFlexAndBisonDirectives)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/scanner.l";
    editor.currentBuffer->filename = *editor.filename;
    editor.currentBuffer->fileTypeCacheValid = false;

    bool inBlockComment = false;
    bool inTomlMultiline = false;
    bool inMarkupFence = false;
    char tomlQuote = 0;
    char markupFenceChar = 0;

    auto flexTokens = editor.syntaxHighlighter->tokenizeLine(
        "%option noyywrap",
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);
    EXPECT_TRUE(hasTokenAt(flexTokens, 0, 7, TOKEN_KEYWORD));

    *editor.filename = "/tmp/parser.y";
    editor.currentBuffer->filename = *editor.filename;
    editor.currentBuffer->fileTypeCacheValid = false;
    auto bisonTokens = editor.syntaxHighlighter->tokenizeLine(
        "%token NUMBER",
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);
    EXPECT_TRUE(hasTokenAt(bisonTokens, 0, 6, TOKEN_KEYWORD));

    auto separatorTokens = editor.syntaxHighlighter->tokenizeLine(
        "%%",
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);
    EXPECT_TRUE(hasTokenAt(separatorTokens, 0, 2, TOKEN_KEYWORD));
}

TEST(SyntaxHighlighterTest, HighlightsFormatterConfigsLikeYaml)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/.mlang-format";
    editor.currentBuffer->filename = *editor.filename;
    editor.currentBuffer->fileTypeCacheValid = false;

    bool inBlockComment = false;
    bool inTomlMultiline = false;
    bool inMarkupFence = false;
    char tomlQuote = 0;
    char markupFenceChar = 0;

    const std::string indentLine = "ContinuationIndentWidth: 8";
    const auto indentTokens = editor.syntaxHighlighter->tokenizeLine(
        indentLine,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);
    EXPECT_TRUE(hasTokenAt(indentTokens, 0, 23, TOKEN_KEYWORD));
    EXPECT_TRUE(hasTokenAt(indentTokens, 23, 1, TOKEN_OPERATOR));

    const std::string styleLine = "BasedOnStyle: \"LLVM\"";
    const auto styleTokens = editor.syntaxHighlighter->tokenizeLine(
        styleLine,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);
    EXPECT_TRUE(hasTokenAt(styleTokens, 0, 12, TOKEN_KEYWORD));
    EXPECT_TRUE(hasTokenAt(styleTokens, 14, 6, TOKEN_STRING));

    const std::string boolLine = "IndentFunctionSignatureClosingParen: true";
    const auto boolTokens = editor.syntaxHighlighter->tokenizeLine(
        boolLine,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);
    EXPECT_TRUE(hasTokenAt(boolTokens, 0, 35, TOKEN_KEYWORD));
    EXPECT_TRUE(hasTokenAt(boolTokens, 37, 4, TOKEN_KEYWORD));

    const std::string commentLine = "// ignored config comment";
    const auto commentTokens = editor.syntaxHighlighter->tokenizeLine(
        commentLine,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);
    EXPECT_TRUE(hasTokenAt(commentTokens, 0, 25, TOKEN_COMMENT));

    *editor.filename = "/tmp/.clang-format";
    editor.currentBuffer->filename = *editor.filename;
    editor.currentBuffer->fileTypeCacheValid = false;
    const auto clangFormatTokens = editor.syntaxHighlighter->tokenizeLine(
        styleLine,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);
    EXPECT_TRUE(hasTokenAt(clangFormatTokens, 0, 12, TOKEN_KEYWORD));
    EXPECT_TRUE(hasTokenAt(clangFormatTokens, 14, 6, TOKEN_STRING));
}

TEST(SyntaxHighlighterTest, DoesNotHighlightImplicitMembersWhenDisabled)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);

    auto root = makeTempRoot();
    {
        std::ofstream header(root / "foo.h");
        header << "class Foo { public: int bar; void method(); };\n";
    }

    editor.setProjectRoot(root.string());
    *editor.filename = (root / "foo.cpp").string();
    editor.currentBuffer->lines = {"void Foo::method() {", "    bar = 1;", "}"};
    editor.syntaxCppHighlightImplicitMembers = false;

    std::string output;
    editor.renderLineWithSyntax(
        output,
        editor.currentBuffer->lines[1],
        0,
        (int)editor.currentBuffer->lines[1].size(),
        1);

    const std::string memberColor = editor.theme.syntax(TOKEN_MEMBER);
    EXPECT_TRUE(text_utils::is_not_found(output.find(memberColor)));
}

TEST(SyntaxHighlighterTest, HighlightsTemplateParamsSingleType)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.cpp";

    const std::string line = "template <Foo> struct Bar {};";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar,
        false,
        false,
        true);

    int fooPos = (int)line.find("Foo");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(fooPos)));
    EXPECT_TRUE(hasTokenAt(tokens, fooPos, 3, TOKEN_TYPE));
}

TEST(SyntaxHighlighterTest, HighlightsTemplateParamsNestedTypes)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.cpp";

    const std::string line = "template <Foo, Bar<Baz>> struct Qux {};";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int fooPos = (int)line.find("Foo");
    int barPos = (int)line.find("Bar");
    int bazPos = (int)line.find("Baz");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(fooPos)));
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(barPos)));
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(bazPos)));
    EXPECT_TRUE(hasTokenAt(tokens, fooPos, 3, TOKEN_TYPE));
    EXPECT_TRUE(hasTokenAt(tokens, barPos, 3, TOKEN_TYPE));
    EXPECT_TRUE(hasTokenAt(tokens, bazPos, 3, TOKEN_TYPE));
}

TEST(SyntaxHighlighterTest, HighlightsCppPreprocessorLine)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.cpp";

    const std::string line = "#include <vector>";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    ASSERT_FALSE(tokens.empty());
    EXPECT_EQ(tokens[0].type, TOKEN_PREPROCESSOR);
    EXPECT_EQ(tokens[0].start, 0);
    EXPECT_EQ(tokens[0].length, (int)line.size());
}

TEST(SyntaxHighlighterTest, HighlightsCppBranchPredictionAttributes)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.cpp";

    const std::string line = "if (ready) [[likely]]; else [[unlikely]] return;";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    const auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    const int likelyPos = (int)line.find("likely");
    const int unlikelyPos = (int)line.find("unlikely");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(likelyPos)));
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(unlikelyPos)));
    EXPECT_TRUE(hasTokenAt(tokens, likelyPos, 6, TOKEN_KEYWORD));
    EXPECT_TRUE(hasTokenAt(tokens, unlikelyPos, 8, TOKEN_KEYWORD));
}

TEST(SyntaxHighlighterTest, HighlightsQualifiedTypeAfterScope)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.cpp";

    const std::string line = "std::vector<int> v;";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int vecPos = (int)line.find("vector");
    int stdPos = (int)line.find("std");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(vecPos)));
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(stdPos)));
    EXPECT_TRUE(hasTokenAt(tokens, stdPos, 3, TOKEN_NAMESPACE_1));
    EXPECT_TRUE(hasTokenAt(tokens, vecPos, 6, TOKEN_NAMESPACE_4));
}

TEST(SyntaxHighlighterTest, HighlightsCppQualifiedNamespaceSegments)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.cpp";

    const std::string line = "std::algorithm::fft(values);";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    EXPECT_TRUE(hasTokenAt(tokens, 0, 3, TOKEN_NAMESPACE_1));
    EXPECT_TRUE(hasTokenAt(tokens, 3, 2, TOKEN_OPERATOR));
    EXPECT_TRUE(hasTokenAt(tokens, 5, 9, TOKEN_NAMESPACE_2));
    EXPECT_TRUE(hasTokenAt(tokens, 14, 2, TOKEN_OPERATOR));
    EXPECT_TRUE(hasTokenAt(tokens, 16, 3, TOKEN_NAMESPACE_4));
}

TEST(SyntaxHighlighterTest, HighlightsCppStaticCallQualifiedSegments)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.cpp";

    const std::string line = "SomeObject::someFunction();";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    EXPECT_TRUE(hasTokenAt(tokens, 0, 10, TOKEN_NAMESPACE_1));
    EXPECT_TRUE(hasTokenAt(tokens, 10, 2, TOKEN_OPERATOR));
    EXPECT_TRUE(hasTokenAt(tokens, 12, 12, TOKEN_NAMESPACE_4));
}

TEST(SyntaxHighlighterTest, CppSemanticTokensPreserveQualifiedSegments)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.cpp";
    editor.syntaxCppSemanticTokens = true;

    const std::string line = "namespace::SomeClass value;";
    editor.currentBuffer->lines = {line};
    editor.currentBuffer->lspSemanticTokens.resize(1);
    editor.currentBuffer->lspSemanticTokensValid = true;

    int someClassPos = (int)line.find("SomeClass");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(someClassPos)));
    editor.currentBuffer->lspSemanticTokens[0].push_back(
        {someClassPos, 9, "class", false, false});

    std::string output;
    editor.renderLineWithSyntax(output, line, 0, (int)line.size(), 0);

    const std::string namespaceColor = editor.theme.syntax(TOKEN_NAMESPACE_4);
    EXPECT_TRUE(
        text_utils::is_found(output.find(namespaceColor + "SomeClass")));
}

TEST(SyntaxHighlighterTest, SemanticTokensDoNotOverrideComments)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.cpp";
    editor.syntaxCppSemanticTokens = true;

    const std::string line = "/// value type mentioned in documentation";
    editor.currentBuffer->lines = {line};
    editor.currentBuffer->lspSemanticTokens.resize(1);
    editor.currentBuffer->lspSemanticTokensValid = true;

    const int valuePos = (int)line.find("value");
    const int typePos = (int)line.find("type");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(valuePos)));
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(typePos)));
    editor.currentBuffer->lspSemanticTokens[0].push_back(
        {valuePos, 5, "variable", false, false});
    editor.currentBuffer->lspSemanticTokens[0].push_back(
        {typePos, 4, "type", false, false});

    std::string output;
    editor.renderLineWithSyntax(output, line, 0, (int)line.size(), 0);

    EXPECT_EQ(
        output,
        editor.theme.syntax(TOKEN_COMMENT) + line + editor.theme.baseFg());
}

TEST(SyntaxHighlighterTest, SemanticTokensDoNotOverrideLexicalKeywords)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.cpp";
    editor.syntaxCppSemanticTokens = true;

    const std::string line = "let value = 1;";
    editor.currentBuffer->lines = {line};
    editor.currentBuffer->lspSemanticTokens.resize(1);
    editor.currentBuffer->lspSemanticTokensValid = true;
    editor.currentBuffer->lspSemanticTokens[0].push_back(
        {0, 3, "variable", false, false});

    std::string output;
    editor.renderLineWithSyntax(output, line, 0, (int)line.size(), 0);

    EXPECT_TRUE(
        text_utils::is_found(
            output.find(editor.theme.syntax(TOKEN_KEYWORD) + "let")));
}

TEST(SyntaxHighlighterTest, HighlightsOptionalAndProjectTypes)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.cpp";
    editor.syntaxCppHighlightTypeNames = true;

    const std::string line = "std::optional<LspDiagnosticSummary> diag;";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int optionalPos = (int)line.find("optional");
    int summaryPos = (int)line.find("LspDiagnosticSummary");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(optionalPos)));
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(summaryPos)));
    EXPECT_TRUE(hasTokenAt(tokens, optionalPos, 8, TOKEN_NAMESPACE_4));
    EXPECT_TRUE(hasTokenAt(tokens, summaryPos, 20, TOKEN_TYPE));
}

TEST(SyntaxHighlighterTest, UsesSemanticTokensForCppTypes)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.cpp";
    editor.syntaxCppSemanticTokens = true;

    const std::string line =
        "std::unique_ptr<SyntaxHighlighter> syntaxHighlighter;";
    editor.currentBuffer->lines = {line};
    editor.currentBuffer->lspSemanticTokens.resize(1);
    editor.currentBuffer->lspSemanticTokensValid = true;

    int uniquePos = (int)line.find("unique_ptr");
    int typePos = (int)line.find("SyntaxHighlighter");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(uniquePos)));
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(typePos)));

    editor.currentBuffer->lspSemanticTokens[0].push_back(
        {uniquePos, 10, "type", false, false});
    editor.currentBuffer->lspSemanticTokens[0].push_back(
        {typePos, 17, "class", false, false});

    std::string output;
    editor.renderLineWithSyntax(output, line, 0, (int)line.size(), 0);

    const std::string typeColor = editor.theme.syntax(TOKEN_TYPE);
    EXPECT_TRUE(text_utils::is_found(output.find(typeColor)));
}

TEST(SyntaxHighlighterTest, UsesSemanticTokensForCppMembers)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.cpp";
    editor.syntaxCppSemanticTokens = true;
    editor.syntaxCppMemberToken = TOKEN_MEMBER;

    const std::string line =
        "std::vector<int> completions; completions.push_back(1);";
    editor.currentBuffer->lines = {line};
    editor.currentBuffer->lspSemanticTokens.resize(1);
    editor.currentBuffer->lspSemanticTokensValid = true;

    int declPos = (int)line.find("completions");
    int usePos = (int)line.find("completions", declPos + 1);
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(declPos)));
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(usePos)));

    editor.currentBuffer->lspSemanticTokens[0].push_back(
        {declPos, 11, "variable", true, false});
    editor.currentBuffer->lspSemanticTokens[0].push_back(
        {usePos, 11, "variable", false, false});

    std::string output;
    editor.renderLineWithSyntax(output, line, 0, (int)line.size(), 0);

    const std::string memberColor = editor.theme.syntax(TOKEN_MEMBER);
    EXPECT_TRUE(text_utils::is_found(output.find(memberColor + "completions")));
}

TEST(SyntaxHighlighterTest, SemanticTokensRespectLocalAndMemberColors)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.cpp";
    editor.syntaxCppSemanticTokens = true;
    editor.syntaxCppLocalToken = TOKEN_NORMAL;
    editor.syntaxCppMemberToken = TOKEN_TYPE;

    const std::string line =
        "Foo member; void f(Foo param){ Foo local; member=local; "
        "this->member=local; local.member=1; }";
    editor.currentBuffer->lines = {line};
    editor.currentBuffer->lspSemanticTokens.resize(1);
    editor.currentBuffer->lspSemanticTokensValid = true;

    auto addToken = [&](std::string_view text,
                        std::string_view tokenType,
                        bool isDecl,
                        bool isDef)
    {
        int pos = (int)line.find(std::string(text));
        ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(pos)));
        editor.currentBuffer->lspSemanticTokens[0].push_back(
            {pos, (int)text.size(), std::string(tokenType), isDecl, isDef});
    };

    addToken("member", "variable", true, false);  // class-scope field
    addToken("param", "parameter", true, false);  // parameter
    addToken("local", "variable", true, false);   // local decl
    addToken("member", "variable", false, false); // use: member=local
    addToken("member", "member", false, false);   // this->member
    addToken("member", "member", false, false);   // local.member

    std::string output;
    editor.renderLineWithSyntax(output, line, 0, (int)line.size(), 0);

    const std::string typeColor = editor.theme.syntax(TOKEN_TYPE);
    const std::string normalColor = editor.theme.syntax(TOKEN_NORMAL);
    EXPECT_TRUE(text_utils::is_found(output.find(typeColor + "member")));
    EXPECT_TRUE(text_utils::is_found(output.find(normalColor + "local")));
}

TEST(SyntaxHighlighterTest, SemanticTokensDifferentiateMembersAndMethods)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.cpp";
    editor.syntaxCppSemanticTokens = true;
    editor.syntaxCppMemberToken = TOKEN_MEMBER;

    const std::string line = "ctx.commandBuffer; ctx.startCommandPopup();";
    editor.currentBuffer->lines = {line};
    editor.currentBuffer->lspSemanticTokens.resize(1);
    editor.currentBuffer->lspSemanticTokensValid = true;

    int fieldPos = (int)line.find("commandBuffer");
    int methodPos = (int)line.find("startCommandPopup");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(fieldPos)));
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(methodPos)));

    editor.currentBuffer->lspSemanticTokens[0].push_back(
        {fieldPos, 13, "field", false, false});
    editor.currentBuffer->lspSemanticTokens[0].push_back(
        {methodPos, 17, "method", false, false});

    std::string output;
    editor.renderLineWithSyntax(output, line, 0, (int)line.size(), 0);

    const std::string memberColor = editor.theme.syntax(TOKEN_MEMBER);
    const std::string funcColor = editor.theme.syntax(TOKEN_FUNCTION);
    EXPECT_TRUE(
        text_utils::is_found(output.find(memberColor + "commandBuffer")));
    EXPECT_TRUE(
        text_utils::is_found(output.find(funcColor + "startCommandPopup")));
}

TEST(SyntaxHighlighterTest, CompletionRowBuildsAndTruncates)
{
    CompletionEntry entry;
    entry.label = "printf";
    entry.labelDetail = "(const char* fmt, ...)";
    entry.labelDescription = "int";

    std::string full = widgets::buildCompletionRowForTest(entry, 200);
    EXPECT_TRUE(text_utils::is_found(full.find("printf")));
    EXPECT_TRUE(text_utils::is_found(full.find("fmt")));
    EXPECT_TRUE(text_utils::is_found(full.find("int")));

    std::string truncated = widgets::buildCompletionRowForTest(entry, 8);
    EXPECT_LE(text_utils::displayWidth(truncated), 8);
}

TEST(SyntaxHighlighterTest, CompletionRowUsesDetailWhenLabelDetailsMissing)
{
    CompletionEntry entry;
    entry.label = "int";
    entry.detail = "builtin type alias (int32_t)";

    std::string full = widgets::buildCompletionRowForTest(entry, 200);
    EXPECT_TRUE(text_utils::is_found(full.find("int")));
    EXPECT_TRUE(text_utils::is_found(full.find("builtin type alias")));
}

TEST(SyntaxHighlighterTest, CompletionPopupPrefersBelowCursorLine)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    editor.currentMode = INSERT;
    editor.screenRows = 12;
    editor.screenCols = 80;
    *editor.cursorY = 2;
    *editor.cursorX = 4;
    *editor.offsetY = 0;
    *editor.offsetX = 0;
    editor.completionActive = true;
    editor.completionAll = {CompletionEntry{.label = "printf"}};
    editor.completionFiltered = {0};

    std::string output;
    widgets::drawCompletionPopup(output, editor);

    const int cursorRow =
        (*editor.cursorY - *editor.offsetY) + 1 + editor.tabBarRows();
    EXPECT_TRUE(
        text_utils::is_found(output.find(
            Terminal::cursorPos(
                cursorRow + 1,
                1 + *editor.cursorX + editor.gutterWidth()))));
}

TEST(SyntaxHighlighterTest, CompletionPopupAboveCursorDoesNotCoverCursorLine)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    editor.currentMode = INSERT;
    editor.screenRows = 6;
    editor.screenCols = 80;
    *editor.cursorY = 4;
    *editor.cursorX = 4;
    *editor.offsetY = 0;
    *editor.offsetX = 0;
    editor.completionActive = true;
    editor.completionAll =
        {CompletionEntry{.label = "printf"},
         CompletionEntry{.label = "puts"},
         CompletionEntry{.label = "fprintf"}};
    editor.completionFiltered = {0, 1, 2};

    std::string output;
    widgets::drawCompletionPopup(output, editor);

    const int cursorRow =
        (*editor.cursorY - *editor.offsetY) + 1 + editor.tabBarRows();
    EXPECT_TRUE(
        text_utils::is_not_found(
            output.find(Terminal::cursorPos(cursorRow, 1))));
}

TEST(SyntaxHighlighterTest, CompletionPopupShowsAliasDocumentation)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    editor.currentMode = INSERT;
    editor.screenRows = 12;
    editor.screenCols = 90;
    *editor.cursorY = 2;
    *editor.cursorX = 4;
    *editor.offsetY = 0;
    *editor.offsetX = 0;
    editor.completionActive = true;
    CompletionEntry alias;
    alias.label = "Distance";
    alias.kind = 7;
    alias.detail = "type alias: f32";
    alias.documentation =
        "Distance stores meters as a single-precision floating point value.";
    editor.completionAll = {alias};
    editor.completionFiltered = {0};

    std::string output;
    widgets::drawCompletionPopup(output, editor);

    EXPECT_TRUE(text_utils::is_found(output.find("Distance stores meters")));
}

TEST(SyntaxHighlighterTest, CompletionPopupTruncatesLongDocumentation)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    editor.currentMode = INSERT;
    editor.screenRows = 20;
    editor.screenCols = 100;
    *editor.cursorY = 2;
    *editor.cursorX = 4;
    *editor.offsetY = 0;
    *editor.offsetX = 0;
    editor.completionActive = true;

    CompletionEntry alias;
    alias.label = "Distance";
    alias.kind = 7;
    alias.detail = "type alias: f32";
    alias.documentation.assign(700, 'a');
    alias.documentation += " tail-marker";
    editor.completionAll = {alias};
    editor.completionFiltered = {0};

    std::string output;
    widgets::drawCompletionPopup(output, editor);

    EXPECT_TRUE(text_utils::is_found(output.find("...")));
    EXPECT_TRUE(text_utils::is_not_found(output.find("tail-marker")));
}

TEST(SyntaxHighlighterTest, HighlightsSystemIncludeFromCompileCommands)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);

    auto root = makeTempRoot();
    auto includeDir = root / "include";
    std::filesystem::create_directories(includeDir);
    {
        std::ofstream hdr(includeDir / "vector");
        hdr << "// header\n";
    }
    {
        std::ofstream cc(root / "compile_commands.json");
        // generic_string: JSON would reject Windows backslashes.
        cc << R"([{"directory": ")" << root.generic_string()
           << R"(", "command": "clang++ -Iinclude -c main.cpp"}])";
    }

    editor.setProjectRoot(root.string());
    *editor.filename = (root / "main.cpp").string();

    const std::string line = "#include <vector>";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    EXPECT_FALSE(editor.tasks.idle());
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do
    {
        editor.serviceTasks();
        tokens = editor.tokenizeLine(
            line,
            inBlockComment,
            inTomlMultiline,
            tomlQuote,
            inMarkupFence,
            markupFenceChar);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while(!editor.tasks.idle() &&
            std::chrono::steady_clock::now() < deadline);
    ASSERT_TRUE(editor.tasks.idle());

    int start = (int)line.find('<');
    int end = (int)line.find('>');
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(start)));
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(end)));
    EXPECT_TRUE(hasTokenAt(tokens, start, end - start + 1, TOKEN_STRING));
}

TEST(SyntaxHighlighterTest, NoSystemIncludeHighlightWhenDisabled)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);

    auto root = makeTempRoot();
    auto includeDir = root / "include";
    std::filesystem::create_directories(includeDir);
    {
        std::ofstream hdr(includeDir / "vector");
        hdr << "// header\n";
    }
    {
        std::ofstream cc(root / "compile_commands.json");
        cc << R"([{"directory": ")" << root.generic_string()
           << R"(", "command": "clang++ -Iinclude -c main.cpp"}])";
    }

    editor.setProjectRoot(root.string());
    *editor.filename = (root / "main.cpp").string();
    editor.syntaxCppHighlightSystemIncludes = false;

    const std::string line = "#include <vector>";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    EXPECT_FALSE(hasTokenType(tokens, TOKEN_STRING));
}

TEST(SyntaxHighlighterTest, HighlightsUserTypeInFunctionParams)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);

    auto root = makeTempRoot();
    {
        std::ofstream header(root / "mode_context.h");
        header << "class ModeContext { };";
    }

    editor.setProjectRoot(root.string());
    *editor.filename = (root / "main.cpp").string();
    editor.syntaxHighlighter->ensureCppMemberIndexLoaded();
    ASSERT_TRUE(waitForHeaderIndex(editor));

    const std::string line = "void foo(ModeContext& bar);";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int typePos = (int)line.find("ModeContext");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(typePos)));
    EXPECT_TRUE(hasTokenAt(tokens, typePos, 11, TOKEN_TYPE));
}

TEST(SyntaxHighlighterTest, NoParamTypeHighlightWhenDisabled)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);

    auto root = makeTempRoot();
    {
        std::ofstream header(root / "mode_context.h");
        header << "class ModeContext { };";
    }

    editor.setProjectRoot(root.string());
    *editor.filename = (root / "main.cpp").string();
    editor.syntaxCppHighlightParamTypes = false;

    const std::string line = "void foo(ModeContext& bar);";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int typePos = (int)line.find("ModeContext");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(typePos)));
    EXPECT_FALSE(hasTokenAt(tokens, typePos, 11, TOKEN_TYPE));
}

TEST(SyntaxHighlighterTest, HighlightsUserTypeInLocalDeclaration)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);

    auto root = makeTempRoot();
    {
        std::ofstream header(root / "mode_context.h");
        header << "class ModeContext { };";
    }

    editor.setProjectRoot(root.string());
    *editor.filename = (root / "main.cpp").string();
    editor.syntaxHighlighter->ensureCppMemberIndexLoaded();
    ASSERT_TRUE(waitForHeaderIndex(editor));

    const std::string line = "ModeContext ctx;";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int typePos = (int)line.find("ModeContext");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(typePos)));
    EXPECT_TRUE(hasTokenAt(tokens, typePos, 11, TOKEN_TYPE));
}

TEST(SyntaxHighlighterTest, DoesNotHighlightParamVariableName)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.cpp";

    const std::string line = "void foo(int bar);";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int barPos = (int)line.find("bar");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(barPos)));
    EXPECT_FALSE(hasTokenAt(tokens, barPos, 3, TOKEN_MEMBER));
}

TEST(SyntaxHighlighterTest, DoesNotHighlightLocalVariableName)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);

    auto root = makeTempRoot();
    {
        std::ofstream header(root / "foo.h");
        header << "class Foo { public: void bar(); };";
    }
    editor.setProjectRoot(root.string());
    *editor.filename = (root / "foo.cpp").string();
    const std::string line = "    int key = 0;";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar,
        true,
        true,
        false);

    EXPECT_FALSE(hasTokenType(tokens, TOKEN_MEMBER));
}

TEST(SyntaxHighlighterTest, DoesNotHighlightLocalVariableInFreeFunction)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.cpp";
    const std::string line = "    int value = 0;";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar,
        false,
        true,
        false);

    EXPECT_FALSE(hasTokenType(tokens, TOKEN_MEMBER));
}

TEST(SyntaxHighlighterTest, DoesNotHighlightLocalVariableInMethod)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);

    auto root = makeTempRoot();
    {
        std::ofstream header(root / "foo.h");
        header << "class Foo { public: void bar(); };";
    }
    editor.setProjectRoot(root.string());
    *editor.filename = (root / "foo.cpp").string();
    const std::string line = "    std::string rest = \"\";";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar,
        true,
        true,
        false);

    EXPECT_FALSE(hasTokenType(tokens, TOKEN_MEMBER));
}

TEST(SyntaxHighlighterTest, HighlightsUserTypePointerInParams)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);

    auto root = makeTempRoot();
    {
        std::ofstream header(root / "editor.h");
        header << "class Editor\n{\npublic:\n    void foo();\n};\n";
    }

    editor.setProjectRoot(root.string());
    *editor.filename = (root / "formatter.cpp").string();
    editor.syntaxHighlighter->ensureCppMemberIndexLoaded();
    ASSERT_TRUE(waitForHeaderIndex(editor));

    const std::string line = "Formatter::Formatter(Editor* editor) {}";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int typePos = (int)line.find("Editor");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(typePos)));
    EXPECT_TRUE(hasTokenAt(tokens, typePos, 6, TOKEN_TYPE));
}

TEST(SyntaxHighlighterTest, DoesNotHighlightLocalVariableMatchingMember)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);

    auto root = makeTempRoot();
    {
        std::ofstream header(root / "foo.h");
        header << "class Foo {\n"
                  "public:\n"
                  "    void bar();\n"
                  "    std::string cmd;\n"
                  "};\n";
    }
    editor.setProjectRoot(root.string());
    *editor.filename = (root / "foo.cpp").string();
    const std::string line = "    std::string cmd = \"\";";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar,
        true,
        true,
        false);

    EXPECT_FALSE(hasTokenType(tokens, TOKEN_MEMBER));
}

TEST(SyntaxHighlighterTest, DoesNotHighlightParamNameMatchingMember)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);

    auto root = makeTempRoot();
    {
        std::ofstream header(root / "formatter.h");
        header << "class Editor { };\n"
                  "class Formatter {\n"
                  "public:\n"
                  "    Formatter(Editor* editor);\n"
                  "private:\n"
                  "    Editor* editor;\n"
                  "};\n";
    }

    editor.setProjectRoot(root.string());
    *editor.filename = (root / "formatter.cpp").string();

    const std::string line =
        "Formatter::Formatter(Editor* editor) : editor(editor) {}";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int namePos = (int)line.find("editor)");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(namePos)));
    EXPECT_FALSE(hasTokenAt(tokens, namePos, 6, TOKEN_MEMBER));
}

TEST(SyntaxHighlighterTest, HighlightsPythonBasics)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.py";

    const std::string line = "def foo(x): return 42 # comment";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int defPos = (int)line.find("def");
    int fooPos = (int)line.find("foo");
    int numPos = (int)line.find("42");
    int commentPos = (int)line.find("#");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(defPos)));
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(fooPos)));
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(numPos)));
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(commentPos)));
    EXPECT_TRUE(hasTokenAt(tokens, defPos, 3, TOKEN_KEYWORD));
    EXPECT_TRUE(hasTokenAt(tokens, fooPos, 3, TOKEN_FUNCTION));
    EXPECT_TRUE(hasTokenAt(tokens, numPos, 2, TOKEN_NUMBER));
    EXPECT_TRUE(hasTokenAt(
        tokens,
        commentPos,
        (int)line.size() - commentPos,
        TOKEN_COMMENT));
}

TEST(SyntaxHighlighterTest, HighlightsPythonClassName)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.py";

    const std::string line = "class Foo(Bar):";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int classPos = (int)line.find("class");
    int fooPos = (int)line.find("Foo");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(classPos)));
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(fooPos)));
    EXPECT_TRUE(hasTokenAt(tokens, classPos, 5, TOKEN_KEYWORD));
    EXPECT_TRUE(hasTokenAt(tokens, fooPos, 3, TOKEN_TYPE));
}

TEST(SyntaxHighlighterTest, HighlightsPythonSelfMember)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.py";

    const std::string line = "self.child = None";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int childPos = (int)line.find("child");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(childPos)));
    EXPECT_TRUE(hasTokenAt(tokens, childPos, 5, TOKEN_MEMBER));
}

TEST(SyntaxHighlighterTest, HighlightsPythonBuiltinTypes)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.py";

    const std::string line = "status: int | None";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int intPos = (int)line.find("int");
    int nonePos = (int)line.find("None");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(intPos)));
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(nonePos)));
    EXPECT_TRUE(hasTokenAt(tokens, intPos, 3, TOKEN_TYPE));
    EXPECT_TRUE(hasTokenAt(tokens, nonePos, 4, TOKEN_TYPE));
}

TEST(SyntaxHighlighterTest, HighlightsPythonEnumValue)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.py";

    const std::string line = "Color.RED";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int redPos = (int)line.find("RED");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(redPos)));
    EXPECT_TRUE(hasTokenAt(tokens, redPos, 3, TOKEN_MEMBER));
}

TEST(SyntaxHighlighterTest, HighlightsMlangMemberAccess)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.mla";

    const std::string line = "foo.value";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int valuePos = (int)line.find("value");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(valuePos)));
    EXPECT_TRUE(hasTokenAt(tokens, valuePos, 5, TOKEN_MEMBER));
}

TEST(SyntaxHighlighterTest, HighlightsMlangQualifiedNamespaceSegments)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.mla";

    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;

    const std::string line = "use std::esc::bold_on;";
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    EXPECT_TRUE(hasTokenAt(tokens, 4, 3, TOKEN_NAMESPACE_1));
    EXPECT_TRUE(hasTokenAt(tokens, 7, 2, TOKEN_OPERATOR));
    EXPECT_TRUE(hasTokenAt(tokens, 9, 3, TOKEN_NAMESPACE_2));
    EXPECT_TRUE(hasTokenAt(tokens, 12, 2, TOKEN_OPERATOR));
    EXPECT_TRUE(hasTokenAt(tokens, 14, 7, TOKEN_NAMESPACE_4));
}

TEST(SyntaxHighlighterTest, HighlightsMlangQualifiedTypeFinalSegment)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.mla";

    const std::string line = "let value: namespace::SomeClass;";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int namespacePos = (int)line.find("namespace");
    int someClassPos = (int)line.find("SomeClass");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(namespacePos)));
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(someClassPos)));
    EXPECT_TRUE(hasTokenAt(tokens, namespacePos, 9, TOKEN_NAMESPACE_1));
    EXPECT_TRUE(hasTokenAt(tokens, someClassPos, 9, TOKEN_NAMESPACE_4));
}

TEST(SyntaxHighlighterTest, HighlightsMlangModQualifiedNamespaceSegments)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.mla";

    const std::string line = "mod std::esc;";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int stdPos = (int)line.find("std");
    int escPos = (int)line.find("esc");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(stdPos)));
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(escPos)));
    EXPECT_TRUE(hasTokenAt(tokens, stdPos, 3, TOKEN_NAMESPACE_1));
    EXPECT_TRUE(hasTokenAt(tokens, escPos, 3, TOKEN_NAMESPACE_4));
}

TEST(SyntaxHighlighterTest, HighlightsMlangTraitNameAsType)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.mla";

    const std::string line = "trait Summary {";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int namePos = (int)line.find("Summary");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(namePos)));
    EXPECT_TRUE(hasTokenAt(tokens, namePos, 7, TOKEN_TYPE));
}

TEST(SyntaxHighlighterTest, HighlightsMlangUserTypeInAnnotationAndLiteral)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.mla";

    const std::string line = "let post: SocialPost = SocialPost {";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int firstPos = (int)line.find("SocialPost");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(firstPos)));
    EXPECT_TRUE(hasTokenAt(tokens, firstPos, 10, TOKEN_TYPE));

    int secondPos = (int)line.rfind("SocialPost");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(secondPos)));
    EXPECT_TRUE(hasTokenAt(tokens, secondPos, 10, TOKEN_TYPE));
}

TEST(SyntaxHighlighterTest, HighlightsMlangAliasNameAsType)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.mla";

    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;

    const std::string aliasLine = "alias Distance = f32;";
    auto aliasTokens = editor.tokenizeLine(
        aliasLine,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);
    int aliasNamePos = (int)aliasLine.find("Distance");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(aliasNamePos)));
    EXPECT_TRUE(hasTokenAt(aliasTokens, aliasNamePos, 8, TOKEN_TYPE));

    const std::string useLine = "let d1: Distance = 10.0f;";
    auto useTokens = editor.tokenizeLine(
        useLine,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);
    int useNamePos = (int)useLine.find("Distance");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(useNamePos)));
    EXPECT_TRUE(hasTokenAt(useTokens, useNamePos, 8, TOKEN_TYPE));
}

TEST(SyntaxHighlighterTest, HighlightsMlangUseTypeAliasNameAsType)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.mla";

    const std::string line = "use type Distance = f32;";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int namePos = (int)line.find("Distance");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(namePos)));
    EXPECT_TRUE(hasTokenAt(tokens, namePos, 8, TOKEN_TYPE));
}

TEST(SyntaxHighlighterTest, HighlightsMlangBuiltinDocTypes)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/types.mla";
    editor.syntaxMlangHighlightBuiltinDocs = true;

    const std::string line = "// @builtin Foo";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int fooPos = (int)line.find("Foo");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(fooPos)));
    EXPECT_TRUE(hasTokenAt(tokens, fooPos, 3, TOKEN_TYPE));
}

TEST(SyntaxHighlighterTest, DisablesMlangBuiltinDocHighlighting)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/types.mla";
    editor.syntaxMlangHighlightBuiltinDocs = false;

    const std::string line = "// @builtin Foo";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int fooPos = (int)line.find("Foo");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(fooPos)));
    EXPECT_FALSE(hasTokenAt(tokens, fooPos, 3, TOKEN_TYPE));
}

TEST(SyntaxHighlighterTest, TogglesMlangTypeHighlighting)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.mla";

    const std::string line = "int value";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;

    editor.syntaxMlangHighlightTypes = true;
    auto tokensOn = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);
    int typePos = (int)line.find("int");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(typePos)));
    EXPECT_TRUE(hasTokenAt(tokensOn, typePos, 3, TOKEN_TYPE));

    editor.syntaxMlangHighlightTypes = false;
    auto tokensOff = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);
    EXPECT_FALSE(hasTokenAt(tokensOff, typePos, 3, TOKEN_TYPE));
}

TEST(SyntaxHighlighterTest, HighlightsMlangPlatformKeywords)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.mla";

    const std::string line = "if aarch64!() || linux!() || macos!() || "
                             "posix!() || windows!() || x64!() { }";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    for(std::string_view word :
        {"aarch64", "linux", "macos", "posix", "windows", "x64"})
    {
        int pos = (int)line.find(word);
        ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(pos))) << word;
        EXPECT_TRUE(hasTokenAt(tokens, pos, (int)word.size(), TOKEN_KEYWORD))
            << word;
    }
}

TEST(SyntaxHighlighterTest, HighlightsMlangConceptConstraintKeywords)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/concepts.mla";
    const std::string line =
        "concept Addable<T> = req(value: T) { typename T; "
        "{ value + value } noexcept; }; generic<T> where "
        "Addable<T> cexpr fn twice(value: T) -> T { return value + value; } "
        "generic<U> where Addable<U> fn another(value: U) -> U;";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    const auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);
    for(std::string_view word :
        {"concept", "req", "typename", "noexcept", "generic", "where", "cexpr"})
    {
        const int pos = static_cast<int>(line.find(word));
        ASSERT_GE(pos, 0) << word;
        EXPECT_TRUE(hasTokenAt(
            tokens,
            pos,
            static_cast<int>(word.size()),
            TOKEN_KEYWORD))
            << word;
    }
    const int name = static_cast<int>(line.find("Addable"));
    EXPECT_TRUE(hasTokenAt(tokens, name, 7, TOKEN_TYPE));
}

TEST(SyntaxHighlighterTest, RemovedRequiresIsNotAnMlangKeyword)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/concepts.mla";
    const std::string line = "requires";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    const auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);
    EXPECT_FALSE(hasTokenAt(tokens, 0, 8, TOKEN_KEYWORD));
}

TEST(SyntaxHighlighterTest, IgnoresMlangConstraintWordsInStringsAndComments)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/concepts.mla";
    const std::string line = "\"req where concept\"; // req where concept";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    const auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);
    for(std::string_view word : {"req", "where", "concept"})
    {
        size_t pos = line.find(word);
        while(pos != std::string::npos)
        {
            EXPECT_FALSE(hasTokenAt(
                tokens,
                static_cast<int>(pos),
                static_cast<int>(word.size()),
                TOKEN_KEYWORD));
            pos = line.find(word, pos + word.size());
        }
    }
}

TEST(SyntaxHighlighterTest, HighlightsMlangConceptUsesAndStaticAssertAsTypes)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/concepts.mla";

    auto tokenize = [&](const std::string& line)
    {
        bool inBlockComment = false;
        bool inTomlMultiline = false;
        char tomlQuote = 0;
        bool inMarkupFence = false;
        char markupFenceChar = 0;
        return editor.tokenizeLine(
            line,
            inBlockComment,
            inTomlMultiline,
            tomlQuote,
            inMarkupFence,
            markupFenceChar);
    };

    const std::string arithmetic = "generic<T> where arithmetic<T>";
    const auto arithmeticTokens = tokenize(arithmetic);
    EXPECT_TRUE(hasTokenAt(
        arithmeticTokens,
        static_cast<int>(arithmetic.find("arithmetic")),
        10,
        TOKEN_TYPE));

    const std::string constrained = "generic<T: integral>";
    const auto constrainedTokens = tokenize(constrained);
    EXPECT_TRUE(hasTokenAt(
        constrainedTokens,
        static_cast<int>(constrained.find("integral")),
        8,
        TOKEN_TYPE));

    const std::string floating =
        "generic<T> where floating_point<T> fn category(value: T)";
    const auto floatingTokens = tokenize(floating);
    EXPECT_TRUE(hasTokenAt(
        floatingTokens,
        static_cast<int>(floating.find("floating_point")),
        14,
        TOKEN_TYPE));

    const std::string assertion = "static_assert!(same_as<i32, i32>);";
    const auto assertionTokens = tokenize(assertion);
    EXPECT_TRUE(hasTokenAt(assertionTokens, 0, 13, TOKEN_TYPE));
    EXPECT_TRUE(hasTokenAt(
        assertionTokens,
        static_cast<int>(assertion.find("same_as")),
        7,
        TOKEN_TYPE));

    const std::string ordinary = "let arithmetic = 1; arithmetic(value);";
    const auto ordinaryTokens = tokenize(ordinary);
    EXPECT_FALSE(hasTokenAt(
        ordinaryTokens,
        static_cast<int>(ordinary.find("arithmetic")),
        10,
        TOKEN_TYPE));
}

TEST(SyntaxHighlighterTest, HighlightsMlangBranchPredictionKeywords)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.mla";

    const std::string line = "if likely ready { } else unlikely { }";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    const auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    const int likelyPos = (int)line.find("likely");
    const int unlikelyPos = (int)line.find("unlikely");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(likelyPos)));
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(unlikelyPos)));
    EXPECT_TRUE(hasTokenAt(tokens, likelyPos, 6, TOKEN_KEYWORD));
    EXPECT_TRUE(hasTokenAt(tokens, unlikelyPos, 8, TOKEN_KEYWORD));
}

TEST(SyntaxHighlighterTest, HighlightsPythonCapsConstantAfterModule)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.py";

    const std::string line = "pexpect.TIMEOUT";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int timeoutPos = (int)line.find("TIMEOUT");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(timeoutPos)));
    EXPECT_TRUE(hasTokenAt(tokens, timeoutPos, 7, TOKEN_MEMBER));
}

TEST(SyntaxHighlighterTest, DoesNotHighlightLambdaParamNames)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.cpp";
    const std::string line = "auto appendLsp = [&](const std::string& label, "
                             "bool running, bool activeForFile,";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);

    int labelPos = (int)line.find("label");
    int runningPos = (int)line.find("running");
    int activePos = (int)line.find("activeForFile");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(labelPos)));
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(runningPos)));
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(activePos)));
    EXPECT_FALSE(hasTokenAt(tokens, labelPos, 5, TOKEN_MEMBER));
    EXPECT_FALSE(hasTokenAt(tokens, runningPos, 7, TOKEN_MEMBER));
    EXPECT_FALSE(hasTokenAt(tokens, activePos, 13, TOKEN_MEMBER));
}

TEST(SyntaxHighlighterTest, DirtyLargeBufferUsesIncrementalSyntaxCache)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/large.cpp";
    editor.currentBuffer->lines.clear();
    for(int i = 0; i < 12000; ++i)
        editor.currentBuffer->lines.push_back(
            "int value_" + std::to_string(i) + " = " + std::to_string(i) + ";");
    editor.currentBuffer->dirty = true;

    std::string output;
    const int row = 11950;
    const std::string& line = editor.currentBuffer->lines[row];
    editor.renderLineWithSyntax(output, line, 0, (int)line.size(), row);

    EXPECT_GE(editor.currentBuffer->syntaxCacheComputedUpTo, row);
    EXPECT_EQ(
        editor.currentBuffer->syntaxCache.size(),
        editor.currentBuffer->lines.size());
    EXPECT_FALSE(output.empty());
}

namespace
{
std::vector<Token> tokenizeCSharp(
    Editor& editor,
    const std::string& line,
    bool& inBlockComment)
{
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    return editor.tokenizeLine(
        line,
        inBlockComment,
        inTomlMultiline,
        tomlQuote,
        inMarkupFence,
        markupFenceChar);
}
} // namespace

TEST(SyntaxHighlighterTest, HighlightsCSharpKeywordsTypesAndStrings)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/Program.cs";
    bool inBlock = false;

    std::string line =
        "public static async Task<int> Run(string path) { var n = 1; }";
    auto tokens = tokenizeCSharp(editor, line, inBlock);
    EXPECT_TRUE(hasTokenAt(tokens, (int)line.find("public"), 6, TOKEN_KEYWORD));
    EXPECT_TRUE(hasTokenAt(tokens, (int)line.find("static"), 6, TOKEN_KEYWORD));
    EXPECT_TRUE(hasTokenAt(tokens, (int)line.find("async"), 5, TOKEN_KEYWORD));
    EXPECT_TRUE(hasTokenAt(tokens, (int)line.find("int>"), 3, TOKEN_TYPE));
    EXPECT_TRUE(hasTokenAt(tokens, (int)line.find("string"), 6, TOKEN_TYPE));
    EXPECT_TRUE(hasTokenAt(tokens, (int)line.find("var"), 3, TOKEN_KEYWORD));
    EXPECT_TRUE(hasTokenAt(tokens, (int)line.find("1;"), 1, TOKEN_NUMBER));

    // Verbatim string: a trailing backslash does not escape the quote.
    line = R"(var p = @"C:\temp\"; // done)";
    tokens = tokenizeCSharp(editor, line, inBlock);
    const int at = (int)line.find('@');
    EXPECT_TRUE(hasTokenAt(tokens, at, (int)line.find(';') - at, TOKEN_STRING));
    EXPECT_TRUE(hasTokenAt(
        tokens,
        (int)line.find("//"),
        (int)line.size() - (int)line.find("//"),
        TOKEN_COMMENT));

    // Verbatim "" is a quote; interpolated and raw strings.
    line =
        R"(var s = @"say ""hi"""; var t = $"n={n}"; var r = """raw "x" """;)";
    tokens = tokenizeCSharp(editor, line, inBlock);
    const int v = (int)line.find('@');
    EXPECT_TRUE(hasTokenAt(tokens, v, (int)line.find(';') - v, TOKEN_STRING));
    const int d = (int)line.find('$');
    EXPECT_TRUE(
        hasTokenAt(tokens, d, (int)line.find(';', d) - d, TOKEN_STRING));
    const int r = (int)line.find(R"("""raw)");
    EXPECT_TRUE(hasTokenAt(tokens, r, (int)line.rfind(';') - r, TOKEN_STRING));

    // Directives and doc comments.
    line = "    #region Helpers";
    tokens = tokenizeCSharp(editor, line, inBlock);
    EXPECT_TRUE(
        hasTokenAt(tokens, 4, (int)line.size() - 4, TOKEN_PREPROCESSOR));
    line = "/// <summary>Docs</summary>";
    tokens = tokenizeCSharp(editor, line, inBlock);
    EXPECT_TRUE(hasTokenAt(tokens, 0, (int)line.size(), TOKEN_COMMENT));
    // Block comments continue on the next line.
    line = "int x; /* start";
    tokens = tokenizeCSharp(editor, line, inBlock);
    EXPECT_TRUE(inBlock);
}

TEST(SyntaxHighlighterTest, HighlightsPowerShell)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/build.ps1";
    bool inBlock = false;
    bool inHere = false;
    char hereQuote = 0;
    bool inFence = false;
    char fenceChar = 0;
    auto tokenize = [&](const std::string& line)
    {
        return editor
            .tokenizeLine(line, inBlock, inHere, hereQuote, inFence, fenceChar);
    };

    std::string line = "function Get-Thing([string]$Path) { if ($env:HOME "
                       "-eq 'x') { Write-Host \"a`\"b\" } } # done";
    auto tokens = tokenize(line);
    EXPECT_TRUE(hasTokenAt(tokens, 0, 8, TOKEN_KEYWORD));
    EXPECT_TRUE(hasTokenAt(tokens, 9, 9, TOKEN_FUNCTION));
    EXPECT_TRUE(hasTokenAt(tokens, (int)line.find("string"), 6, TOKEN_TYPE));
    EXPECT_TRUE(hasTokenAt(tokens, (int)line.find("$Path"), 5, TOKEN_MEMBER));
    EXPECT_TRUE(hasTokenAt(tokens, (int)line.find("if"), 2, TOKEN_KEYWORD));
    EXPECT_TRUE(
        hasTokenAt(tokens, (int)line.find("$env:HOME"), 9, TOKEN_MEMBER));
    EXPECT_TRUE(hasTokenAt(tokens, (int)line.find("-eq"), 3, TOKEN_OPERATOR));
    EXPECT_TRUE(hasTokenAt(tokens, (int)line.find("'x'"), 3, TOKEN_STRING));
    EXPECT_TRUE(
        hasTokenAt(tokens, (int)line.find("Write-Host"), 10, TOKEN_FUNCTION));
    EXPECT_TRUE(hasTokenAt(tokens, (int)line.find("\"a"), 6, TOKEN_STRING));
    EXPECT_TRUE(hasTokenAt(tokens, (int)line.find("# done"), 6, TOKEN_COMMENT));

    // Keywords are case-insensitive; -Parameter names are not operators.
    line = "ForEach ($f in $files) { Copy-Item -Path $f -Force }";
    tokens = tokenize(line);
    EXPECT_TRUE(hasTokenAt(tokens, 0, 7, TOKEN_KEYWORD));
    EXPECT_TRUE(hasTokenAt(tokens, (int)line.find("in "), 2, TOKEN_KEYWORD));
    EXPECT_FALSE(
        hasTokenAt(tokens, (int)line.find("-Path"), 5, TOKEN_OPERATOR));

    // <# ... #> block comments span lines.
    tokens = tokenize("$x = 1 <# start");
    EXPECT_TRUE(inBlock);
    tokens = tokenize("still comment #> $y");
    EXPECT_FALSE(inBlock);
    EXPECT_TRUE(hasTokenAt(tokens, 0, 16, TOKEN_COMMENT));
    EXPECT_TRUE(hasTokenAt(tokens, 17, 2, TOKEN_MEMBER));

    // Here-strings span lines until "@ at the start of a line.
    tokens = tokenize("$s = @\"");
    EXPECT_TRUE(inHere);
    tokens = tokenize("  if $not # code");
    EXPECT_TRUE(hasTokenAt(tokens, 0, 16, TOKEN_STRING));
    tokens = tokenize("\"@ | Out-File a.txt");
    EXPECT_FALSE(inHere);
    EXPECT_TRUE(hasTokenAt(tokens, 0, 2, TOKEN_STRING));
    EXPECT_TRUE(hasTokenAt(tokens, 5, 8, TOKEN_FUNCTION));
}

TEST(SyntaxHighlighterTest, CppQualifiedTypesUseTheSameFinalSegmentColor)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/state.h";
    for(const auto& name :
        {std::string("std::string"),
         std::string("std::string_view"),
         std::string("std::optional<int>"),
         std::string("std::chrono::steady_clock::time_point")})
    {
        const auto line = name + " value;";
        bool comment = false, toml = false, fence = false;
        char quote = 0, fenceChar = 0;
        const auto tokens =
            editor.tokenizeLine(line, comment, toml, quote, fence, fenceChar);
        const auto end = name.find('<');
        const auto type = name.substr(0, end);
        const int start = static_cast<int>(type.rfind("::") + 2);
        EXPECT_TRUE(hasTokenAt(
            tokens,
            start,
            static_cast<int>(type.size()) - start,
            TOKEN_NAMESPACE_4))
            << name;
        EXPECT_FALSE(hasTokenAt(
            tokens,
            start,
            static_cast<int>(type.size()) - start,
            TOKEN_TYPE))
            << name;
    }
}

TEST(SyntaxHighlighterTest, CppFieldsAreDistinctFromTypesAndLocalVariables)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/state.h";
    editor.syntaxCppSemanticTokens = false;
    editor.currentBuffer->lines =
        {"struct State {",
         "    bool active = true;",
         "    bool docsLoading = false, docsIndicator = false;",
         "    std::string docsTitle, docsText;",
         "    std::chrono::steady_clock::time_point docsStarted{};",
         "    void method() {",
         "        bool local = false;",
         "    }",
         "};"};
    const auto memberColor = editor.theme.syntax(TOKEN_MEMBER);
    EXPECT_NE(memberColor, editor.theme.syntax(TOKEN_TYPE));
    EXPECT_NE(memberColor, editor.theme.syntax(TOKEN_NORMAL));
    for(const auto& [row, names] :
        std::vector<std::pair<int, std::vector<std::string>>>{
            {1, {"active"}},
            {2, {"docsLoading", "docsIndicator"}},
            {3, {"docsTitle", "docsText"}},
            {4, {"docsStarted"}}})
    {
        const auto& line = editor.currentBuffer->lines[row];
        std::string output;
        editor.renderLineWithSyntax(
            output,
            line,
            0,
            static_cast<int>(line.size()),
            row);
        for(const auto& name : names)
            EXPECT_NE(output.find(memberColor + name), std::string::npos)
                << line << " member " << name;
    }
    const auto& local = editor.currentBuffer->lines[6];
    std::string output;
    editor.renderLineWithSyntax(
        output,
        local,
        0,
        static_cast<int>(local.size()),
        6);
    EXPECT_EQ(output.find(memberColor + "local"), std::string::npos);
    EXPECT_NE(
        output.find(editor.theme.syntax(TOKEN_NORMAL) + " local"),
        std::string::npos);
}

TEST(SyntaxHighlighterTest, CppFieldDeclaratorsSkipNestedInitializerCommas)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/state.h";
    for(const auto& line :
        {std::string("bool enabled = choose(a, b), ready = false;"),
         std::string("std::pair<int, int> enabled{1, 2}, ready{3, 4};"),
         std::string(
             "std::string enabled = \"a,b\", ready; // ignored, fake;")})
    {
        bool comment = false, toml = false, fence = false;
        char quote = 0, fenceChar = 0;
        const auto tokens =
            editor.tokenizeLine(line, comment, toml, quote, fence, fenceChar);
        EXPECT_TRUE(hasTokenAt(
            tokens,
            static_cast<int>(line.find("enabled")),
            7,
            TOKEN_MEMBER))
            << line;
        EXPECT_TRUE(hasTokenAt(
            tokens,
            static_cast<int>(line.find("ready")),
            5,
            TOKEN_MEMBER))
            << line;
        EXPECT_FALSE(hasTokenAt(
            tokens,
            static_cast<int>(line.find("fake")),
            4,
            TOKEN_MEMBER));
    }
}

TEST(SyntaxHighlighterTest, CppFunctionAndOperatorDeclarationsAreNotFields)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/state.h";
    for(const auto& line :
        {std::string("bool operator==(const State& other) const;"),
         std::string("std::string name() const;")})
    {
        bool comment = false, toml = false, fence = false;
        char quote = 0, fenceChar = 0;
        const auto tokens =
            editor.tokenizeLine(line, comment, toml, quote, fence, fenceChar);
        EXPECT_FALSE(hasTokenType(tokens, TOKEN_MEMBER)) << line;
    }
    for(const auto& line :
        {std::string("std::string value = make(a, b);"),
         std::string("int value = 1;")})
    {
        bool comment = false, toml = false, fence = false;
        char quote = 0, fenceChar = 0;
        const auto tokens = editor.tokenizeLine(
            line,
            comment,
            toml,
            quote,
            fence,
            fenceChar,
            false,
            true,
            false);
        EXPECT_FALSE(hasTokenType(tokens, TOKEN_MEMBER)) << line;
    }
}

TEST(
    SyntaxHighlighterTest,
    SemanticTokensKeepLocalObjectsNormalBeforeMemberCalls)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/locals.cpp";
    editor.syntaxCppSemanticTokens = true;
    editor.currentBuffer->lines =
        {"void splitLines()",
         "{",
         "    std::vector<std::string> lines;",
         "    lines.push_back(line);",
         "    pointer->field.clear();",
         "    object.lines.push_back(line);",
         "}"};
    editor.currentBuffer->lspSemanticTokens.resize(editor.lines->size());
    editor.currentBuffer->lspSemanticTokensValid = true;
    const auto add = [&](int row,
                         std::string_view name,
                         const std::string& type,
                         bool declaration = false)
    {
        const auto& line = editor.currentBuffer->lines[row];
        editor.currentBuffer->lspSemanticTokens[row].push_back(
            {static_cast<int>(line.find(name)),
             static_cast<int>(name.size()),
             type,
             declaration,
             false});
    };
    add(2, "lines", "variable", true);
    add(3, "lines", "variable");
    add(3, "push_back", "method");
    add(4, "pointer", "variable");
    add(4,
        "field",
        "variable"); // Older servers can report fields as variables.
    add(4, "clear", "method");
    add(5, "object", "variable");
    add(5, "lines", "field");
    add(5, "push_back", "method");
    for(int row = 2; row <= 5; ++row)
    {
        const auto& line = editor.currentBuffer->lines[row];
        std::string output;
        editor.renderLineWithSyntax(
            output,
            line,
            0,
            static_cast<int>(line.size()),
            row);
        const auto member = editor.theme.syntax(TOKEN_MEMBER);
        if(row == 2)
            EXPECT_EQ(output.find(member + "lines"), std::string::npos);
        if(row == 3)
        {
            EXPECT_TRUE(output.starts_with("    lines"));
            EXPECT_NE(
                output.find(editor.theme.syntax(TOKEN_FUNCTION) + "push_back"),
                std::string::npos);
        }
        if(row == 4)
        {
            EXPECT_TRUE(output.starts_with("    pointer"));
            EXPECT_NE(output.find(member + "field"), std::string::npos);
        }
        if(row == 5)
        {
            EXPECT_TRUE(output.starts_with("    object"));
            EXPECT_NE(output.find(member + "lines"), std::string::npos);
        }
    }
}

TEST(SyntaxHighlighterTest, CppLocalsStayLocalAcrossNestedCallBlocksAndLambdas)
{
    const std::vector<std::string> lines{
        "namespace example",
        "{",
        "CompileCommand findCompileCommand(const Request& request,",
        "                                  const std::function<bool()>& "
        "cancelled)",
        "{",
        "    std::vector<fs::path> candidates;",
        "    if(!request.compileCommandsDirectory.empty())",
        "    {",
        "        fs::path configured(request.compileCommandsDirectory);",
        "        candidates.push_back(configured.filename());",
        "    }",
        "    std::unordered_set<std::string> seenDirectories;",
        "    auto addAncestors = [&](fs::path directory)",
        "    {",
        "        while(!directory.empty() && "
        "seenDirectories.insert(directory.string()).second)",
        "        {",
        "            const auto parent = directory.parent_path();",
        "            if(parent == directory) break;",
        "            directory = parent;",
        "        }",
        "    };",
        "    addAncestors(request.directory);",
        "    std::unordered_set<std::string> seenDatabases;",
        "    for(const auto& path : candidates)",
        "    {",
        "        if(cancelled()) return {};",
        "    }",
        "    return {};",
        "}",
        "struct After",
        "{",
        "    std::string member;",
        "};",
        "}"};
    for(bool semantic : {false, true})
    {
        Editor editor = Editor::createForTests();
        setupEditorBuffer(editor);
        *editor.filename = "/tmp/nested_scope.cpp";
        editor.syntaxCppSemanticTokens = semantic;
        editor.currentBuffer->lines = lines;
        editor.currentBuffer->lspSemanticTokens.resize(lines.size());
        editor.currentBuffer->lspSemanticTokensValid = true;
        for(const auto& [row, name] : std::vector<std::pair<int, std::string>>{
                {11, "seenDirectories"},
                {12, "addAncestors"},
                {16, "parent"},
                {22, "seenDatabases"}})
        {
            const int position = static_cast<int>(lines[row].find(name));
            editor.currentBuffer->lspSemanticTokens[row].push_back(
                {position,
                 static_cast<int>(name.size()),
                 "variable",
                 true,
                 true});
            std::string output;
            editor.renderLineWithSyntax(
                output,
                lines[row],
                0,
                static_cast<int>(lines[row].size()),
                row);
            ASSERT_GT(
                editor.currentBuffer->syntaxCache.size(),
                static_cast<size_t>(row));
            EXPECT_TRUE(
                editor.currentBuffer->syntaxCache[row].inCppFunctionContext)
                << name;
            EXPECT_NE(
                output.find(editor.theme.syntax(TOKEN_NORMAL) + " " + name),
                std::string::npos)
                << name;
        }
        const int memberRow = 31;
        std::string output;
        editor.renderLineWithSyntax(
            output,
            lines[memberRow],
            0,
            static_cast<int>(lines[memberRow].size()),
            memberRow);
        EXPECT_FALSE(
            editor.currentBuffer->syntaxCache[memberRow].inCppFunctionContext);
        EXPECT_NE(
            output.find(editor.theme.syntax(TOKEN_MEMBER) + "member"),
            std::string::npos);
    }
}
