// Starts a real clangd through LspClient: process launch, the stdio pipes,
// file URIs and shutdown. Skipped when clangd is not installed.

#include "lsp_client.h"
#include "real_mode_test_utils.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace
{
std::string findClangd()
{
    if(const char* configured = std::getenv("UVIM_TEST_CLANGD");
       configured && *configured)
        return configured;
#ifdef _WIN32
    const char separator = ';';
    const char* names[] = {"clangd.exe"};
#else
    const char separator = ':';
    const char* names[] = {"clangd"};
#endif
    std::error_code ec;
    const char* pathEnv = std::getenv("PATH");
    std::string path = pathEnv ? pathEnv : "";
    size_t start = 0;
    while(start <= path.size())
    {
        size_t end = path.find(separator, start);
        if(end == std::string::npos)
            end = path.size();
        const fs::path dir = path.substr(start, end - start);
        for(const char* name : names)
        {
            if(!dir.empty() && fs::is_regular_file(dir / name, ec))
                return (dir / name).string();
        }
        start = end + 1;
    }
#ifdef _WIN32
    const char* programFiles = std::getenv("ProgramFiles");
    const fs::path llvm = fs::path(programFiles && *programFiles
                                       ? programFiles
                                       : "C:\\Program Files") /
                          "LLVM" / "bin" / "clangd.exe";
    if(fs::is_regular_file(llvm, ec))
        return llvm.string();
#endif
    return {};
}
} // namespace

TEST(ClangdIntegrationTest, DiagnosticsDefinitionAndShutdown)
{
    const std::string clangd = findClangd();
    if(clangd.empty())
        GTEST_SKIP() << "clangd not installed";

    auto root = uvim_test::make_temp_dir("uvim_clangd_it_");
    const fs::path source = root / "main.cpp";
    // The error sits in its own function: clangd drops calls inside an
    // expression that fails to compile.
    const std::string text = "int add(int a, int b) { return a + b; }\n"
                             "int main() { return add(1, 2); }\n"
                             "int broken() { return missing; }\n";
    uvim_test::write_file(source, text);

    LspClient client;
    ASSERT_TRUE(client.start(clangd, root.string())) << client.lastError();
    EXPECT_TRUE(client.running());
    client.didOpen(source.string(), "cpp", text);

    // clangd publishes diagnostics asynchronously after parsing.
    std::vector<LspClient::Diagnostic> diagnostics;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while(diagnostics.empty() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        diagnostics = client.diagnostics(source.string());
    }
    ASSERT_FALSE(diagnostics.empty()) << "no diagnostics from " << clangd;
    EXPECT_EQ(diagnostics.front().line, 2);
    EXPECT_NE(diagnostics.front().message.find("missing"), std::string::npos)
        << diagnostics.front().message;

    // Go to definition of `add` in main(): back to line 0 of the same file.
    const std::string line1 = "int main() { return add(1, 2); }";
    const int column = static_cast<int>(line1.find("add(1"));
    auto location = client.definition(source.string(), 1, column, line1);
    ASSERT_TRUE(location.has_value());
    std::error_code ec;
    EXPECT_TRUE(fs::equivalent(location->path, source, ec))
        << location->path << " vs " << source.string();
    EXPECT_EQ(location->line, 0);

    client.stop();
    EXPECT_FALSE(client.running());
}
