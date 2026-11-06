#include "real_mode_test_utils.h"

TEST(EmbeddedUiTest, UsesOneColumnGutterWithoutLineNumbers)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"one", "two"};
    EXPECT_EQ(editor.lineNumberWidth(), 0);
    EXPECT_EQ(editor.gutterWidth(), 1);
}

TEST(EmbeddedUiTest, BarColorsDoNotEnableSyntaxColors)
{
    Editor editor = Editor::createForTests();
#ifndef UVIM_ENABLE_TERMINAL_COLORS
    EXPECT_TRUE(editor.theme.syntax(TOKEN_KEYWORD).empty());
#endif
#ifdef UVIM_EMBEDDED_COLORS
    EXPECT_EQ(editor.theme.statusBar(), "\x1b[0;38;2;255;255;255;48;2;0;0;64m");
    EXPECT_EQ(editor.theme.tabBar(), "\x1b[0;38;2;255;255;255;48;2;0;0;64m");
    EXPECT_EQ(editor.theme.reset(), "\x1b[0m");
#else
    EXPECT_TRUE(editor.theme.statusBar().empty());
    EXPECT_TRUE(editor.theme.tabBar().empty());
#endif
}
