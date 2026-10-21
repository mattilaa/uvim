#include "editor_lsp_controller.h"
#include "lsp_client.h"
#include "process_pipe.h"
#include "real_mode_test_utils.h"
#include "syntax_highlighter.h"

#include <algorithm>
#include <future>
#include <thread>

namespace
{
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

bool until(const std::function<bool()>& ready)
{
    const auto deadline = Clock::now() + 5s;
    while(!ready())
    {
        if(Clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(5ms);
    }
    return true;
}
} // namespace

TEST(LspAsyncTest, EditorCanEditAndOpenFilesWhileServerInitializes)
{
    const auto root = uvim_test::make_temp_dir("uvim_async_lsp_");
    uvim_test::write_file(root / "hold-initialize", "");
    uvim_test::write_file(root / "first.cpp", "int first;\n");
    uvim_test::write_file(root / "second.cpp", "int second;\n");
    Editor editor = Editor::createForTests();
    editor.setProjectRoot(root.string());
    editor.openFile((root / "first.cpp").string());
    const auto started = Clock::now();
    editor.enableClangdLsp(true, root.string(), UVIM_TEST_LSP_SERVER);
    EXPECT_LT(Clock::now() - started, 500ms);
    EXPECT_FALSE(editor.isClangdLspEnabled());
    EXPECT_FALSE(editor.lspClient);
    ASSERT_TRUE(until(
        [&] { return std::filesystem::exists(root / "initialize.received"); }));

    editor.showLspInfo();
    EXPECT_NE(std::find(editor.lspInfoLines.begin(), editor.lspInfoLines.end(),
                        "clangd: STARTING"),
              editor.lspInfoLines.end());

    auto sm = uvim_test::makeMachine(editor, NormalMode{});
    sm.dispatch('i');
    sm.dispatch('X');
    sm.dispatch(keyCode(control::ControlKey::ESC));
    EXPECT_EQ(editor.lines->front(), "Xint first;");
    editor.openFile((root / "second.cpp").string());
    EXPECT_EQ(editor.lines->front(), "int second;");
    EXPECT_FALSE(editor.isClangdLspEnabled());

    uvim_test::write_file(root / "initialize.release", "");
    ASSERT_TRUE(until(
        [&]
        {
            editor.serviceTasks();
            return editor.tasks.idle();
        }));
    EXPECT_TRUE(editor.isClangdLspEnabled());
    EXPECT_EQ(editor.statusMessage, "clangd LSP: ready");
    EXPECT_TRUE(editor.currentBuffer->lspSyncNeeded);
    std::filesystem::remove_all(root);
}

TEST(LspAsyncTest, PipeBackpressureDoesNotBlockDocumentUpdatesAndKeepsOrder)
{
    const auto root = uvim_test::make_temp_dir("uvim_lsp_pipe_");
    uvim_test::write_file(root / "hold-input", "");
    LspClient client;
    ASSERT_TRUE(client.startServer(UVIM_TEST_LSP_SERVER, root.string()));
    ASSERT_TRUE(until(
        [&]
        { return std::filesystem::exists(root / "initialized.received"); }));
    const std::string path = (root / "main.cpp").string();
    const std::string newestText = "newest text\nnext line\n";
    const auto started = Clock::now();
    client.didOpen(path, "cpp", std::string(2 * 1024 * 1024, 'x'));
    client.didChange(path, newestText, "cpp");
    EXPECT_LT(Clock::now() - started, 500ms);

    uvim_test::write_file(root / "input.release", "");
    ASSERT_TRUE(until(
        [&]
        {
            return uvim_test::read_file(root / "latest-text") == newestText;
        }));
    EXPECT_EQ(uvim_test::read_file(root / "messages"),
              "textDocument/didOpen\ntextDocument/didChange\n");
    client.stop();
    std::filesystem::remove_all(root);
}

TEST(LspAsyncTest, ServerDoesNotKeepAnotherToolsInputPipeOpen)
{
    const auto root = uvim_test::make_temp_dir("uvim_lsp_pipe_isolation_");
    ProcessPipe tool({UVIM_TEST_LSP_SERVER, "--drain"}, "w");
    ASSERT_TRUE(tool.isOpen());
    LspClient client;
    ASSERT_TRUE(client.startServer(UVIM_TEST_LSP_SERVER, root.string()));
    std::promise<int> closed;
    auto result = closed.get_future();
    std::thread closer([&] { closed.set_value(tool.close()); });
    const auto state = result.wait_for(1s);
    // Always release the server before joining, so a handle leak reports a
    // failure instead of hanging the regression suite itself.
    client.stop();
    closer.join();
    EXPECT_EQ(state, std::future_status::ready);
    EXPECT_EQ(result.get(), 0);
    std::filesystem::remove_all(root);
}

TEST(LspAsyncTest, DisablingPendingStartupDoesNotPublishStaleClient)
{
    const auto root = uvim_test::make_temp_dir("uvim_lsp_disabled_");
    uvim_test::write_file(root / "hold-initialize", "");
    Editor editor = Editor::createForTests();
    editor.setProjectRoot(root.string());
    editor.enableClangdLsp(true, root.string(), UVIM_TEST_LSP_SERVER);
    ASSERT_TRUE(until(
        [&] { return std::filesystem::exists(root / "initialize.received"); }));
    const auto started = Clock::now();
    editor.enableClangdLsp(false);
    EXPECT_LT(Clock::now() - started, 500ms);
    uvim_test::write_file(root / "initialize.release", "");
    ASSERT_TRUE(until(
        [&]
        {
            editor.serviceTasks();
            return editor.tasks.idle();
        }));
    EXPECT_FALSE(editor.isClangdLspEnabled());
    EXPECT_FALSE(editor.lspClient);
    std::filesystem::remove_all(root);
}

TEST(LspAsyncTest, StartupFailureUpdatesStatusWithoutPublishingClient)
{
    const auto root = uvim_test::make_temp_dir("uvim_lsp_failure_");
    Editor editor = Editor::createForTests();
    editor.setProjectRoot(root.string());
    editor.enableClangdLsp(true, root.string(),
                           (root / "missing-server").string());
    ASSERT_TRUE(until(
        [&]
        {
            editor.serviceTasks();
            return editor.tasks.idle();
        }));
    EXPECT_FALSE(editor.isClangdLspEnabled());
    EXPECT_FALSE(editor.clangdLspLastError.empty());
    EXPECT_EQ(editor.statusMessage, "clangd LSP: failed to start");
    std::filesystem::remove_all(root);
}

#ifdef UVIM_ENABLE_HTML_LSP
TEST(LspAsyncTest, OpeningAnotherRecognizedFileDoesNotRestartPendingServer)
{
    const auto root = uvim_test::make_temp_dir("uvim_html_async_");
    uvim_test::write_file(root / "hold-initialize", "");
    uvim_test::write_file(root / "second.html", "<html></html>\n");
    Editor editor = Editor::createForTests();
    editor.setProjectRoot(root.string());
    editor.enableHtmlLsp(true, UVIM_TEST_LSP_SERVER, {});
    ASSERT_TRUE(until(
        [&] { return std::filesystem::exists(root / "initialize.received"); }));
    const auto started = Clock::now();
    editor.openFile((root / "second.html").string());
    EXPECT_LT(Clock::now() - started, 500ms);
    EXPECT_EQ(editor.tasks.snapshot().size(), 1u);
    uvim_test::write_file(root / "initialize.release", "");
    ASSERT_TRUE(until(
        [&]
        {
            editor.serviceTasks();
            return editor.tasks.idle();
        }));
    EXPECT_TRUE(editor.isHtmlLspEnabled());
    std::filesystem::remove_all(root);
}
#endif

TEST(LspAsyncTest, CppHeaderIndexIsPublishedAfterBackgroundScan)
{
    const auto root = uvim_test::make_temp_dir("uvim_cpp_index_async_");
    uvim_test::write_file(root / "types.h",
                          "struct Example { int member; };\n");
    Editor editor = Editor::createForTests();
    editor.setProjectRoot(root.string());
    editor.createNewBuffer();
    editor.syntaxHighlighter->ensureCppMemberIndexLoaded();
    EXPECT_FALSE(editor.syntaxHighlighter->isCppMemberIndexLoaded());
    // Duplicate prewarm requests must share the same scan.
    editor.syntaxHighlighter->ensureCppMemberIndexLoaded();
    EXPECT_EQ(editor.tasks.snapshot().size(), 1u);
    auto sm = uvim_test::makeMachine(editor, NormalMode{});
    sm.dispatch('i');
    sm.dispatch('x');
    EXPECT_EQ(editor.lines->front(), "x");
    ASSERT_TRUE(until(
        [&]
        {
            editor.serviceTasks();
            return editor.tasks.idle();
        }));
    EXPECT_TRUE(editor.syntaxHighlighter->isCppMemberIndexLoaded());
    EXPECT_EQ(editor.statusMessage, "C++ header symbols: ready");
    std::filesystem::remove_all(root);
}
