#include "editor_lsp_controller.h"
#include "editor.h"
#include "enablelog.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif
#include <filesystem>
#include <memory>

EditorLspController::EditorLspController(Editor& editor) : editor(editor) {}

bool EditorLspController::starting(const std::unique_ptr<LspClient>& slot) const
{
    const auto it = pending.find(&slot);
    return it != pending.end() && it->second;
}

#ifdef UVIM_ENABLE_CLANGD_LSP
void EditorLspController::retire(std::unique_ptr<LspClient> client)
{
    if(!client)
        return;
    auto owned = std::shared_ptr<LspClient>(std::move(client));
    editor.submitTask(
        "stopping LSP",
        "",
        [owned](TaskQueue::Context&) { owned->stop(); });
}

void EditorLspController::disable(
    std::unique_ptr<LspClient>& slot,
    bool& enabled)
{
    ++generations[&slot];
    pending[&slot] = false;
    enabled = false;
    retire(std::move(slot));
}

void EditorLspController::start(
    const std::string& label,
    std::unique_ptr<LspClient>& slot,
    bool& enabled,
    std::function<bool(LspClient&)> launch,
    std::function<void(bool, const std::string&)> completed)
{
    disable(slot, enabled);
    const size_t generation = generations[&slot];
    pending[&slot] = true;

    struct Result
    {
        std::unique_ptr<LspClient> client;
        std::string error;
    };

    auto result = std::make_shared<Result>();
    const bool diagnostics = editor.emitLspDiagnostics;
    editor.setStatusMessage(label + " LSP: starting");
    editor.needsFullRedraw = true;
    auto abort = [this, &slot, generation, result, label]
    {
        retire(std::move(result->client));
        if(generations[&slot] == generation)
        {
            pending[&slot] = false;
            editor.setStatusMessage(label + " LSP: startup cancelled");
            editor.needsFullRedraw = true;
        }
    };
    editor.submitTask(
        "starting " + label + " LSP",
        "lsp-start:" + label,
        [result, launch = std::move(launch), diagnostics, label](
            TaskQueue::Context& ctx)
        {
            if(ctx.cancelled())
                return;
            ctx.detail("initializing");
            auto client = std::make_unique<LspClient>();
            client->setLogSignature(label);
            client->setDiagnosticsEnabled(diagnostics);
            if(launch(*client))
                result->client = std::move(client);
            else
                result->error = client->lastError().empty()
                                    ? "failed to start"
                                    : client->lastError();
        },
        [this,
         &slot,
         &enabled,
         generation,
         result,
         label,
         completed = std::move(completed)]
        {
            if(generations[&slot] != generation)
            {
                retire(std::move(result->client));
                return;
            }
            pending[&slot] = false;
            slot = std::move(result->client);
            enabled = slot && slot->running();
            if(slot)
            {
                slot->setDiagnosticsEnabled(editor.emitLspDiagnostics);
                editor.attachLspWaitHook(*slot);
            }
            // A restarted server needs fresh didOpen notifications, including
            // buffers which were already synchronized to the previous server.
            if(enabled)
                for(auto& buffer : editor.buffers)
                {
                    buffer->lspSyncNeeded = true;
                    buffer->lspHashValid = false;
                    buffer->lspSemanticTokensValid = false;
                }
            editor.setStatusMessage(
                label + (enabled ? " LSP: ready" : " LSP: failed to start"));
            if(completed)
                completed(enabled, result->error);
            editor.needsFullRedraw = true;
        },
        std::move(abort));
}
#endif

void EditorLspController::enableClangdLsp(
    bool enable,
    const std::string& compileCommandsDir,
    const std::string& clangdPath,
    const std::string& queryDriverAllowList)
{
    editor.enableClangdLspImpl(
        enable,
        compileCommandsDir,
        clangdPath,
        queryDriverAllowList);
}

bool EditorLspController::isClangdLspEnabled() const
{
    return editor.isClangdLspEnabledImpl();
}

void EditorLspController::enableRobotLsp(
    bool enable,
    const std::string& robotLspPath,
    const std::vector<std::string>& robotLspArgs)
{
    editor.enableRobotLspImpl(enable, robotLspPath, robotLspArgs);
}

bool EditorLspController::isRobotLspEnabled() const
{
    return editor.isRobotLspEnabledImpl();
}

void EditorLspController::enablePythonLsp(
    bool enable,
    const std::string& pythonLspPath,
    const std::vector<std::string>& pythonLspArgs)
{
    editor.enablePythonLspImpl(enable, pythonLspPath, pythonLspArgs);
}

bool EditorLspController::isPythonLspEnabled() const
{
    return editor.isPythonLspEnabledImpl();
}

void EditorLspController::enableMlangLsp(
    bool enable,
    const std::string& mlangLspPath,
    const std::vector<std::string>& mlangLspArgs)
{
    editor.enableMlangLspImpl(enable, mlangLspPath, mlangLspArgs);
}

bool EditorLspController::isMlangLspEnabled() const
{
    return editor.isMlangLspEnabledImpl();
}

void EditorLspController::enableHtmlLsp(
    bool enable,
    const std::string& htmlLspPath,
    const std::vector<std::string>& htmlLspArgs)
{
    editor.enableHtmlLspImpl(enable, htmlLspPath, htmlLspArgs);
}

bool EditorLspController::isHtmlLspEnabled() const
{
    return editor.isHtmlLspEnabledImpl();
}

void EditorLspController::enableCssLsp(
    bool enable,
    const std::string& cssLspPath,
    const std::vector<std::string>& cssLspArgs)
{
    editor.enableCssLspImpl(enable, cssLspPath, cssLspArgs);
}

bool EditorLspController::isCssLspEnabled() const
{
    return editor.isCssLspEnabledImpl();
}

void EditorLspController::enableJsonLsp(
    bool enable,
    const std::string& jsonLspPath,
    const std::vector<std::string>& jsonLspArgs)
{
    editor.enableJsonLspImpl(enable, jsonLspPath, jsonLspArgs);
}

bool EditorLspController::isJsonLspEnabled() const
{
    return editor.isJsonLspEnabledImpl();
}

void EditorLspController::enableTsLsp(
    bool enable,
    const std::string& tsLspPath,
    const std::vector<std::string>& tsLspArgs)
{
    editor.enableTsLspImpl(enable, tsLspPath, tsLspArgs);
}

bool EditorLspController::isTsLspEnabled() const
{
    return editor.isTsLspEnabledImpl();
}

void EditorLspController::enableCSharpLsp(
    bool enable,
    const std::string& csharpLspPath,
    const std::vector<std::string>& csharpLspArgs)
{
    editor.enableCSharpLspImpl(enable, csharpLspPath, csharpLspArgs);
}

bool EditorLspController::isCSharpLspEnabled() const
{
    return editor.isCSharpLspEnabledImpl();
}
