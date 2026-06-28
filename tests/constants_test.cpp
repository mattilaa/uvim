#include "constants.h"
#include "config_parser.h"
#include "json_utils.h"
#include <gtest/gtest.h>

TEST(ConstantsTest, MatchesFilePatternsSuffixCaseInsensitive)
{
    EXPECT_TRUE(constants::matches_file_patterns("main.CPP",
                                                 constants::cpp_suffixes));
    EXPECT_TRUE(constants::matches_file_patterns("note.MarkDown",
                                                 constants::markdown_suffixes));
    EXPECT_FALSE(constants::matches_file_patterns("note.txt",
                                                  constants::markdown_suffixes));
}

TEST(ConstantsTest, MatchesFilePatternsPrefixAndSuffix)
{
    EXPECT_TRUE(constants::matches_file_patterns("CMakeLists.txt",
                                                 constants::cmake_prefixes,
                                                 constants::cmake_suffixes));
    EXPECT_TRUE(constants::matches_file_patterns("CMakeCache",
                                                 constants::cmake_prefixes,
                                                 constants::cmake_suffixes));
    EXPECT_TRUE(constants::matches_file_patterns("foo.cmake",
                                                 constants::cmake_prefixes,
                                                 constants::cmake_suffixes));
    EXPECT_FALSE(constants::matches_file_patterns("Makefile",
                                                  constants::cmake_prefixes,
                                                  constants::cmake_suffixes));
}

TEST(ConstantsTest, IsCmakeFileUsesPrefixOrSuffix)
{
    auto is_cmake = [](std::string_view path) {
        return constants::is_filetype<constants::cmake_prefixes,
                                       constants::cmake_suffixes>(path);
    };
    EXPECT_TRUE(is_cmake("CMakeLists.txt"));
    EXPECT_TRUE(is_cmake("/tmp/CMakeCache.txt"));
    EXPECT_TRUE(is_cmake("module.CMAKE"));
    EXPECT_FALSE(is_cmake("Makefile"));
}

TEST(ConstantsTest, IsCppFileMatchesStdlibPatterns)
{
    auto is_cpp = [](std::string_view path) {
        return constants::is_filetype<constants::no_pattern,
                                       constants::cpp_suffixes,
                                       constants::cpp_stdlib_patterns>(path);
    };
    EXPECT_TRUE(is_cpp("/usr/include/c++/v1/vector"));
    EXPECT_TRUE(is_cpp("/usr/include/bits/std_abs.h"));
    EXPECT_FALSE(is_cpp("/usr/include/python3.12/Python.pyi"));
}

TEST(JsonUtilsTest, ParsedPositiveIntegersKeepSignedCompatibility)
{
    json_utils::Document doc;
    ASSERT_TRUE(json_utils::parse(doc, R"({"id":1})"));

    const json_utils::Value* id = json_utils::find(doc, "id");
    ASSERT_NE(id, nullptr);
    EXPECT_TRUE(id->IsInt());
    EXPECT_TRUE(id->IsUint());
    EXPECT_TRUE(id->IsInt64());
    EXPECT_TRUE(id->IsUint64());
    EXPECT_EQ(id->GetInt(), 1);
}

TEST(ConfigParserTest, ParsesTomlSectionsAndValues)
{
    const auto values = editor::config::parseTomlMap(
        "[editor]\n"
        "tabspaces = 2\n"
        "autobraces = true # keep this on\n"
        "[editor.syntax.cpp]\n"
        "locals_color = \"normal\"\n"
        "[theme.base]\n"
        "fg = \"#e0def4\"\n");

    EXPECT_EQ(values.at("editor.tabspaces"), "2");
    EXPECT_EQ(values.at("editor.autobraces"), "true");
    EXPECT_EQ(values.at("editor.syntax.cpp.locals_color"), "normal");
    EXPECT_EQ(values.at("theme.base.fg"), "#e0def4");
}
