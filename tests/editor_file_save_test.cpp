#include "real_mode_test_utils.h"

namespace
{
class FileSaveTest : public testing::Test
{
protected:
    std::filesystem::path directory = uvim_test::make_temp_dir("uvim_save_");
    std::filesystem::path path = directory / "notes.txt";

    ~FileSaveTest() override
    {
        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
    }

    std::string readSaved() const
    {
        std::ifstream file(path);
        return {std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>()};
    }
};
} // namespace

TEST_F(FileSaveTest, PreservesCleanLinesAndCursor)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    uvim_test::set_buffer_filename(editor, path.string());
    editor.formatOnSave = false;
    const std::vector<std::string> content = {
        "", "  leading and  interior spaces", "caf\xc3\xa9", std::string(8192, 'x')};
    editor.currentBuffer->lines = content;
    *editor.cursorY = 1;
    *editor.cursorX = 5;
    *editor.dirty = true;

    editor.saveFile();

    EXPECT_EQ(editor.currentBuffer->lines, content);
    EXPECT_EQ(readSaved(), "\n  leading and  interior spaces\ncaf\xc3\xa9\n" +
                               std::string(8192, 'x') + "\n");
    EXPECT_EQ(*editor.cursorX, 5);
    EXPECT_FALSE(*editor.dirty);
    EXPECT_TRUE(editor.currentBuffer->matchesSavedContent());
    EXPECT_EQ(editor.statusMessage.find("cleaned"), std::string::npos);
}

TEST_F(FileSaveTest, StillExpandsTabsAndTrimsTrailingWhitespace)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    uvim_test::set_buffer_filename(editor, path.string());
    editor.formatOnSave = false;
    editor.currentBuffer->lines = {
        "kept  inside", "alpha   ", "\tbeta", "x\ty", "  \t  ", ""};
    *editor.cursorY = 1;
    *editor.cursorX = 8;

    editor.saveFile();

    EXPECT_EQ(editor.currentBuffer->lines,
              (std::vector<std::string>{"kept  inside", "alpha", "    beta",
                                        "x   y", "", ""}));
    EXPECT_EQ(readSaved(), "kept  inside\nalpha\n    beta\nx   y\n\n\n");
    EXPECT_EQ(*editor.cursorX, 4);
    EXPECT_NE(editor.statusMessage.find("4 lines cleaned"), std::string::npos);
    EXPECT_TRUE(editor.currentBuffer->matchesSavedContent());
}
