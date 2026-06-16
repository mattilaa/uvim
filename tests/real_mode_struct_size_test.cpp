#include "real_mode_test_utils.h"

using namespace uvim_test;

TEST(RealModeTransitionsTest, GsShowsStructSizePopup)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"struct Foo { int a; char b; };",
                                   "int main(){", "    Foo value{};",
                                   "    return value.a;", "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 7;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_TRUE(editor.symbolPopupModal);
    EXPECT_EQ(editor.symbolPopupText,
              "sizeof(Foo) = 8 bytes\na: 4 bytes\nb: 1 bytes");
}

TEST(RealModeTransitionsTest, GsShowsLocalVariableSizePopup)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"struct Foo { int a; char b; };",
                                   "int main(){", "    Foo value{};",
                                   "    return value.a;", "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 3;
    *editor.cursorX = 11;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_TRUE(editor.symbolPopupModal);
    EXPECT_EQ(editor.symbolPopupText,
              "sizeof(value) = 8 bytes\na: 4 bytes\nb: 1 bytes");
}

TEST(RealModeTransitionsTest, GsResolvesQualifiedTemplateTypeAtCursor)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"#include <optional>", "int main(){",
                                   "    std::optional<int> value{};",
                                   "    return value.value_or(0);", "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 2;
    *editor.cursorX = 9;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_TRUE(editor.symbolPopupModal);
    EXPECT_NE(editor.symbolPopupText.find("sizeof(std::optional<int>) = "),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find(" bytes"), std::string::npos);
    EXPECT_EQ(editor.symbolPopupText.find("?"), std::string::npos);
}

TEST(RealModeTransitionsTest, GsOmitsMembersWithUnresolvedSize)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"struct Flags { unsigned a:3; int b; };",
                                   "int main(){", "    Flags value{};",
                                   "    return value.b;", "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 7;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_NE(editor.symbolPopupText.find("sizeof(Flags) = "),
              std::string::npos);
    EXPECT_EQ(editor.symbolPopupText.find("a: ?"), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("b: 4 bytes"), std::string::npos);
}

TEST(RealModeTransitionsTest, GsShowsPaddingBetweenMembers)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"struct Padded { char a; int b; char c; };",
                                   "int main(){", "    Padded value{};",
                                   "    return value.b;", "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 7;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_EQ(editor.symbolPopupText,
              "sizeof(Padded) = 12 bytes\na: 1 bytes\npad: 3 bytes\nb: 4 "
              "bytes\nc: 1 bytes");
}

TEST(RealModeTransitionsTest, GsShowsMembersForStructWithMethods)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"#include <string>",
                                   "#include <unordered_set>",
                                   "#include <vector>",
                                   "struct CommandOutputMode",
                                   "{",
                                   "    static constexpr const char* name()",
                                   "    {",
                                   "        return \"RUN\";",
                                   "    }",
                                   "",
                                   "    std::string command;",
                                   "    std::vector<std::string> lines;",
                                   "    int cursor = 0;",
                                   "    int offset = 0;",
                                   "    bool visualMode = false;",
                                   "    std::unordered_set<int> selectedLines;",
                                   "",
                                   "private:",
                                   "    int contentRows() const;",
                                   "};"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 3;
    *editor.cursorX = 8;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_NE(editor.symbolPopupText.find("sizeof(CommandOutputMode) = "),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("command: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("lines: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("cursor: 4 bytes"),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("offset: 4 bytes"),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("visualMode: 1 bytes"),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("selectedLines: "),
              std::string::npos);
    EXPECT_EQ(editor.symbolPopupText.find("contentRows"), std::string::npos);
}

TEST(RealModeTransitionsTest, GsShowsMultipleNestedStructMembers)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"#include <string>",
                                   "#include <vector>",
                                   "struct BufferLike",
                                   "{",
                                   "    struct BlameEntry",
                                   "    {",
                                   "        std::string hash;",
                                   "        std::string author;",
                                   "        bool valid = false;",
                                   "    };",
                                   "",
                                   "    struct EditState",
                                   "    {",
                                   "        std::vector<std::string> lines;",
                                   "        int cursorX = 0;",
                                   "        int cursorY = 0;",
                                   "        BlameEntry current;",
                                   "    };",
                                   "",
                                   "    BlameEntry blame;",
                                   "    EditState state;",
                                   "    int index = 0;",
                                   "};"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 2;
    *editor.cursorX = 8;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_NE(editor.symbolPopupText.find("sizeof(BufferLike) = "),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("BlameEntry: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("  hash: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("  author: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("  valid: 1 bytes"),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("EditState: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("  cursorX: 4 bytes"),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("  cursorY: 4 bytes"),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("  current: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("    hash: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("blame: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("state: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("index: 4 bytes"), std::string::npos);
}

TEST(RealModeTransitionsTest, GsExpandsMembersFromLocalIncludedHeaders)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    const std::filesystem::path root = make_temp_dir("uvim_gs_include_");
    write_file(root / "syntax_state.h", "#pragma once\n"
                                        "struct CppMethodScanState\n"
                                        "{\n"
                                        "    bool inBlockComment = false;\n"
                                        "    bool inMethod = false;\n"
                                        "    int braceDepth = 0;\n"
                                        "};\n");

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"#include \"syntax_state.h\"",
                                   "struct BufferLike",
                                   "{",
                                   "    struct SyntaxCacheLine",
                                   "    {",
                                   "        bool valid = false;",
                                   "        CppMethodScanState methodState;",
                                   "    };",
                                   "",
                                   "    SyntaxCacheLine cache;",
                                   "};"};
    set_buffer_filename(editor, (root / "buffer_like.h").string());
    *editor.cursorY = 1;
    *editor.cursorX = 8;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_NE(editor.symbolPopupText.find("SyntaxCacheLine: "),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("  methodState: "),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("    inBlockComment: 1 bytes"),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("    inMethod: 1 bytes"),
              std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("    braceDepth: 4 bytes"),
              std::string::npos);
}

TEST(RealModeTransitionsTest, GsShowsMarkerWhenNestedStructLimitIsReached)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"struct Outer",
                                   "{",
                                   "    struct Level1",
                                   "    {",
                                   "        struct Level2",
                                   "        {",
                                   "            struct Level3",
                                   "            {",
                                   "                struct Level4",
                                   "                {",
                                   "                    int tooDeep = 0;",
                                   "                };",
                                   "                Level4 next;",
                                   "            };",
                                   "            Level3 next;",
                                   "        };",
                                   "        Level2 next;",
                                   "    };",
                                   "    Level1 next;",
                                   "};"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 8;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_NE(editor.symbolPopupText.find("Level1: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("  Level2: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("    Level3: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("      Level4: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("        ..."), std::string::npos);
    EXPECT_EQ(editor.symbolPopupText.find("tooDeep: "), std::string::npos);
}

TEST(RealModeTransitionsTest, GsSizePopupIsModalAndClosesWithQ)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"struct Foo { int a; char b; };",
                                   "int main(){", "    Foo value{};",
                                   "    return value.a;", "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 7;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');
    ASSERT_TRUE(editor.symbolPopupActive);
    editor.needsFullRedraw = false;

    sm.dispatch('j');
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_FALSE(editor.needsFullRedraw);

    sm.dispatch('q');
    EXPECT_FALSE(editor.symbolPopupActive);
    EXPECT_FALSE(editor.symbolPopupModal);
    EXPECT_TRUE(editor.needsFullRedraw);
}

TEST(RealModeTransitionsTest, GsSizePopupScrollsWithJAndK)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int value;"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    editor.screenRows = 5;
    editor.symbolPopupActive = true;
    editor.symbolPopupModal = true;
    editor.symbolPopupText = "total\na\nb\nc\nd";
    editor.symbolPopupCursorX = 0;
    editor.symbolPopupCursorY = 0;
    *editor.cursorX = 0;
    *editor.cursorY = 0;
    auto sm = makeMachine(editor, NormalMode{});

    editor.needsFullRedraw = false;
    sm.dispatch('j');
    EXPECT_EQ(editor.symbolPopupScroll, 1);
    EXPECT_EQ(*editor.cursorY, 0);
    EXPECT_TRUE(editor.needsFullRedraw);

    editor.needsFullRedraw = false;
    sm.dispatch('j');
    sm.dispatch('j');
    EXPECT_EQ(editor.symbolPopupScroll, 2);
    EXPECT_TRUE(editor.needsFullRedraw);

    editor.needsFullRedraw = false;
    sm.dispatch('k');
    EXPECT_EQ(editor.symbolPopupScroll, 1);
    EXPECT_TRUE(editor.needsFullRedraw);
}

TEST(RealModeTransitionsTest, GsSizePopupClosesWithGs)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"struct Foo { int a; char b; };",
                                   "int main(){", "    Foo value{};",
                                   "    return value.a;", "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 7;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');
    ASSERT_TRUE(editor.symbolPopupActive);
    editor.needsFullRedraw = false;

    Terminal::unreadKey('s');
    sm.dispatch('g');
    EXPECT_FALSE(editor.symbolPopupActive);
    EXPECT_FALSE(editor.symbolPopupModal);
    EXPECT_TRUE(editor.needsFullRedraw);
}
