#include "real_mode_test_utils.h"

using namespace uvim_test;

TEST(EditorFileControllerTest, FindsSameDirectoryHeaderForSource)
{
    const auto root = make_temp_dir("uvim_alt_same_dir_");
    write_file(root / "CMakeLists.txt",
               "cmake_minimum_required(VERSION 3.20)\n");
    write_file(root / "src" / "cpp_constants.cpp",
               "#include \"cpp_constants.h\"\n");
    write_file(root / "src" / "cpp_constants.h", "#pragma once\n");

    Editor editor = Editor::createForTests();

    EXPECT_EQ(
        editor.findAlternateFile((root / "src" / "cpp_constants.cpp").string()),
        (root / "src" / "cpp_constants.h").string());
}

TEST(EditorFileControllerTest, FindsIncludedHeaderWhenSourceNameDiffers)
{
    const auto root = make_temp_dir("uvim_alt_include_");
    write_file(root / "CMakeLists.txt",
               "cmake_minimum_required(VERSION 3.20)\n");
    write_file(root / "src" / "syntax_tables.cpp",
               "#include \"cpp_constants.h\"\n");
    write_file(root / "src" / "cpp_constants.h", "#pragma once\n");

    Editor editor = Editor::createForTests();

    EXPECT_EQ(
        editor.findAlternateFile((root / "src" / "syntax_tables.cpp").string()),
        (root / "src" / "cpp_constants.h").string());
}

TEST(EditorFileControllerTest, FindsSourceIncludingHeaderWhenNameDiffers)
{
    const auto root = make_temp_dir("uvim_alt_reverse_include_");
    write_file(root / "CMakeLists.txt",
               "cmake_minimum_required(VERSION 3.20)\n");
    write_file(root / "src" / "syntax_tables.cpp",
               "#include \"cpp_constants.h\"\n");
    write_file(root / "src" / "cpp_constants.h", "#pragma once\n");

    Editor editor = Editor::createForTests();

    EXPECT_EQ(
        editor.findAlternateFile((root / "src" / "cpp_constants.h").string()),
        (root / "src" / "syntax_tables.cpp").string());
}
