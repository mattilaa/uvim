#include "editor.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <chrono>

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

TEST(JsTsResolutionTest, InfersTypeFromArrayMethodParam)
{
    std::vector<std::string> lines = {
        "type User = {",
        "  id: number;",
        "  title: string;",
        "};",
        "const users: User[] = [];",
        "return users.find((user) => user.id === id);",
    };

    std::string typeName = Editor::testInferTsTypeFromArrayMethodLine(
        lines[5], "user", lines, 5);
    EXPECT_EQ(typeName, "User");

    int typeY = -1;
    int typeX = 0;
    ASSERT_TRUE(Editor::testFindTsTypeDefinition(lines, "User", typeY, typeX));

    int memberY = -1;
    int memberX = 0;
    EXPECT_TRUE(Editor::testFindTsMemberInType(lines, typeY, "id", memberY,
                                               memberX));
    EXPECT_TRUE(Editor::testFindTsMemberInType(lines, typeY, "title", memberY,
                                               memberX));
}

TEST(JsTsResolutionTest, ResolvesTsconfigPaths)
{
    auto root = make_temp_dir("uvim_tsconfig_");
    write_file(root / "tsconfig.json",
               R"json({
  "compilerOptions": {
    "baseUrl": "src",
    "paths": {
      "@models/*": ["models/*"]
    }
  }
})json");

    write_file(root / "src/models/user.ts", "export type User = { id: number };\n");
    write_file(root / "app.ts", "import { User } from \"@models/user\";\n");

    std::string resolved = Editor::testResolveJsTsModule(
        (root / "app.ts").string(), "@models/user");
    EXPECT_EQ(std::filesystem::path(resolved).lexically_normal().string(),
              (root / "src/models/user.ts").lexically_normal().string());
}

TEST(JsTsResolutionTest, ResolvesNodeModule)
{
    auto root = make_temp_dir("uvim_node_modules_");
    write_file(root / "app.ts", "import { x } from \"pkg\";\n");
    write_file(root / "node_modules/pkg/package.json",
               R"json({ "types": "index.d.ts" })json");
    write_file(root / "node_modules/pkg/index.d.ts", "export const x: number;\n");

    std::string resolved = Editor::testResolveJsTsModule(
        (root / "app.ts").string(), "pkg");
    EXPECT_EQ(std::filesystem::path(resolved).lexically_normal().string(),
              (root / "node_modules/pkg/index.d.ts")
                  .lexically_normal()
                  .string());

    write_file(root / "node_modules/pkg/sub.ts", "export const y: number;\n");
    std::string subResolved = Editor::testResolveJsTsModule(
        (root / "app.ts").string(), "pkg/sub");
    EXPECT_EQ(std::filesystem::path(subResolved).lexically_normal().string(),
              (root / "node_modules/pkg/sub.ts").lexically_normal().string());
}
