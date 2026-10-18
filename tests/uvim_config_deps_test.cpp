#include "uvim_config_deps.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>

using namespace uvim_config_deps;

namespace
{
Finder fakeFinder(std::set<std::string> present)
{
    return [present](const std::string& program)
    { return present.count(program) > 0; };
}

const Dependency* byName(const std::vector<Dependency>& deps,
                         const std::string& name)
{
    auto it = std::find_if(deps.begin(), deps.end(),
                           [&](const Dependency& d) { return d.name == name; });
    return it == deps.end() ? nullptr : &*it;
}

int indexOf(const std::vector<Dependency>& deps, const std::string& name)
{
    for(size_t i = 0; i < deps.size(); ++i)
        if(deps[i].name == name)
            return static_cast<int>(i);
    return -1;
}
} // namespace

TEST(UvimConfigDepsTest, OnlyEnabledFeaturesNeedTools)
{
    Features none;
    EXPECT_TRUE(required(none, Platform::Linux, PackageManager::Apt,
                         fakeFinder({}))
                    .empty());

    Features f;
    f.csharpLsp = true;
    f.gitTools = true;
    const auto deps =
        required(f, Platform::Linux, PackageManager::Apt, fakeFinder({}));
    ASSERT_NE(byName(deps, "csharp-ls"), nullptr);
    ASSERT_NE(byName(deps, ".NET SDK (dotnet)"), nullptr);
    ASSERT_NE(byName(deps, "git"), nullptr);
    EXPECT_EQ(byName(deps, "clangd"), nullptr);
    EXPECT_EQ(byName(deps, "Node.js (npm)"), nullptr);
    // The prerequisite comes before what it installs.
    EXPECT_LT(indexOf(deps, ".NET SDK (dotnet)"), indexOf(deps, "csharp-ls"));
    EXPECT_EQ(byName(deps, "csharp-ls")->install,
              "dotnet tool install --global csharp-ls");
}

TEST(UvimConfigDepsTest, InstallCommandsFollowThePackageManager)
{
    Features f;
    f.clangdLsp = true;
    f.tsLsp = true;
    f.searchTools = true;
    auto install = [&](PackageManager pm, const std::string& name)
    {
        return byName(required(f, Platform::Linux, pm, fakeFinder({})), name)
            ->install;
    };
    EXPECT_EQ(install(PackageManager::Brew, "clangd"), "brew install llvm");
    EXPECT_NE(install(PackageManager::Apt, "clangd").find("apt-get install -y clangd"),
              std::string::npos);
    EXPECT_NE(install(PackageManager::Dnf, "clangd").find("clang-tools-extra"),
              std::string::npos);
    EXPECT_EQ(install(PackageManager::Winget, "ripgrep"),
              "winget install --id BurntSushi.ripgrep.MSVC -e");
    EXPECT_EQ(install(PackageManager::None, "ripgrep"), "");
    EXPECT_EQ(install(PackageManager::Apt, "typescript-language-server"),
              "npm install -g typescript-language-server typescript");
    EXPECT_LT(indexOf(required(f, Platform::Linux, PackageManager::Apt,
                               fakeFinder({})),
                      "Node.js (npm)"),
              indexOf(required(f, Platform::Linux, PackageManager::Apt,
                               fakeFinder({})),
                      "typescript-language-server"));
}

TEST(UvimConfigDepsTest, MissingAndPythonInstaller)
{
    Features f;
    f.pythonLsp = true;
    f.robotLsp = true;
    f.formatters = true;
    // pylsp satisfies the Python LSP as well as pyright.
    const auto finder = fakeFinder({"npm", "pylsp", "pipx", "clang-format"});
    const auto deps =
        required(f, Platform::Linux, PackageManager::Apt, finder);
    const auto absent = missing(deps, finder);
    EXPECT_EQ(byName(absent, "pyright"), nullptr);
    EXPECT_EQ(byName(absent, "clang-format"), nullptr);
    ASSERT_NE(byName(absent, "robotframework-lsp"), nullptr);
    EXPECT_EQ(byName(absent, "robotframework-lsp")->install,
              "pipx install robotframework-lsp");
    ASSERT_NE(byName(absent, "ruff"), nullptr);
    EXPECT_TRUE(byName(absent, "ruff")->optional);
}

TEST(UvimConfigDepsTest, PackageManagerDetection)
{
    EXPECT_EQ(detectPackageManager(Platform::MacOS, fakeFinder({"brew"})),
              PackageManager::Brew);
    EXPECT_EQ(detectPackageManager(Platform::Linux, fakeFinder({"dnf"})),
              PackageManager::Dnf);
    EXPECT_EQ(detectPackageManager(Platform::Windows, fakeFinder({})),
              PackageManager::None);
}
