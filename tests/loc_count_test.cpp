#include "editor.h"
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace
{
std::filesystem::path make_temp_dir(const std::string& prefix)
{
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path base =
        std::filesystem::temp_directory_path() /
        (prefix + std::to_string(now));
    std::filesystem::create_directories(base);
    return base;
}

void write_file(const std::filesystem::path& path, std::string_view content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}
} // namespace

TEST(LocCountTest, CountsCppLoc)
{
    auto root = make_temp_dir("uvim_loc_cpp_");
    auto path = root / "main.cpp";
    write_file(path,
               "// header comment\n"
               "\n"
               "int main() {\n"
               "  /* block\n"
               "     comment */\n"
               "  return 0; // inline comment\n"
               "}\n");
    EXPECT_EQ(Editor::testCountLocForFile(path.string()), 3);
}

TEST(LocCountTest, CountsPythonLoc)
{
    auto root = make_temp_dir("uvim_loc_py_");
    auto path = root / "app.py";
    write_file(path,
               "# comment\n"
               "\n"
               "def foo():\n"
               "    x = 1  # inline\n"
               "    # comment\n"
               "    return x\n");
    EXPECT_EQ(Editor::testCountLocForFile(path.string()), 3);
}

TEST(LocCountTest, CountsHtmlLoc)
{
    auto root = make_temp_dir("uvim_loc_html_");
    auto path = root / "index.html";
    write_file(path,
               "<!-- header -->\n"
               "<div>\n"
               "  <!-- block\n"
               "       comment -->\n"
               "  <span>Hi</span>\n"
               "</div>\n");
    EXPECT_EQ(Editor::testCountLocForFile(path.string()), 3);
}

TEST(LocCountTest, CountsYamlLoc)
{
    auto root = make_temp_dir("uvim_loc_yaml_");
    auto path = root / "config.yaml";
    write_file(path,
               "# comment\n"
               "name: test\n"
               "items:\n"
               "  - a\n"
               "  # comment\n"
               "\n");
    EXPECT_EQ(Editor::testCountLocForFile(path.string()), 3);
}

TEST(LocCountTest, CountsCssLoc)
{
    auto root = make_temp_dir("uvim_loc_css_");
    auto path = root / "styles.css";
    write_file(path,
               "/* comment\n"
               "   comment */\n"
               ".class {\n"
               "  color: red; /* inline comment */\n"
               "}\n");
    EXPECT_EQ(Editor::testCountLocForFile(path.string()), 3);
}

TEST(LocCountTest, CountsTomlLoc)
{
    auto root = make_temp_dir("uvim_loc_toml_");
    auto path = root / "config.toml";
    write_file(path,
               "# comment\n"
               "name = \"uvim\"\n"
               "\n"
               "[section]\n"
               "value = 1 # inline\n");
    EXPECT_EQ(Editor::testCountLocForFile(path.string()), 3);
}

TEST(LocCountTest, CountsShellLoc)
{
    auto root = make_temp_dir("uvim_loc_sh_");
    auto path = root / "script.sh";
    write_file(path,
               "#!/bin/sh\n"
               "# comment\n"
               "\n"
               "echo \"hi\" # inline\n"
               "exit 0\n");
    EXPECT_EQ(Editor::testCountLocForFile(path.string()), 2);
}

TEST(LocCountTest, CountsJsLoc)
{
    auto root = make_temp_dir("uvim_loc_js_");
    auto path = root / "app.js";
    write_file(path,
               "// comment\n"
               "const x = 1;\n"
               "/* block */\n"
               "function f() { return x; }\n");
    EXPECT_EQ(Editor::testCountLocForFile(path.string()), 2);
}

TEST(LocCountTest, CountsRustLoc)
{
    auto root = make_temp_dir("uvim_loc_rs_");
    auto path = root / "lib.rs";
    write_file(path,
               "// comment\n"
               "pub fn foo() -> i32 {\n"
               "    /* block */\n"
               "    1\n"
               "}\n");
    EXPECT_EQ(Editor::testCountLocForFile(path.string()), 3);
}

TEST(LocCountTest, CountsXmlLoc)
{
    auto root = make_temp_dir("uvim_loc_xml_");
    auto path = root / "doc.xml";
    write_file(path,
               "<!-- comment -->\n"
               "<root>\n"
               "  <child>1</child>\n"
               "</root>\n");
    EXPECT_EQ(Editor::testCountLocForFile(path.string()), 3);
}

TEST(LocCountTest, CountsTsLoc)
{
    auto root = make_temp_dir("uvim_loc_ts_");
    auto path = root / "app.ts";
    write_file(path,
               "// comment\n"
               "type User = { id: number };\n"
               "/* block */\n"
               "const users: User[] = [];\n");
    EXPECT_EQ(Editor::testCountLocForFile(path.string()), 2);
}

TEST(LocCountTest, CountsGoLoc)
{
    auto root = make_temp_dir("uvim_loc_go_");
    auto path = root / "main.go";
    write_file(path,
               "package main\n"
               "// comment\n"
               "func main() {\n"
               "  /* block */\n"
               "  println(\"hi\")\n"
               "}\n");
    EXPECT_EQ(Editor::testCountLocForFile(path.string()), 4);
}

TEST(LocCountTest, CountsCmakeLoc)
{
    auto root = make_temp_dir("uvim_loc_cmake_");
    auto path = root / "CMakeLists.txt";
    write_file(path,
               "# comment\n"
               "cmake_minimum_required(VERSION 3.20)\n"
               "\n"
               "project(uvim)\n"
               "# another\n");
    EXPECT_EQ(Editor::testCountLocForFile(path.string()), 2);
}

TEST(LocCountTest, CountsRobotLoc)
{
    auto root = make_temp_dir("uvim_loc_robot_");
    auto path = root / "suite.robot";
    write_file(path,
               "*** Test Cases ***\n"
               "# comment\n"
               "Example\n"
               "    Log    hello\n");
    EXPECT_EQ(Editor::testCountLocForFile(path.string()), 3);
}

TEST(LocCountTest, CountsIniLoc)
{
    auto root = make_temp_dir("uvim_loc_ini_");
    auto path = root / "app.ini";
    write_file(path,
               "# comment\n"
               "[section]\n"
               "key=value\n"
               "\n");
    EXPECT_EQ(Editor::testCountLocForFile(path.string()), 2);
}

TEST(LocCountTest, CountsMarkdownLoc)
{
    auto root = make_temp_dir("uvim_loc_md_");
    auto path = root / "README.md";
    write_file(path,
               "# Title\n"
               "\n"
               "Some text.\n");
    EXPECT_EQ(Editor::testCountLocForFile(path.string()), 2);
}
