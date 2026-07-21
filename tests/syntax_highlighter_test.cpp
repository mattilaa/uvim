#include "editor.h"
#include "syntax_highlighter.h"
#include "terminal.h"
#include "text_utils.h"
#include "widgets/completion_popup.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace
{
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

bool hasTokenAt(const std::vector<Token>& tokens, int start, int length,
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

    std::string output;
    editor.renderLineWithSyntax(output, editor.currentBuffer->lines[1], 0,
                                (int)editor.currentBuffer->lines[1].size(), 1);

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
    editor.renderLineWithSyntax(output, editor.currentBuffer->lines[2], 0,
                                (int)editor.currentBuffer->lines[2].size(), 2);

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
        line, inBlockComment, inTomlMultiline, tomlQuote, inMarkupFence,
        markupFenceChar);

    EXPECT_TRUE(hasTokenAt(tokens, 0, 4, TOKEN_KEYWORD));
    EXPECT_TRUE(hasTokenAt(tokens, 6, 3, TOKEN_TYPE));
    EXPECT_TRUE(hasTokenAt(tokens, 12, 3, TOKEN_TYPE));

    const std::string aarch64Line = "ldr x0, [sp]\n";
    const auto aarch64Tokens = editor.syntaxHighlighter->tokenizeLine(
        aarch64Line, inBlockComment, inTomlMultiline, tomlQuote, inMarkupFence,
        markupFenceChar);

    EXPECT_TRUE(hasTokenAt(aarch64Tokens, 0, 3, TOKEN_KEYWORD));
    EXPECT_TRUE(hasTokenAt(aarch64Tokens, 4, 2, TOKEN_TYPE));
    EXPECT_TRUE(hasTokenAt(aarch64Tokens, 9, 2, TOKEN_TYPE));

    std::string rendered;
    editor.renderLineWithSyntax(rendered, editor.currentBuffer->lines[0], 0,
                                (int)editor.currentBuffer->lines[0].size(), 0);

    EXPECT_TRUE(text_utils::is_found(
        rendered.find(editor.theme.syntax(TOKEN_KEYWORD))));
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
    editor.renderLineWithSyntax(output, editor.currentBuffer->lines[1], 0,
                                (int)editor.currentBuffer->lines[1].size(), 1);

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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar, false, false, true);

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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

    ASSERT_FALSE(tokens.empty());
    EXPECT_EQ(tokens[0].type, TOKEN_PREPROCESSOR);
    EXPECT_EQ(tokens[0].start, 0);
    EXPECT_EQ(tokens[0].length, (int)line.size());
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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

    int vecPos = (int)line.find("vector");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(vecPos)));
    EXPECT_TRUE(hasTokenAt(tokens, vecPos, 6, TOKEN_TYPE));
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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

    int optionalPos = (int)line.find("optional");
    int summaryPos = (int)line.find("LspDiagnosticSummary");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(optionalPos)));
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(summaryPos)));
    EXPECT_TRUE(hasTokenAt(tokens, optionalPos, 8, TOKEN_TYPE));
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

    auto addToken = [&](std::string_view text, std::string_view tokenType,
                        bool isDecl, bool isDef)
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
    EXPECT_TRUE(text_utils::is_found(output.find(Terminal::cursorPos(
        cursorRow + 1, 1 + *editor.cursorX + editor.gutterWidth()))));
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
    editor.completionAll = {CompletionEntry{.label = "printf"},
                            CompletionEntry{.label = "puts"},
                            CompletionEntry{.label = "fprintf"}};
    editor.completionFiltered = {0, 1, 2};

    std::string output;
    widgets::drawCompletionPopup(output, editor);

    const int cursorRow =
        (*editor.cursorY - *editor.offsetY) + 1 + editor.tabBarRows();
    EXPECT_TRUE(text_utils::is_not_found(
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
        cc << R"([{"directory": ")" << root.string()
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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

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
        cc << R"([{"directory": ")" << root.string()
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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

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

    const std::string line = "void foo(ModeContext& bar);";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

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

    const std::string line = "ModeContext ctx;";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar, true, true, false);

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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar, false, true, false);

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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar, true, true, false);

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

    const std::string line = "Formatter::Formatter(Editor* editor) {}";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar, true, true, false);

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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

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
    EXPECT_TRUE(hasTokenAt(tokens, commentPos, (int)line.size() - commentPos,
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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

    int valuePos = (int)line.find("value");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(valuePos)));
    EXPECT_TRUE(hasTokenAt(tokens, valuePos, 5, TOKEN_MEMBER));
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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

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
    auto aliasTokens =
        editor.tokenizeLine(aliasLine, inBlockComment, inTomlMultiline,
                            tomlQuote, inMarkupFence, markupFenceChar);
    int aliasNamePos = (int)aliasLine.find("Distance");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(aliasNamePos)));
    EXPECT_TRUE(hasTokenAt(aliasTokens, aliasNamePos, 8, TOKEN_TYPE));

    const std::string useLine = "let d1: Distance = 10.0f;";
    auto useTokens =
        editor.tokenizeLine(useLine, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);
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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

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
    auto tokensOn =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);
    int typePos = (int)line.find("int");
    ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(typePos)));
    EXPECT_TRUE(hasTokenAt(tokensOn, typePos, 3, TOKEN_TYPE));

    editor.syntaxMlangHighlightTypes = false;
    auto tokensOff =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);
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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

    for(std::string_view word :
        {"aarch64", "linux", "macos", "posix", "windows", "x64"})
    {
        int pos = (int)line.find(word);
        ASSERT_TRUE(text_utils::is_found(static_cast<size_t>(pos))) << word;
        EXPECT_TRUE(hasTokenAt(tokens, pos, (int)word.size(), TOKEN_KEYWORD))
            << word;
    }
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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

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
    auto tokens =
        editor.tokenizeLine(line, inBlockComment, inTomlMultiline, tomlQuote,
                            inMarkupFence, markupFenceChar);

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
        editor.currentBuffer->lines.push_back("int value_" + std::to_string(i) +
                                              " = " + std::to_string(i) + ";");
    editor.currentBuffer->dirty = true;

    std::string output;
    const int row = 11950;
    const std::string& line = editor.currentBuffer->lines[row];
    editor.renderLineWithSyntax(output, line, 0, (int)line.size(), row);

    EXPECT_GE(editor.currentBuffer->syntaxCacheComputedUpTo, row);
    EXPECT_EQ(editor.currentBuffer->syntaxCache.size(),
              editor.currentBuffer->lines.size());
    EXPECT_FALSE(output.empty());
}
