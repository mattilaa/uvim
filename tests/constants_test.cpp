#include "constants.h"
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
