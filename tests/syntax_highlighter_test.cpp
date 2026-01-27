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
                                      tomlQuote, inMarkupFence, markupFenceChar);

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
