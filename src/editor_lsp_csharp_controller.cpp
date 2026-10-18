#include "csharp_lsp.h"
#include "editor.h"
#include "editor_utils.h"
#include "enablelog.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif
#include <filesystem>
#include <memory>

namespace
{
mla::log::FileLogger LSP_LOG("CSHARP");
}

void Editor::enableCSharpLspImpl(bool enable, const std::string& csharpLspPath,
                                 const std::vector<std::string>& csharpLspArgs)
{
    csharpLspEnabled = false;
    this->csharpLspPath = csharpLspPath;
    this->csharpLspArgs = csharpLspArgs;

#ifdef UVIM_ENABLE_CSHARP_LSP
    if(!enable)
    {
        if(csharpLspClient)
        {
            csharpLspClient->stop();
            csharpLspClient.reset();
        }
        return;
    }

    const std::string server = csharp_lsp::resolveServer(this->csharpLspPath);
    if(server.empty())
    {
        setStatusMessage("C# LSP: csharp-ls not found (dotnet tool install "
                         "-g csharp-ls)");
        LOG_ERROR(LSP_LOG, "No C# language server found (configured: {})",
                  this->csharpLspPath);
        return;
    }

    // The server loads the solution or project, so start it there.
    std::string rootDir = csharp_lsp::projectRoot(
        currentBuffer ? currentBuffer->filename : std::string());
    if(rootDir.empty())
        rootDir = projectRoot;
    if(rootDir.empty())
    {
        std::error_code cwdEc;
        auto cwd = std::filesystem::current_path(cwdEc);
        rootDir = cwdEc ? "." : cwd.string();
    }

    csharpLspClient = std::make_unique<LspClient>();
    attachLspWaitHook(*csharpLspClient);
    csharpLspClient->setLogSignature("CSHARP");
    csharpLspClient->setDiagnosticsEnabled(emitLspDiagnostics);
    if(!csharpLspClient->startServer(
           server, rootDir,
           csharp_lsp::serverArgs(server, this->csharpLspArgs)))
    {
        csharpLspClient.reset();
        LOG_ERROR(LSP_LOG, "C# LSP failed to start: {}", server);
        return;
    }

    csharpLspEnabled = true;
    LOG_DEBUG(LSP_LOG, "C# LSP enabled: {} in {}", server, rootDir);
#else
    (void)enable;
    (void)csharpLspPath;
    (void)csharpLspArgs;
    LOG_ERROR(LSP_LOG, "C# LSP is not compiled");
#endif
}

bool Editor::isCSharpLspEnabledImpl() const
{
#ifdef UVIM_ENABLE_CSHARP_LSP
    return csharpLspEnabled && csharpLspClient && csharpLspClient->running();
#else
    return false;
#endif
}

void Editor::syncCSharpLspIfNeeded(bool force)
{
#ifdef UVIM_ENABLE_CSHARP_LSP
    if(!currentBuffer || currentBuffer->filename.empty() || !lines ||
       !isFileType<FileType::CSharp>() || !isCSharpLspEnabled())
        return;
    if(currentBuffer->lspOpenDeferred)
    {
        if(!force &&
           std::chrono::steady_clock::now() < currentBuffer->lspOpenDeferUntil)
            return;
        currentBuffer->lspOpenDeferred = false;
    }
    if(!force && !currentBuffer->lspSyncNeeded && !*dirty)
        return;

    const size_t hash = editor::helper::hash_lines(*lines);
    if(!currentBuffer->lspHashValid || hash != currentBuffer->lspContentHash)
    {
        std::string text;
        text.reserve(lines->size() * 80);
        for(size_t i = 0; i < lines->size(); ++i)
        {
            text += (*lines)[i];
            if(i + 1 < lines->size())
                text.push_back('\n');
        }
        csharpLspClient->didChange(currentBuffer->filename, text, "csharp");
        currentBuffer->lspContentHash = hash;
        currentBuffer->lspHashValid = true;
        needsFullRedraw = true;
    }
    currentBuffer->lspSyncNeeded = false;
#else
    (void)force;
#endif
}
