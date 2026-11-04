#include "cursor_call.h"
#include "json_utils.h"
#include "mlang_size_probe.h"
#include "real_mode_test_utils.h"
#include "symbol_popup_layout.h"
#include "widgets/command_popup.h"
#include <clocale>
#include <regex>
#include <tuple>

using namespace uvim_test;

TEST(RealModeTransitionsTest, GsShowsStructSizePopup)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines =
        {"struct Foo { int a; char b; };",
         "int main(){",
         "    Foo value{};",
         "    return value.a;",
         "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 7;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_TRUE(editor.symbolPopupModal);
    EXPECT_EQ(
        editor.symbolPopupText,
        "sizeof(Foo) = 8 bytes\na: 4 bytes\nb: 1 bytes\npad: 3 bytes");
}

TEST(RealModeTransitionsTest, GsShowsLocalVariableSizePopup)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines =
        {"struct Foo { int a; char b; };",
         "int main(){",
         "    Foo value{};",
         "    return value.a;",
         "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 3;
    *editor.cursorX = 11;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_TRUE(editor.symbolPopupModal);
    EXPECT_EQ(
        editor.symbolPopupText,
        "sizeof(value) = 8 bytes\na: 4 bytes\nb: 1 bytes\npad: 3 bytes");
}

TEST(RealModeTransitionsTest, GsResolvesQualifiedTemplateTypeAtCursor)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines =
        {"#include <optional>",
         "int main(){",
         "    std::optional<int> value{};",
         "    return value.value_or(0);",
         "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 2;
    *editor.cursorX = 9;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_TRUE(editor.symbolPopupModal);
    EXPECT_NE(
        editor.symbolPopupText.find("sizeof(std::optional<int>) = "),
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
    editor.currentBuffer->lines =
        {"struct Flags { unsigned a:3; int b; };",
         "int main(){",
         "    Flags value{};",
         "    return value.b;",
         "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 7;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_NE(
        editor.symbolPopupText.find("sizeof(Flags) = "),
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
    editor.currentBuffer->lines =
        {"struct Padded { char a; int b; char c; };",
         "int main(){",
         "    Padded value{};",
         "    return value.b;",
         "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_probe_test.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 7;
    auto sm = makeMachine(editor, NormalMode{});

    Terminal::unreadKey('s');
    sm.dispatch('g');

    EXPECT_TRUE(editor.symbolPopupActive);
    EXPECT_EQ(
        editor.symbolPopupText,
        "sizeof(Padded) = 12 bytes\na: 1 bytes\npad: 3 bytes\nb: 4 "
        "bytes\nc: 1 bytes\npad: 3 bytes");
}

TEST(RealModeTransitionsTest, GsShowsMembersForStructWithMethods)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines =
        {"#include <string>",
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
    EXPECT_NE(
        editor.symbolPopupText.find("sizeof(CommandOutputMode) = "),
        std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("command: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("lines: "), std::string::npos);
    EXPECT_NE(
        editor.symbolPopupText.find("cursor: 4 bytes"),
        std::string::npos);
    EXPECT_NE(
        editor.symbolPopupText.find("offset: 4 bytes"),
        std::string::npos);
    EXPECT_NE(
        editor.symbolPopupText.find("visualMode: 1 bytes"),
        std::string::npos);
    EXPECT_NE(
        editor.symbolPopupText.find("selectedLines: "),
        std::string::npos);
    EXPECT_EQ(editor.symbolPopupText.find("contentRows"), std::string::npos);
}

TEST(RealModeTransitionsTest, GsShowsMultipleNestedStructMembers)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines =
        {"#include <string>",
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
    EXPECT_NE(
        editor.symbolPopupText.find("sizeof(BufferLike) = "),
        std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("BlameEntry: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("  hash: "), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("  author: "), std::string::npos);
    EXPECT_NE(
        editor.symbolPopupText.find("  valid: 1 bytes"),
        std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("EditState: "), std::string::npos);
    EXPECT_NE(
        editor.symbolPopupText.find("  cursorX: 4 bytes"),
        std::string::npos);
    EXPECT_NE(
        editor.symbolPopupText.find("  cursorY: 4 bytes"),
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
    write_file(
        root / "syntax_state.h",
        "#pragma once\n"
        "struct CppMethodScanState\n"
        "{\n"
        "    bool inBlockComment = false;\n"
        "    bool inMethod = false;\n"
        "    int braceDepth = 0;\n"
        "};\n");

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines =
        {"#include \"syntax_state.h\"",
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
    EXPECT_NE(
        editor.symbolPopupText.find("SyntaxCacheLine: "),
        std::string::npos);
    EXPECT_NE(
        editor.symbolPopupText.find("  methodState: "),
        std::string::npos);
    EXPECT_NE(
        editor.symbolPopupText.find("    inBlockComment: 1 bytes"),
        std::string::npos);
    EXPECT_NE(
        editor.symbolPopupText.find("    inMethod: 1 bytes"),
        std::string::npos);
    EXPECT_NE(
        editor.symbolPopupText.find("    braceDepth: 4 bytes"),
        std::string::npos);
}

TEST(RealModeTransitionsTest, GsShowsMarkerWhenNestedStructLimitIsReached)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";

    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines =
        {"struct Outer",
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
    editor.currentBuffer->lines =
        {"struct Foo { int a; char b; };",
         "int main(){",
         "    Foo value{};",
         "    return value.a;",
         "}"};
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
    editor.currentBuffer->lines =
        {"struct Foo { int a; char b; };",
         "int main(){",
         "    Foo value{};",
         "    return value.a;",
         "}"};
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

TEST(RealModeTransitionsTest, GsCommandBrowsesMemberOffsetsAndByteMap)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"struct Padded { char a; int b; char c; };"};
    set_buffer_filename(editor, "/tmp/uvim_size_browse_test.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 7;
    auto sm = makeMachine(editor, NormalMode{});
    dispatch_command(sm, "gs");
    ASSERT_TRUE(editor.symbolPopupActive);
    ASSERT_TRUE(editor.symbolPopupModal);
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    EXPECT_EQ(editor.sizePopupSelection, 0);
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_NE(
        editor.symbolPopupText.find("Selected: a | 1 bytes | offset +0x0"),
        std::string::npos);
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_NE(
        editor.symbolPopupText.find(
            "Selected: <padding> | 3 bytes | offset +0x1"),
        std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("+0x1: -- -- --"), std::string::npos);
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_NE(
        editor.symbolPopupText.find("Selected: b | 4 bytes | offset +0x4"),
        std::string::npos);
    sm.dispatch(keyCode(control::ControlKey::CTRL_K));
    EXPECT_EQ(editor.sizePopupSelection, 2);
    sm.dispatch('q');
    EXPECT_TRUE(editor.sizePopupEntries.empty());
}

TEST(RealModeTransitionsTest, GsPrimitiveShowsBytesWithoutStructMembers)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"int value = 42;"};
    set_buffer_filename(editor, "/tmp/uvim_size_primitive.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 4;
    editor.executeCommand("gs");
    ASSERT_TRUE(editor.symbolPopupActive);
    EXPECT_EQ(editor.symbolPopupText, "sizeof(value) = 4 bytes");
    editor.browseSizePopup(0);
    EXPECT_NE(
        editor.symbolPopupText.find("+0x0: ## ## ## ##"),
        std::string::npos);
    EXPECT_NE(
        editor.symbolPopupText.find("Raw hex: available with :ss"),
        std::string::npos);
}

TEST(RealModeTransitionsTest, SizePopupUsesThemeColorsForMembersAndPadding)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.screenCols = 100;
    editor.screenRows = 24;
    editor.symbolPopupActive = editor.symbolPopupModal = true;
    editor.symbolPopupText = "sizeof(Foo) = 8 bytes\na: 1 bytes\npad: 3 bytes";
    std::string output;
    editor.drawSymbolPopup(output);
    EXPECT_NE(
        output.find(editor.theme.uiError() + "pad: 3 bytes"),
        std::string::npos);
    EXPECT_NE(output.find(editor.theme.uiInfo() + "a"), std::string::npos);
}

TEST(RealModeTransitionsTest, SsInNormalModeExplainsDebuggerRequirement)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.executeCommand("ss");
    EXPECT_FALSE(editor.symbolPopupActive);
    EXPECT_NE(
        editor.statusMessage.find("pause the debugger"),
        std::string::npos);
}

TEST(RealModeTransitionsTest, GsShowsMemberExpressionSize)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines =
        {"struct Foo { int a; char b; };",
         "int main() {",
         "Foo value{};",
         "value.b = 1;",
         "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_member.cpp");
    *editor.cursorY = 3;
    *editor.cursorX = 6;
    editor.executeCommand("gs");
    ASSERT_TRUE(editor.symbolPopupActive);
    EXPECT_EQ(editor.symbolPopupText, "sizeof(value.b) = 1 bytes");
}

TEST(RealModeTransitionsTest, GsKeepsMembersBeyondClangDiagnosticLimit)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"struct Many {"};
    for(int i = 0; i < 32; ++i)
        editor.currentBuffer->lines.push_back(
            "int member" + std::to_string(i) + ";");
    editor.currentBuffer->lines.push_back("};");
    set_buffer_filename(editor, "/tmp/uvim_size_many.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 7;
    editor.executeCommand("gs");
    ASSERT_TRUE(editor.symbolPopupActive);
    EXPECT_NE(
        editor.symbolPopupText.find("member31: 4 bytes"),
        std::string::npos);
    ASSERT_EQ(editor.sizePopupEntries.size(), 33u);
    EXPECT_EQ(editor.sizePopupEntries.back().offset, 124);
}

namespace
{
const std::string mlangLayoutMetadata = R"(
!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1)
!1 = !DIFile(filename: "sample.mla", directory: "/tmp")
!2 = distinct !DISubprogram(name: "main", file: !1, line: 4)
!3 = !DIBasicType(name: "u8", size: 8)
!4 = !DIBasicType(name: "i32", size: 32)
!5 = !DIBasicType(name: "f64", size: 64)
!6 = !DICompositeType(tag: DW_TAG_structure_type, name: "Padded", size: 96, elements: !7)
!7 = !{!8, !9, !10}
!8 = !DIDerivedType(tag: DW_TAG_member, name: "tag", baseType: !3, size: 8)
!9 = !DIDerivedType(tag: DW_TAG_member, name: "value", baseType: !4, size: 32, offset: 32)
!10 = !DIDerivedType(tag: DW_TAG_member, name: "tail", baseType: !3, size: 8, offset: 64)
!11 = !DICompositeType(tag: DW_TAG_structure_type, name: "Outer", size: 160, elements: !12)
!12 = !{!13, !14, !15}
!13 = !DIDerivedType(tag: DW_TAG_member, name: "tag", baseType: !3, size: 8)
!14 = !DIDerivedType(tag: DW_TAG_member, name: "child", baseType: !6, size: 96, offset: 32)
!15 = !DIDerivedType(tag: DW_TAG_member, name: "tail", baseType: !3, size: 8, offset: 128)
!16 = !DILocalVariable(name: "item", scope: !2, file: !1, line: 5, type: !11)
!17 = !DILocalVariable(name: "scalar", scope: !2, file: !1, line: 6, type: !5)
!18 = distinct !DISubprogram(name: "other", file: !1, line: 20)
!19 = !DILocalVariable(name: "item", scope: !18, file: !1, line: 21, type: !4)
!20 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !6, size: 64)
!21 = !DILocalVariable(name: "pointer", scope: !2, file: !1, line: 7, type: !20)
!22 = !DIDerivedType(tag: DW_TAG_typedef, name: "Alias", baseType: !11)
!23 = !DICompositeType(tag: DW_TAG_structure_type, name: "Flags", size: 8, elements: !24)
!24 = !{!25, !26}
!25 = !DIDerivedType(tag: DW_TAG_member, name: "a", baseType: !3, size: 1, flags: DIFlagBitField)
!26 = !DIDerivedType(tag: DW_TAG_member, name: "b", baseType: !3, size: 1, offset: 1, flags: DIFlagBitField)
)";
}

TEST(MlangSizeMetadataTest, ResolvesTypesVariablesAndNestedMemberOffsets)
{
    auto layout = parseDebugTypeMetadata(mlangLayoutMetadata, "item", 8);
    ASSERT_TRUE(layout);
    EXPECT_EQ(layout->sizeBits, 160);
    ASSERT_EQ(layout->members.size(), 3u);
    EXPECT_EQ(layout->members[1].name, "child");
    EXPECT_EQ(layout->members[1].offsetBits, 32);
    ASSERT_EQ(layout->members[1].children.size(), 3u);
    EXPECT_EQ(layout->members[1].children[2].offsetBits, 64);
    EXPECT_TRUE(layout->complete);
    auto child =
        parseDebugTypeMetadata(mlangLayoutMetadata, "item.child.value", 8);
    ASSERT_TRUE(child);
    EXPECT_EQ(child->sizeBits, 32);
    auto alias = parseDebugTypeMetadata(mlangLayoutMetadata, "Alias", 8);
    ASSERT_TRUE(alias);
    EXPECT_EQ(alias->sizeBits, 160);
    EXPECT_FALSE(
        parseDebugTypeMetadata(mlangLayoutMetadata, "item.missing", 8));
}

TEST(MlangSizeMetadataTest, KeepsScalarsPointersAndFunctionScopesSeparate)
{
    auto scalar = parseDebugTypeMetadata(mlangLayoutMetadata, "scalar", 8);
    ASSERT_TRUE(scalar);
    EXPECT_EQ(scalar->sizeBits, 64);
    EXPECT_TRUE(scalar->members.empty());
    auto pointer = parseDebugTypeMetadata(mlangLayoutMetadata, "pointer", 8);
    ASSERT_TRUE(pointer);
    EXPECT_EQ(pointer->sizeBits, 64);
    EXPECT_TRUE(pointer->members.empty());
    EXPECT_FALSE(parseDebugTypeMetadata(mlangLayoutMetadata, "scalar", 22));
    auto other = parseDebugTypeMetadata(mlangLayoutMetadata, "item", 22);
    ASSERT_TRUE(other);
    EXPECT_EQ(other->sizeBits, 32);
}

TEST(MlangSizeMetadataTest, IgnoresShadowedVariablesFromExitedLexicalScopes)
{
    auto metadata = mlangLayoutMetadata + R"(
!27 = distinct !DILexicalBlock(scope: !2, file: !1, line: 9)
!28 = !DILocalVariable(name: "item", scope: !27, file: !1, line: 10, type: !4)
!29 = !DILocation(line: 12, column: 5, scope: !2)
)";
    auto layout = parseDebugTypeMetadata(metadata, "item", 12);
    ASSERT_TRUE(layout);
    EXPECT_EQ(layout->sizeBits, 160);
}

TEST(MlangSizeMetadataTest, PreservesPackedBitSizesAndOffsets)
{
    auto flags = parseDebugTypeMetadata(mlangLayoutMetadata, "Flags", 8);
    ASSERT_TRUE(flags);
    EXPECT_EQ(flags->sizeBits, 8);
    ASSERT_EQ(flags->members.size(), 2u);
    EXPECT_EQ(flags->members[1].sizeBits, 1);
    EXPECT_EQ(flags->members[1].offsetBits, 1);
    EXPECT_FALSE(parseDebugTypeMetadata("", "i32", 1));
}

TEST(RealModeTransitionsTest, GsMlangReportsCompilerFailureAndCleansSnapshot)
{
    const auto root = make_temp_dir("uvim_mlang_size_error_");
    {
        ScopedEnv compiler(
            "UVIM_MLANG_COMPILER",
            (root / "missing-compiler").string());
        Editor editor = Editor::createForTests();
        editor.createNewBuffer();
        editor.currentBuffer->lines = {"fn main() -> i32 { return 0; }"};
        set_buffer_filename(editor, (root / "sample.mla").string());
        *editor.cursorY = 0;
        *editor.cursorX = 13;
        editor.executeCommand("gs");
        EXPECT_FALSE(editor.symbolPopupActive);
        EXPECT_NE(
            editor.statusMessage.find("mlang failed:"),
            std::string::npos);
    }
    EXPECT_TRUE(std::filesystem::is_empty(root));
    std::filesystem::remove_all(root);
}

TEST(RealModeTransitionsTest, GsMlangLiveCompilerInspectsUnsavedStructAndMember)
{
    if(!std::getenv("UVIM_MLANG_SIZE_LIVE_TESTS"))
        GTEST_SKIP() << "Set UVIM_MLANG_SIZE_LIVE_TESTS and optionally "
                        "UVIM_MLANG_COMPILER";
    const auto root = make_temp_dir("uvim_mlang_size_live_");
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines =
        {"struct Padded { var tag: u8; var value: i32; var tail: u8; };",
         "fn main() -> i32 {",
         "    var item: Padded = Padded { tag: 1, value: 42, tail: 2 };",
         "    var scalar: f64 = 1.0;",
         "    return item.value;",
         "}"};
    set_buffer_filename(editor, (root / "unsaved.mla").string());
    auto sm = makeMachine(editor, NormalMode{});
    *editor.cursorY = 2;
    *editor.cursorX = 9;
    dispatch_command(sm, "gs");
    ASSERT_TRUE(editor.symbolPopupActive) << editor.statusMessage;
    EXPECT_EQ(
        editor.symbolPopupText,
        "storage(item) = 12 bytes\ntag: 1 bytes\npad: 3 bytes\nvalue: 4 "
        "bytes\ntail: 1 bytes\npad: 3 bytes");
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    EXPECT_NE(
        editor.symbolPopupText.find("Selected: tag | 1 bytes | offset +0x0"),
        std::string::npos);
    sm.dispatch('q');
    *editor.cursorY = 4;
    *editor.cursorX = 16;
    editor.executeCommand("gs");
    ASSERT_TRUE(editor.symbolPopupActive) << editor.statusMessage;
    EXPECT_EQ(editor.symbolPopupText, "storage(item.value) = 4 bytes");
    *editor.cursorY = 3;
    *editor.cursorX = 9;
    editor.executeCommand("gs");
    ASSERT_TRUE(editor.symbolPopupActive) << editor.statusMessage;
    EXPECT_EQ(editor.symbolPopupText, "storage(scalar) = 8 bytes");
    // Unused types must be materialized without executing user code.
    editor.currentBuffer->lines = {
        "struct Unused { var tag: u8; var count: i64; };"};
    *editor.cursorY = 0;
    *editor.cursorX = 8;
    editor.executeCommand("gs");
    ASSERT_TRUE(editor.symbolPopupActive) << editor.statusMessage;
    EXPECT_NE(
        editor.symbolPopupText.find("storage(Unused) = 16 bytes"),
        std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("pad: 7 bytes"), std::string::npos);
    EXPECT_TRUE(std::filesystem::is_empty(root));
    std::filesystem::remove_all(root);
}

TEST(
    RealModeTransitionsTest,
    GsMlangLiveCompilerHandlesImportsGenericsArraysAndBits)
{
    if(!std::getenv("UVIM_MLANG_SIZE_LIVE_TESTS"))
        GTEST_SKIP() << "Set UVIM_MLANG_SIZE_LIVE_TESTS and optionally "
                        "UVIM_MLANG_COMPILER";
    const auto root = make_temp_dir("uvim_mlang_types_");
    write_file(
        root / "types.mla",
        "pub struct Imported { var tag: u8; var count: i64; };\n");
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines =
        {"mod types;",
         "use types::Imported;",
         "struct Box<T> { var value: T; };",
         "struct Flags { var power: bit; var fault: bit; var armed: bool; };",
         "fn main() -> i32 {",
         "    var item: Box<Imported> = Box<Imported> { value: Imported { tag: "
         "1, count: 42 } };",
         "    var flags: Flags = Flags { power: 1, fault: 0, armed: true };",
         "    var values: array<i32, 3> = {1, 2, 3};",
         "    return 0;",
         "}"};
    set_buffer_filename(editor, (root / "sample.mla").string());
    *editor.cursorY = 5;
    *editor.cursorX = 9;
    editor.executeCommand("gs");
    ASSERT_TRUE(editor.symbolPopupActive) << editor.statusMessage;
    EXPECT_NE(
        editor.symbolPopupText.find("storage(item) = 16 bytes"),
        std::string::npos);
    EXPECT_NE(
        editor.symbolPopupText.find("  count: 8 bytes"),
        std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("  pad: 7 bytes"), std::string::npos);
    *editor.cursorY = 6;
    editor.executeCommand("gs");
    ASSERT_TRUE(editor.symbolPopupActive) << editor.statusMessage;
    EXPECT_NE(
        editor.symbolPopupText.find("power (1 bits at bit +0)"),
        std::string::npos);
    EXPECT_NE(
        editor.symbolPopupText.find("fault (1 bits at bit +1)"),
        std::string::npos);
    *editor.cursorY = 7;
    editor.executeCommand("gs");
    ASSERT_TRUE(editor.symbolPopupActive) << editor.statusMessage;
    EXPECT_EQ(
        editor.symbolPopupText,
        "storage(values) = 16 bytes\nlen: 8 bytes\ndata: 8 bytes");
    // An instantiated type at the cursor can require a synthetic parameter.
    editor.currentBuffer->lines =
        {"struct Box<T> { var value: T; };",
         "fn main() -> i32 {",
         "    var item: Box<i32> = Box<i32> { value: 42 };",
         "    return item.value;",
         "}"};
    *editor.cursorY = 2;
    *editor.cursorX = 15;
    editor.executeCommand("gs");
    ASSERT_TRUE(editor.symbolPopupActive) << editor.statusMessage;
    EXPECT_NE(
        editor.symbolPopupText.find("storage(Box<i32>) = 4 bytes"),
        std::string::npos);
    std::filesystem::remove_all(root);
}

TEST(
    RealModeTransitionsTest,
    GsStdStringUsesProjectIncludesDefinesAndLanguageStandard)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";
    const auto root = make_temp_dir("uvim_size_project_");
    write_file(
        root / "include folder" / "types.h",
        "#include <string>\n"
        "#ifndef SIZE_PROBE_PROJECT\n#error missing project define\n#endif\n"
        "#if __cplusplus != 201703L\n#error missing project language "
        "standard\n#endif\n");
    write_file(
        root / "sample.cpp",
        "// contents on disk must remain unchanged\n");
    for(bool commandString : {false, true})
    {
        std::string directory;
        json_utils::append_escaped(directory, root.string());
        const std::string entry =
            commandString
                ? R"("command":"clang++ -I'include folder' -DSIZE_PROBE_PROJECT -std=c++17 -c sample.cpp -o output.o")"
                : R"("arguments":["clang++","-I","include folder","-DSIZE_PROBE_PROJECT","-std=c++17","-c","sample.cpp","-o","output.o"])";
        write_file(
            root / "build" / "compile_commands.json",
            "[{\"directory\":" + directory + ",\"file\":\"sample.cpp\"," +
                entry + "}]");
        Editor editor = Editor::createForTests();
        editor.createNewBuffer();
        editor.currentBuffer->lines =
            {"#include <types.h>",
             "int main() {",
             "    std::string value;",
             "    return value.size();",
             "}"};
        set_buffer_filename(editor, (root / "sample.cpp").string());
        *editor.cursorY = 2;
        *editor.cursorX = 10;
        editor.executeCommand("gs");
        ASSERT_TRUE(editor.symbolPopupActive) << editor.statusMessage;
        EXPECT_TRUE(editor.symbolPopupText.starts_with(
            "sizeof(std::string) = " + std::to_string(sizeof(std::string)) +
            " bytes"));
        EXPECT_GT(editor.sizePopupEntries.size(), 1u);
        EXPECT_FALSE(std::filesystem::exists(root / "output.o"));
        EXPECT_EQ(
            read_file(root / "sample.cpp"),
            "// contents on disk must remain unchanged\n");
    }
    std::filesystem::remove_all(root);
}

TEST(RealModeTransitionsTest, GsReportsActualCompilerErrorForUnresolvableSource)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines =
        {"#include <uvim_missing_size_probe_header.h>", "std::string value;"};
    set_buffer_filename(editor, "/tmp/uvim_size_missing_include.cpp");
    *editor.cursorY = 1;
    *editor.cursorX = 7;
    editor.executeCommand("gs");
    EXPECT_FALSE(editor.symbolPopupActive);
    EXPECT_NE(
        editor.statusMessage.find(
            "uvim_missing_size_probe_header.h' file not found"),
        std::string::npos);
}

TEST(RealModeTransitionsTest, GsStdStringResolvesInsideUvimSource)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";
    const auto source =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "src" /
        "editor.cpp";
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines.clear();
    std::istringstream input(read_file(source));
    std::string line;
    int row = -1, column = 0;
    while(std::getline(input, line))
    {
        if(row < 0)
            if(const auto pos = line.find("std::string");
               pos != std::string::npos)
            {
                row = static_cast<int>(editor.currentBuffer->lines.size());
                column = static_cast<int>(pos) + 6;
            }
        editor.currentBuffer->lines.push_back(line);
    }
    ASSERT_GE(row, 0);
    set_buffer_filename(editor, source.string());
    *editor.cursorY = row;
    *editor.cursorX = column;
    editor.executeCommand("gs");
    ASSERT_TRUE(editor.symbolPopupActive) << editor.statusMessage;
    EXPECT_TRUE(editor.symbolPopupText.starts_with(
        "sizeof(std::string) = " + std::to_string(sizeof(std::string)) +
        " bytes"));
    EXPECT_GT(editor.sizePopupEntries.size(), 1u);
}

TEST(
    RealModeTransitionsTest,
    GsStringMethodsInspectReturnValuesInUnbracedIfElse)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines =
        {"#include <string>",
         "#include <string_view>",
         "std::string displayText(std::string_view line)",
         "{",
         "    std::string out;",
         "    for(unsigned char c : line)",
         "    {",
         "        if(c == '\\t')",
         "            out.append(4 - out.size() % 4, ' ');",
         "        else",
         "            out += c < 32 || c == 127 ? '?' : static_cast<char>(c);",
         "    }",
         "    return out;",
         "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_method_receiver.cpp");
    const auto original = editor.currentBuffer->lines;
    *editor.cursorY = 8;
    for(const std::string method : {"append", "size"})
    {
        *editor.cursorX = static_cast<int>(original[8].find(method));
        editor.executeCommand("gs");
        ASSERT_TRUE(editor.symbolPopupActive) << editor.statusMessage;
        const bool append = method == "append";
        EXPECT_TRUE(editor.symbolPopupText.starts_with(
            append ? "Return value of out.append(4 - out.size() % 4, ' '): " +
                         std::to_string(sizeof(std::string)) + " bytes"
                   : "Return value of out.size(): " +
                         std::to_string(sizeof(std::string::size_type)) +
                         " bytes"));
        if(append)
            EXPECT_GT(editor.sizePopupEntries.size(), 1u);
        else
            EXPECT_EQ(editor.sizePopupEntries.size(), 1u);
        EXPECT_EQ(editor.currentBuffer->lines, original);
    }
}

TEST(RealModeTransitionsTest, GsPointerMethodInspectsReturnedReferenceMembers)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines =
        {"struct Foo { int a; char b; Foo& change() { return *this; } };",
         "int main() {",
         "    Foo value{};",
         "    Foo* pointer = &value;",
         "    pointer->change();",
         "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_pointer_method.cpp");
    *editor.cursorY = 4;
    *editor.cursorX = 14;
    editor.executeCommand("gs");
    ASSERT_TRUE(editor.symbolPopupActive) << editor.statusMessage;
    EXPECT_TRUE(editor.symbolPopupText.starts_with(
        "Return value of pointer->change(): 8 bytes"));
    EXPECT_NE(editor.symbolPopupText.find("Foo&"), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("a: 4 bytes"), std::string::npos);
    EXPECT_NE(editor.symbolPopupText.find("pad: 3 bytes"), std::string::npos);
}

TEST(RealModeTransitionsTest, GsCallableDataMemberInspectsCallReturnValue)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines =
        {"int target() { return 42; }",
         "struct Callbacks { int tag; int (*callback)(); };",
         "int main() {",
         "    Callbacks value{0, target};",
         "    value.callback();",
         "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_callable_member.cpp");
    *editor.cursorY = 4;
    *editor.cursorX = 11;
    editor.executeCommand("gs");
    ASSERT_TRUE(editor.symbolPopupActive) << editor.statusMessage;
    EXPECT_TRUE(editor.symbolPopupText.starts_with(
        "Return value of value.callback(): " + std::to_string(sizeof(int)) +
        " bytes"));
}

TEST(RealModeTransitionsTest, SizeLayoutTableShowsBlankRedPaddingBetweenMembers)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.screenRows = 24;
    editor.screenCols = 100;
    editor.currentBuffer->lines = {"struct Padded { char a; int b; char c; };"};
    set_buffer_filename(editor, "/tmp/uvim_size_table.cpp");
    *editor.cursorY = 0;
    *editor.cursorX = 8;
    auto sm = makeMachine(editor, NormalMode{});
    dispatch_command(sm, "gs");
    ASSERT_TRUE(editor.symbolPopupActive) << editor.statusMessage;
    std::string output;
    editor.drawSymbolPopup(output);
    const auto memberA = output.find(editor.theme.uiInfo() + "a");
    const auto middleGap =
        output.find(editor.theme.storagePadding() + "  +0x1");
    const auto memberB = output.find(editor.theme.uiInfo() + "b");
    const auto tailGap = output.find(editor.theme.storagePadding() + "  +0x9");
    EXPECT_NE(output.find("Offset"), std::string::npos);
    EXPECT_NE(output.find("Bytes"), std::string::npos);
    EXPECT_NE(output.find("Member"), std::string::npos);
    ASSERT_NE(memberA, std::string::npos);
    ASSERT_NE(middleGap, std::string::npos);
    ASSERT_NE(memberB, std::string::npos);
    ASSERT_NE(tailGap, std::string::npos);
    EXPECT_LT(memberA, middleGap);
    EXPECT_LT(middleGap, memberB);
    EXPECT_LT(memberB, tailGap);
    const std::regex ansi("\x1b\\[[0-?]*[ -/]*[@-~]");
    const auto plain = std::regex_replace(output, ansi, "");
    EXPECT_TRUE(std::regex_search(plain, std::regex("\\+0x1 +3 +│")));
    EXPECT_EQ(plain.find("pad:"), std::string::npos);
    // Selection still points to the gap despite the two table heading rows.
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    sm.dispatch(keyCode(control::ControlKey::CTRL_J));
    output.clear();
    editor.drawSymbolPopup(output);
    EXPECT_NE(
        output.find(editor.theme.storagePadding() + "> +0x1"),
        std::string::npos);
    EXPECT_NE(
        editor.symbolPopupText.find("Selected: <padding>"),
        std::string::npos);
}

TEST(RealModeTransitionsTest, SizeLayoutTableScrollsPastItsHeadingRows)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.screenRows = 7;
    editor.symbolPopupActive = editor.symbolPopupModal = true;
    editor.symbolPopupText =
        "total\na: 1 bytes\npad: 3 bytes\nb: 4 bytes\nc: 1 bytes";
    editor.sizePopupEntries =
        {{0, "Padded", 0, 12, false},
         {1, "a", 0, 1, false},
         {2, "<padding>", 1, 3, true},
         {3, "b", 4, 4, false},
         {4, "c", 8, 1, false}};
    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('j');
    sm.dispatch('j');
    EXPECT_EQ(editor.symbolPopupScroll, 2);
    sm.dispatch('j');
    EXPECT_EQ(editor.symbolPopupScroll, 2);
    sm.dispatch('k');
    EXPECT_EQ(editor.symbolPopupScroll, 1);
}

TEST(MlangSizeMetadataTest, AcceptsEquivalentDebugFileRecordsFromClang)
{
    const auto metadata = mlangLayoutMetadata + R"(
!30 = !DIFile(filename: "/tmp/sample.mla", directory: "")
!31 = distinct !DISubprogram(name: "same_file", file: !30, line: 40)
!32 = !DILocalVariable(name: "duplicateFile", scope: !31, file: !30, line: 41, type: !6)
)";
    auto layout = parseDebugTypeMetadata(metadata, "duplicateFile", 42);
    ASSERT_TRUE(layout);
    EXPECT_EQ(layout->sizeBits, 96);
    EXPECT_EQ(layout->members.size(), 3u);
}

namespace
{
void setupGeometryPopup(Editor& editor)
{
    editor.createNewBuffer();
    const std::string longName =
        "nested." + std::string(180, 'x') + ".last_member";
    editor.symbolPopupActive = editor.symbolPopupModal = true;
    editor.symbolPopupText =
        "sizeof(Layout) = 520 bytes\nshort: 1 bytes\npad: 7 bytes\n" +
        longName + ": 512 bytes";
    editor.sizePopupBaseText = editor.symbolPopupText;
    editor.sizePopupSelection = 0;
    editor.sizePopupEntries =
        {{0, "Layout", 0, 520, false},
         {1, "short", 0, 1, false},
         {2, "<padding>", 1, 7, true},
         {3, longName, 8, 512, false}};
}

void expectPopupOutputFits(
    const std::string& output,
    int rows,
    int cols,
    bool unicode = false)
{
    const std::regex position("\x1b\\[([0-9]+);([0-9]+)H");
    const std::regex ansi("\x1b\\[[0-?]*[ -/]*[@-~]");
    std::vector<std::tuple<int, int, size_t>> runs;
    for(std::sregex_iterator it(output.begin(), output.end(), position), end;
        it != end;
        ++it)
        runs.emplace_back(
            std::stoi((*it)[1]),
            std::stoi((*it)[2]),
            static_cast<size_t>(it->position() + it->length()));
    for(size_t i = 0; i < runs.size(); ++i)
    {
        const auto [row, col, start] = runs[i];
        const auto end =
            i + 1 < runs.size()
                ? output.rfind('\x1b', std::get<2>(runs[i + 1]) - 1)
                : output.size();
        const auto text =
            std::regex_replace(output.substr(start, end - start), ansi, "");
        EXPECT_GE(row, 1);
        EXPECT_LE(row, rows);
        EXPECT_GE(col, 1);
        const int width = unicode ? text_utils::utf8DisplayWidth(text)
                                  : text_utils::displayWidth(text);
        EXPECT_LE(col + width - 1, cols) << text;
    }
}
} // namespace

TEST(RealModeTransitionsTest, SizePopupGeometryStaysFixedAcrossMemberSelection)
{
    Editor editor = Editor::createForTests();
    setupGeometryPopup(editor);
    editor.screenRows = 40;
    editor.screenCols = 120;
    const auto initial = layoutSymbolPopup(editor);
    for(int selection : {1, 3, 0, 2})
    {
        editor.browseSizePopup(selection - editor.sizePopupSelection);
        const auto layout = layoutSymbolPopup(editor);
        EXPECT_EQ(layout.width, initial.width);
        EXPECT_EQ(layout.height, initial.height);
        EXPECT_EQ(layout.top, initial.top);
        EXPECT_EQ(layout.left, initial.left);
        std::string output;
        editor.drawSymbolPopup(output);
        expectPopupOutputFits(output, editor.screenRows, editor.screenCols);
    }
}

TEST(RealModeTransitionsTest, SizePopupWrapsAndScrollsWithinResizedTerminal)
{
    Editor editor = Editor::createForTests();
    setupGeometryPopup(editor);
    editor.browseSizePopup(3);
    editor.screenRows = 9;
    editor.screenCols = 26;
    auto sm = makeMachine(editor, NormalMode{});
    const auto layout = layoutSymbolPopup(editor);
    EXPECT_LE(layout.top + layout.height - 1, editor.screenRows);
    EXPECT_LE(layout.left + layout.width - 1, editor.screenCols);
    for(const auto& row : layout.rows)
        EXPECT_LE(text_utils::utf8DisplayWidth(row.text), layout.innerWidth);
    for(size_t i = 0; i < layout.rows.size() + 5; ++i)
        sm.dispatch('j');
    EXPECT_EQ(editor.symbolPopupScroll, layout.maxScroll());
    std::string output;
    editor.drawSymbolPopup(output);
    expectPopupOutputFits(output, editor.screenRows, editor.screenCols);
    EXPECT_NE(layout.rows.back().text.find("debugger"), std::string::npos);
}

TEST(RealModeTransitionsTest, SizePopupNeverOverflowsTinyTerminals)
{
    Editor editor = Editor::createForTests();
    setupGeometryPopup(editor);
    editor.browseSizePopup(3);
    for(int cols : {1, 2, 4, 5, 6, 9, 17, 31})
        for(int rows : {1, 2, 3, 4, 7})
        {
            editor.screenCols = cols;
            editor.screenRows = rows;
            std::string output;
            editor.drawSymbolPopup(output);
            if(cols < 5 || rows < 3)
                EXPECT_TRUE(output.empty());
            else
                expectPopupOutputFits(output, rows, cols);
        }
}

TEST(RealModeTransitionsTest, InspectorTextWrapPreservesUnicodeAndTerminalCells)
{
    const std::string saved = std::setlocale(LC_CTYPE, nullptr);
    std::setlocale(LC_CTYPE, "");
    if(text_utils::utf8DisplayWidth("界") != 2)
        std::setlocale(LC_CTYPE, "en_US.UTF-8");
    if(text_utils::utf8DisplayWidth("界") != 2)
    {
        std::setlocale(LC_CTYPE, saved.c_str());
        GTEST_SKIP() << "UTF-8 locale unavailable";
    }
    const std::string text = "界面🙂é_member_最後_end";
    for(int width : {5, 8, 11, 17})
    {
        const auto wrapped = wrapSymbolPopupLines({text}, width);
        std::string reconstructed;
        for(size_t i = 0; i < wrapped.size(); ++i)
        {
            EXPECT_LE(text_utils::utf8DisplayWidth(wrapped[i]), width);
            reconstructed +=
                wrapped[i].substr(i == 0 ? 0 : std::min(2, width / 3));
        }
        EXPECT_EQ(reconstructed, text);
    }
    Editor editor = Editor::createForTests();
    setupGeometryPopup(editor);
    editor.sizePopupEntries[3].name = text + text + text;
    editor.screenCols = 24;
    editor.screenRows = 12;
    std::string output;
    editor.drawSymbolPopup(output);
    expectPopupOutputFits(output, editor.screenRows, editor.screenCols, true);
    std::setlocale(LC_CTYPE, saved.c_str());
}

TEST(RealModeTransitionsTest, DebuggerInspectorPopupUsesFixedWrappedViewport)
{
    const Theme theme = Theme::defaults();
    const std::vector<std::string> shortRows{"Storage: small, 4 bytes"};
    const auto longRows = wrapSymbolPopupLines(
        {"Storage: " + std::string(250, 'x'), "last row"},
        30);
    for(const auto* entries : {&shortRows, &longRows})
    {
        std::vector<int> filtered;
        for(size_t i = 0; i < entries->size(); ++i)
            filtered.push_back(static_cast<int>(i));
        std::string output;
        widgets::drawCommandPopup(
            output,
            {.frame = {.theme = theme, .screenRows = 12, .screenCols = 34},
             .entries = *entries,
             .filtered = filtered,
             .title = "Size and padding",
             .maxRows = 24,
             .fixedWidth = 100,
             .rawRows = true});
        expectPopupOutputFits(output, 12, 34);
        EXPECT_NE(output.find(Terminal::cursorPos(1, 1)), std::string::npos);
        EXPECT_NE(output.find(Terminal::cursorPos(12, 1)), std::string::npos);
    }
}

TEST(
    RealModeTransitionsTest,
    GsFunctionReturnResolvesOverloadsReferencesPointersAndAuto)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines =
        {"struct Packet { char tag; int value; char tail; };",
         "Packet make(int value) { return {1, value, 2}; }",
         "double make(double value) { return value; }",
         "Packet& reference() { static Packet p{}; return p; }",
         "Packet* pointer() { return &reference(); }",
         "auto deduced() { return make(3); }",
         "template<class T> T echo(T value) { return value; }",
         "auto global = make(5);",
         "int main() {",
         "    auto packet = make(3);",
         "    auto number = make(2.5);",
         "    auto& ref = reference();",
         "    auto ptr = pointer();",
         "    auto inferred = deduced();",
         "    auto scalar = echo<float>(1.0f);",
         "    auto constructed = Packet(1, 2, 3);",
         "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_function_return.cpp");

    struct Case
    {
        int row;
        const char* name;
        int bytes;
        bool members;
    };

    for(const auto& test : std::vector<Case>{
            {9, "make", 12, true},
            {10, "make", 8, false},
            {11, "reference", 12, true},
            {12, "pointer", static_cast<int>(sizeof(void*)), false},
            {13, "deduced", 12, true},
            {14, "echo", 4, false},
            {15, "Packet", 12, true},
            {7, "make", 12, true},
            {1, "make", 12, true}})
    {
        *editor.cursorY = test.row;
        *editor.cursorX = static_cast<int>(
            editor.currentBuffer->lines[test.row].find(test.name));
        editor.executeCommand("gs");
        ASSERT_TRUE(editor.symbolPopupActive)
            << test.name << ": " << editor.statusMessage;
        EXPECT_TRUE(editor.symbolPopupText.starts_with("Return value of "));
        EXPECT_NE(
            editor.symbolPopupText.find(
                ": " + std::to_string(test.bytes) + " bytes"),
            std::string::npos)
            << editor.symbolPopupText;
        if(test.members)
        {
            EXPECT_NE(
                editor.symbolPopupText.find("tag: 1 bytes"),
                std::string::npos);
            EXPECT_NE(
                editor.symbolPopupText.find("pad: 3 bytes"),
                std::string::npos);
        }
        else
            EXPECT_EQ(editor.sizePopupEntries.size(), 1u);
    }
}

TEST(
    RealModeTransitionsTest,
    GsFunctionReturnHandlesExternalFunctionsAndMultilineArgumentsWithoutExecution)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";
    const auto root = make_temp_dir("uvim_return_value_");
    const auto sentinel = root / "must-not-run";
    std::string path;
    json_utils::append_escaped(path, sentinel.string());
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines =
        {"#include <cstdio>",
         "struct Packet { char tag; int value; char tail; };",
         "extern Packet external(int);",
         "int nested(int a, int b) { return a + b; }",
         "Packet produce(int value, const char*) { auto f = std::fopen(" +
             path + ", \"w\"); if(f) std::fclose(f); return {1, value, 2}; }",
         "int main() {",
         "    auto result = produce(",
         "        nested(1, 2), // ignored )",
         R"src(        R"stop(text ) " ( ()stop")src",
         "    );",
         "    auto other = external(42);",
         "}"};
    set_buffer_filename(editor, (root / "program.cpp").string());
    const auto original = editor.currentBuffer->lines;
    for(const auto [row, name] : std::vector<std::pair<int, std::string>>{
            {6, "produce"},
            {10, "external"},
            {2, "external"}})
    {
        *editor.cursorY = row;
        *editor.cursorX = static_cast<int>(original[row].find(name));
        editor.executeCommand("gs");
        ASSERT_TRUE(editor.symbolPopupActive) << editor.statusMessage;
        EXPECT_NE(editor.symbolPopupText.find(": 12 bytes"), std::string::npos);
        EXPECT_NE(
            editor.symbolPopupText.find("value: 4 bytes"),
            std::string::npos);
        EXPECT_FALSE(std::filesystem::exists(sentinel));
        EXPECT_EQ(editor.currentBuffer->lines, original);
    }
    std::filesystem::remove_all(root);
}

TEST(
    RealModeTransitionsTest,
    GsFunctionReturnReportsVoidInsteadOfInventingObjectSize)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer
        ->lines = {"void nothing() {}", "int main() { nothing(); }"};
    set_buffer_filename(editor, "/tmp/uvim_size_void_return.cpp");
    *editor.cursorY = 1;
    *editor.cursorX = 13;
    editor.executeCommand("gs");
    EXPECT_FALSE(editor.symbolPopupActive);
    EXPECT_NE(editor.statusMessage.find("returns void"), std::string::npos);
}

TEST(RealModeTransitionsTest, GsCFunctionReturnShowsStructPadding)
{
    if(std::system("clang --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang is not available";
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines =
        {"struct Packet { char tag; int value; char tail; };",
         "struct Packet make(int value) { return (struct Packet){1, value, 2}; "
         "}",
         "int main(void) {",
         "    struct Packet p = make(42);",
         "    return p.value;",
         "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_c_function_return.c");
    *editor.cursorY = 3;
    *editor.cursorX = 22;
    editor.executeCommand("gs");
    ASSERT_TRUE(editor.symbolPopupActive) << editor.statusMessage;
    EXPECT_TRUE(editor.symbolPopupText.starts_with(
        "Return value of make(42): 12 bytes"));
    EXPECT_NE(editor.symbolPopupText.find("pad: 3 bytes"), std::string::npos);
}

TEST(
    RealModeTransitionsTest,
    GsMlangFunctionReturnsIncludeStructAndScalarLayouts)
{
    if(!std::getenv("UVIM_MLANG_SIZE_LIVE_TESTS"))
        GTEST_SKIP() << "Set UVIM_MLANG_SIZE_LIVE_TESTS and optionally "
                        "UVIM_MLANG_COMPILER";
    const auto root = make_temp_dir("uvim_mlang_returns_");
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines =
        {"struct Packet { var tag: u8; var value: i32; };",
         "fn make(value: i32) -> Packet { return Packet { tag: 1, value: value "
         "}; }",
         "fn make(value: f64) -> f64 { return value; }",
         "fn noop() -> void { return; }",
         "fn main() -> i32 {",
         "    var packet: Packet = make(3);",
         "    var number: f64 = make(2.0);",
         "    noop();",
         "    return packet.value;",
         "}"};
    set_buffer_filename(editor, (root / "program.mla").string());
    for(int row : {5, 6, 1})
    {
        *editor.cursorY = row;
        *editor.cursorX =
            static_cast<int>(editor.currentBuffer->lines[row].find("make"));
        editor.executeCommand("gs");
        ASSERT_TRUE(editor.symbolPopupActive) << editor.statusMessage;
        EXPECT_NE(editor.symbolPopupText.find(": 8 bytes"), std::string::npos);
        if(row != 6)
        {
            EXPECT_NE(
                editor.symbolPopupText.find("value: 4 bytes"),
                std::string::npos);
            EXPECT_NE(
                editor.symbolPopupText.find("pad: 3 bytes"),
                std::string::npos);
        }
        else
            EXPECT_EQ(editor.sizePopupEntries.size(), 1u);
    }
    *editor.cursorY = 7;
    *editor.cursorX = 5;
    editor.executeCommand("gs");
    EXPECT_FALSE(editor.symbolPopupActive);
    EXPECT_NE(editor.statusMessage.find("returns void"), std::string::npos);
    EXPECT_TRUE(std::filesystem::is_empty(root));
    std::filesystem::remove_all(root);
}

TEST(
    CursorCallTest,
    MatchesNestedParenthesesCommentsRawStringsAndDigitSeparators)
{
    const std::vector<std::string> lines =
        {"auto result = make(",
         "  nested(1'000, u8'a'), /* ) */",
         R"src(  R"tag(text ) " ( )tag")src",
         ");"};
    const auto call = cursorCallAt(lines, 0, 15, "make");
    ASSERT_TRUE(call);
    EXPECT_EQ(call->startColumn, 14);
    EXPECT_EQ(call->endLine, 3);
    EXPECT_EQ(call->endColumn, 1);
    EXPECT_NE(call->expression.find("nested(1'000"), std::string::npos);
    EXPECT_FALSE(cursorCallAt({"make(unfinished"}, 0, 1, "make"));
    EXPECT_FALSE(cursorCallAt({"int value;"}, 0, 5, "value"));
}

TEST(DebugReturnMetadataTest, PreservesReferenceObjectSizesPointerSizesAndVoid)
{
    const auto ir = mlangLayoutMetadata + R"(
!30 = !DISubroutineType(types: !31)
!31 = !{!6}
!32 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !30, size: 64)
!33 = !DILocalVariable(name: "probe", scope: !2, file: !1, line: 100, type: !32)
!34 = !DIDerivedType(tag: DW_TAG_reference_type, baseType: !6, size: 64)
!35 = !DISubroutineType(types: !36)
!36 = !{!34}
!37 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !35, size: 64)
!38 = !DILocalVariable(name: "referenceProbe", scope: !2, file: !1, line: 101, type: !37)
!39 = !DISubroutineType(types: !40)
!40 = !{null}
!41 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !39, size: 64)
!42 = !DILocalVariable(name: "voidProbe", scope: !2, file: !1, line: 102, type: !41)
!43 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !4, size: 64)
!44 = !DISubroutineType(types: !45)
!45 = !{!43}
!46 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !44, size: 64)
!47 = !DILocalVariable(name: "pointerProbe", scope: !2, file: !1, line: 103, type: !46)
!48 = !DIDerivedType(tag: DW_TAG_volatile_type, baseType: !6)
!49 = !DIDerivedType(tag: DW_TAG_reference_type, baseType: !48, size: 64)
!50 = !DISubroutineType(types: !51)
!51 = !{!49}
!52 = !DIDerivedType(tag: DW_TAG_ptr_to_member_type, baseType: !50, size: 128)
!53 = !DILocalVariable(name: "volatileProbe", scope: !2, file: !1, line: 104, type: !52)
)";
    const auto value = parseDebugFunctionPointerReturn(ir, "probe");
    ASSERT_TRUE(value);
    EXPECT_EQ(value->sizeBits, 96);
    EXPECT_EQ(value->members.size(), 3u);
    const auto reference =
        parseDebugFunctionPointerReturn(ir, "referenceProbe");
    ASSERT_TRUE(reference);
    EXPECT_EQ(reference->sizeBits, 96);
    EXPECT_EQ(reference->typeName, "Padded&");
    const auto pointer = parseDebugFunctionPointerReturn(ir, "pointerProbe");
    ASSERT_TRUE(pointer);
    EXPECT_EQ(pointer->sizeBits, 64);
    EXPECT_EQ(pointer->typeName, "i32*");
    EXPECT_TRUE(pointer->members.empty());
    const auto nothing = parseDebugFunctionPointerReturn(ir, "voidProbe");
    ASSERT_TRUE(nothing);
    EXPECT_EQ(nothing->typeName, "void");
    const auto qualified = parseDebugFunctionPointerReturn(ir, "volatileProbe");
    ASSERT_TRUE(qualified);
    EXPECT_EQ(qualified->sizeBits, 96);
    EXPECT_EQ(qualified->typeName, "Padded volatile&");
}

TEST(DebugReturnMetadataTest, UsesCallColumnsAndRejectsAmbiguousReturnTypes)
{
    const auto ir = mlangLayoutMetadata + R"(
  %first = call %Padded @make_int(i32 1), !dbg !40
  %second = call double @make_float(double 1.0), !dbg !41
!30 = distinct !DISubprogram(name: "make", linkageName: "make_int", file: !1, line: 40, type: !32)
!31 = distinct !DISubprogram(name: "make", linkageName: "make_float", file: !1, line: 41, type: !33)
!32 = !DISubroutineType(types: !34)
!33 = !DISubroutineType(types: !35)
!34 = !{!6, !4}
!35 = !{!5, !5}
!40 = !DILocation(line: 8, column: 10, scope: !2)
!41 = !DILocation(line: 8, column: 30, scope: !2)
)";
    auto first = parseDebugCallReturn(ir, "make", 8, 10);
    ASSERT_TRUE(first);
    EXPECT_EQ(first->sizeBits, 96);
    auto second = parseDebugCallReturn(ir, "make", 8, 30);
    ASSERT_TRUE(second);
    EXPECT_EQ(second->sizeBits, 64);
    EXPECT_FALSE(parseDebugCallReturn(ir, "make", 8));
    auto definition = parseDebugCallReturn(ir, "make", 40);
    ASSERT_TRUE(definition);
    EXPECT_EQ(definition->sizeBits, 96);
}

TEST(
    RealModeTransitionsTest,
    GsCallableMemberWithoutCallStillShowsPointerStorage)
{
    if(std::system("clang++ --version >/dev/null 2>&1") != 0)
        GTEST_SKIP() << "clang++ is not available";
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines =
        {"int target() { return 42; }",
         "struct Callbacks { int tag; int (*callback)(); };",
         "int main() {",
         "    Callbacks value{0, target};",
         "    auto copy = value.callback;",
         "}"};
    set_buffer_filename(editor, "/tmp/uvim_size_callback_pointer.cpp");
    *editor.cursorY = 4;
    *editor.cursorX = 23;
    editor.executeCommand("gs");
    ASSERT_TRUE(editor.symbolPopupActive) << editor.statusMessage;
    EXPECT_EQ(
        editor.symbolPopupText,
        "sizeof(value.callback) = " + std::to_string(sizeof(int (*)())) +
            " bytes");
}
