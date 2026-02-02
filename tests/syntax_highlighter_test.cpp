#include "editor.h"
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
        if(token.start == start && token.length == length &&
           token.type == type)
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
    editor.currentBuffer->lines = {
        "void Foo::method() {",
        "    bar = 1;",
        "}"};
    editor.syntaxCppHighlightImplicitMembers = true;

    std::string output;
    editor.renderLineWithSyntax(output, editor.currentBuffer->lines[1], 0,
                                (int)editor.currentBuffer->lines[1].size(), 1);

    const std::string memberColor = editor.theme.syntax(TOKEN_MEMBER);
    EXPECT_NE(output.find(memberColor), std::string::npos);
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
    EXPECT_NE(output.find(memberColor), std::string::npos);
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
    editor.currentBuffer->lines = {
        "void Foo::method() {",
        "    bar = 1;",
        "}"};
    editor.syntaxCppHighlightImplicitMembers = false;

    std::string output;
    editor.renderLineWithSyntax(output, editor.currentBuffer->lines[1], 0,
                                (int)editor.currentBuffer->lines[1].size(), 1);

    const std::string memberColor = editor.theme.syntax(TOKEN_MEMBER);
    EXPECT_EQ(output.find(memberColor), std::string::npos);
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
    auto tokens = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                      tomlQuote, inMarkupFence, markupFenceChar,
                                      false, false, true);

    int fooPos = (int)line.find("Foo");
    ASSERT_NE(fooPos, (int)std::string::npos);
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
    auto tokens = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                      tomlQuote, inMarkupFence, markupFenceChar);

    int fooPos = (int)line.find("Foo");
    int barPos = (int)line.find("Bar");
    int bazPos = (int)line.find("Baz");
    ASSERT_NE(fooPos, (int)std::string::npos);
    ASSERT_NE(barPos, (int)std::string::npos);
    ASSERT_NE(bazPos, (int)std::string::npos);
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
    auto tokens = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                      tomlQuote, inMarkupFence, markupFenceChar);

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
    auto tokens = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                      tomlQuote, inMarkupFence, markupFenceChar);

    int vecPos = (int)line.find("vector");
    ASSERT_NE(vecPos, (int)std::string::npos);
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
    auto tokens = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                      tomlQuote, inMarkupFence, markupFenceChar);

    int optionalPos = (int)line.find("optional");
    int summaryPos = (int)line.find("LspDiagnosticSummary");
    ASSERT_NE(optionalPos, (int)std::string::npos);
    ASSERT_NE(summaryPos, (int)std::string::npos);
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
    ASSERT_NE(uniquePos, (int)std::string::npos);
    ASSERT_NE(typePos, (int)std::string::npos);

    editor.currentBuffer->lspSemanticTokens[0].push_back(
        {uniquePos, 10, TOKEN_TYPE});
    editor.currentBuffer->lspSemanticTokens[0].push_back(
        {typePos, 17, TOKEN_TYPE});

    std::string output;
    editor.renderLineWithSyntax(output, line, 0, (int)line.size(), 0);

    const std::string typeColor = editor.theme.syntax(TOKEN_TYPE);
    EXPECT_NE(output.find(typeColor), std::string::npos);
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
    auto tokens = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                      tomlQuote, inMarkupFence, markupFenceChar);

    int start = (int)line.find('<');
    int end = (int)line.find('>');
    ASSERT_NE(start, (int)std::string::npos);
    ASSERT_NE(end, (int)std::string::npos);
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
    auto tokens = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                      tomlQuote, inMarkupFence, markupFenceChar);

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
    auto tokens = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                      tomlQuote, inMarkupFence, markupFenceChar);

    int typePos = (int)line.find("ModeContext");
    ASSERT_NE(typePos, (int)std::string::npos);
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
    auto tokens = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                      tomlQuote, inMarkupFence, markupFenceChar);

    int typePos = (int)line.find("ModeContext");
    ASSERT_NE(typePos, (int)std::string::npos);
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
    auto tokens = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                      tomlQuote, inMarkupFence, markupFenceChar);

    int typePos = (int)line.find("ModeContext");
    ASSERT_NE(typePos, (int)std::string::npos);
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
    auto tokens = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                      tomlQuote, inMarkupFence, markupFenceChar);

    int barPos = (int)line.find("bar");
    ASSERT_NE(barPos, (int)std::string::npos);
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
    editor.currentBuffer->lines = {"void Foo::bar() {", "    int key = 0;", "}"};

    std::string output;
    editor.renderLineWithSyntax(output, editor.currentBuffer->lines[1], 0,
                                (int)editor.currentBuffer->lines[1].size(), 1);

    const std::string memberColor = editor.theme.syntax(TOKEN_MEMBER);
    EXPECT_EQ(output.find(memberColor), std::string::npos);
}

TEST(SyntaxHighlighterTest, DoesNotHighlightLocalVariableInFreeFunction)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.cpp";
    editor.currentBuffer->lines = {"int foo() {", "    int value = 0;", "}"};

    std::string output;
    editor.renderLineWithSyntax(output, editor.currentBuffer->lines[1], 0,
                                (int)editor.currentBuffer->lines[1].size(), 1);

    const std::string memberColor = editor.theme.syntax(TOKEN_MEMBER);
    EXPECT_EQ(output.find(memberColor), std::string::npos);
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
    editor.currentBuffer->lines = {"void Foo::bar() {",
                                   "    std::string rest = \"\";",
                                   "}"};

    std::string output;
    editor.renderLineWithSyntax(output, editor.currentBuffer->lines[1], 0,
                                (int)editor.currentBuffer->lines[1].size(), 1);

    const std::string memberColor = editor.theme.syntax(TOKEN_MEMBER);
    EXPECT_EQ(output.find(memberColor), std::string::npos);
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
    auto tokens = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                      tomlQuote, inMarkupFence, markupFenceChar);

    int typePos = (int)line.find("Editor");
    ASSERT_NE(typePos, (int)std::string::npos);
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
    editor.currentBuffer->lines = {"void Foo::bar() {",
                                   "    std::string cmd = \"\";",
                                   "}"};

    std::string output;
    editor.renderLineWithSyntax(output, editor.currentBuffer->lines[1], 0,
                                (int)editor.currentBuffer->lines[1].size(), 1);

    const std::string memberColor = editor.theme.syntax(TOKEN_MEMBER);
    EXPECT_EQ(output.find(memberColor), std::string::npos);
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

    const std::string line = "Formatter::Formatter(Editor* editor) : editor(editor) {}";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                      tomlQuote, inMarkupFence, markupFenceChar);

    int namePos = (int)line.find("editor)");
    ASSERT_NE(namePos, (int)std::string::npos);
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
    auto tokens = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                      tomlQuote, inMarkupFence, markupFenceChar);

    int defPos = (int)line.find("def");
    int fooPos = (int)line.find("foo");
    int numPos = (int)line.find("42");
    int commentPos = (int)line.find("#");
    ASSERT_NE(defPos, (int)std::string::npos);
    ASSERT_NE(fooPos, (int)std::string::npos);
    ASSERT_NE(numPos, (int)std::string::npos);
    ASSERT_NE(commentPos, (int)std::string::npos);
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
    auto tokens = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                      tomlQuote, inMarkupFence, markupFenceChar);

    int classPos = (int)line.find("class");
    int fooPos = (int)line.find("Foo");
    ASSERT_NE(classPos, (int)std::string::npos);
    ASSERT_NE(fooPos, (int)std::string::npos);
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
    auto tokens = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                      tomlQuote, inMarkupFence, markupFenceChar);

    int childPos = (int)line.find("child");
    ASSERT_NE(childPos, (int)std::string::npos);
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
    auto tokens = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                      tomlQuote, inMarkupFence, markupFenceChar);

    int intPos = (int)line.find("int");
    int nonePos = (int)line.find("None");
    ASSERT_NE(intPos, (int)std::string::npos);
    ASSERT_NE(nonePos, (int)std::string::npos);
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
    auto tokens = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                      tomlQuote, inMarkupFence, markupFenceChar);

    int redPos = (int)line.find("RED");
    ASSERT_NE(redPos, (int)std::string::npos);
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
    auto tokens = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                      tomlQuote, inMarkupFence, markupFenceChar);

    int valuePos = (int)line.find("value");
    ASSERT_NE(valuePos, (int)std::string::npos);
    EXPECT_TRUE(hasTokenAt(tokens, valuePos, 5, TOKEN_MEMBER));
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
    auto tokens = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                      tomlQuote, inMarkupFence, markupFenceChar);

    int fooPos = (int)line.find("Foo");
    ASSERT_NE(fooPos, (int)std::string::npos);
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
    auto tokens = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                      tomlQuote, inMarkupFence, markupFenceChar);

    int fooPos = (int)line.find("Foo");
    ASSERT_NE(fooPos, (int)std::string::npos);
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
    auto tokensOn = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                        tomlQuote, inMarkupFence,
                                        markupFenceChar);
    int typePos = (int)line.find("int");
    ASSERT_NE(typePos, (int)std::string::npos);
    EXPECT_TRUE(hasTokenAt(tokensOn, typePos, 3, TOKEN_TYPE));

    editor.syntaxMlangHighlightTypes = false;
    auto tokensOff = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                         tomlQuote, inMarkupFence,
                                         markupFenceChar);
    EXPECT_FALSE(hasTokenAt(tokensOff, typePos, 3, TOKEN_TYPE));
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
    auto tokens = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                      tomlQuote, inMarkupFence, markupFenceChar);

    int timeoutPos = (int)line.find("TIMEOUT");
    ASSERT_NE(timeoutPos, (int)std::string::npos);
    EXPECT_TRUE(hasTokenAt(tokens, timeoutPos, 7, TOKEN_MEMBER));
}

TEST(SyntaxHighlighterTest, DoesNotHighlightLambdaParamNames)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);
    *editor.filename = "/tmp/example.cpp";
    const std::string line =
        "auto appendLsp = [&](const std::string& label, bool running, bool activeForFile,";
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    auto tokens = editor.tokenizeLine(line, inBlockComment, inTomlMultiline,
                                      tomlQuote, inMarkupFence, markupFenceChar);

    int labelPos = (int)line.find("label");
    int runningPos = (int)line.find("running");
    int activePos = (int)line.find("activeForFile");
    ASSERT_NE(labelPos, (int)std::string::npos);
    ASSERT_NE(runningPos, (int)std::string::npos);
    ASSERT_NE(activePos, (int)std::string::npos);
    EXPECT_FALSE(hasTokenAt(tokens, labelPos, 5, TOKEN_MEMBER));
    EXPECT_FALSE(hasTokenAt(tokens, runningPos, 7, TOKEN_MEMBER));
    EXPECT_FALSE(hasTokenAt(tokens, activePos, 13, TOKEN_MEMBER));
}
