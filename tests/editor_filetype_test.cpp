#include "editor.h"
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
}

TEST(EditorFileTypeTest, DispatchMatchesSpecificHelpers)
{
    Editor editor = Editor::createForTests();
    setupEditorBuffer(editor);

    *editor.filename = "/tmp/example.cpp";
    EXPECT_TRUE(editor.isFileType<FileType::Cpp>());
    EXPECT_FALSE(editor.isFileType<FileType::Html>());

    *editor.filename = "/tmp/index.html";
    EXPECT_TRUE(editor.isFileType<FileType::Html>());
    EXPECT_FALSE(editor.isFileType<FileType::Xml>());

    *editor.filename = "/tmp/layout.xml";
    EXPECT_TRUE(editor.isFileType<FileType::Xml>());
    EXPECT_FALSE(editor.isFileType<FileType::Cpp>());

    *editor.filename = "/tmp/.clang-format";
    EXPECT_TRUE(editor.isFileType<FileType::FormatterConfig>());
    EXPECT_FALSE(editor.isFileType<FileType::Yaml>());

    *editor.filename = "/tmp/.mlang-format";
    EXPECT_TRUE(editor.isFileType<FileType::FormatterConfig>());
    EXPECT_FALSE(editor.isFileType<FileType::Toml>());

    *editor.filename = "/tmp/scanner.l";
    EXPECT_TRUE(editor.isFileType<FileType::Flex>());
    EXPECT_FALSE(editor.isFileType<FileType::Bison>());

    *editor.filename = "/tmp/parser.y";
    EXPECT_TRUE(editor.isFileType<FileType::Bison>());
    EXPECT_FALSE(editor.isFileType<FileType::Flex>());
}
