// Starts a real C# language server (csharp-ls or OmniSharp) through
// LspClient on a small console project. Skipped when no server or no .NET
// SDK is installed.

#include "csharp_lsp.h"
#include "lsp_client.h"
#include "process_pipe.h"
#include "real_mode_test_utils.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace
{
// "net8.0" for SDK 8.x etc.: the project must target an installed SDK.
std::string targetFramework()
{
    ProcessPipe pipe({"dotnet", "--version"});
    if(!pipe)
        return {};
    const std::string version = pipe.readLine();
    if(pipe.close() != 0 || version.empty())
        return {};
    return "net" + version.substr(0, version.find('.')) + ".0";
}
} // namespace

TEST(CSharpLspIntegrationTest, DiagnosticsDefinitionAndShutdown)
{
    const std::string server = csharp_lsp::resolveServer("csharp-ls");
    if(server.empty())
        GTEST_SKIP() << "no C# language server installed";
    const std::string framework = targetFramework();
    if(framework.empty())
        GTEST_SKIP() << "no .NET SDK installed";

    auto root = uvim_test::make_temp_dir("uvim_csharp_it_");
    uvim_test::write_file(
        root / "App.csproj",
        "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
        "  <PropertyGroup>\n"
        "    <OutputType>Exe</OutputType>\n"
        "    <TargetFramework>" + framework + "</TargetFramework>\n"
        "  </PropertyGroup>\n"
        "</Project>\n");
    const fs::path source = root / "Program.cs";
    const std::string text = "class Program\n"
                             "{\n"
                             "    static int Add(int a, int b) => a + b;\n"
                             "    static int Main() => Add(1, 2);\n"
                             "    static int Broken() => missing;\n"
                             "}\n";
    uvim_test::write_file(source, text);
    EXPECT_EQ(csharp_lsp::projectRoot(source.string()),
              fs::absolute(root).string());

    LspClient client;
    ASSERT_TRUE(client.startServer(server, root.string(),
                                   csharp_lsp::serverArgs(server, {})))
        << client.lastError();
    client.didOpen(source.string(), "csharp", text);

    // Loading the project (MSBuild) can take a while on the first run.
    std::vector<LspClient::Diagnostic> diagnostics;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(180);
    while(std::chrono::steady_clock::now() < deadline)
    {
        diagnostics = client.diagnostics(source.string());
        bool found = false;
        for(const auto& diag : diagnostics)
            found = found || diag.message.find("missing") != std::string::npos;
        if(found)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        if(!client.running())
            break;
    }
    bool missingReported = false;
    for(const auto& diag : diagnostics)
    {
        if(diag.message.find("missing") != std::string::npos)
        {
            missingReported = true;
            EXPECT_EQ(diag.line, 4);
        }
    }
    ASSERT_TRUE(missingReported) << "no diagnostic for `missing` from "
                                 << server;

    const std::string line3 = "    static int Main() => Add(1, 2);";
    auto location = client.definition(source.string(), 3,
                                      (int)line3.find("Add("), line3);
    ASSERT_TRUE(location.has_value());
    std::error_code ec;
    EXPECT_TRUE(fs::equivalent(location->path, source, ec)) << location->path;
    EXPECT_EQ(location->line, 2);

    client.stop();
    EXPECT_FALSE(client.running());
}
