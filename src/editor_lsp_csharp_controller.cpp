#include "csharp_lsp.h"
#include "editor.h"
#include "editor_lsp_controller.h"
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
        lspController->disable(csharpLspClient, csharpLspEnabled);
        return;
    }

    // Resolve the server and solution on the worker too: PATH and project
    // searches can be slow on Windows/network drives.
    const std::string filename = currentBuffer ? currentBuffer->filename : "";
    const std::string root = projectRoot;
    lspController->start(
        "C#", csharpLspClient, csharpLspEnabled,
        [path = this->csharpLspPath, args = this->csharpLspArgs, filename,
         root](LspClient& client)
        {
            const std::string server = csharp_lsp::resolveServer(path);
            std::string rootDir = csharp_lsp::projectRoot(filename);
            if(rootDir.empty())
                rootDir = root;
            if(rootDir.empty())
            {
                std::error_code ec;
                rootDir = std::filesystem::current_path(ec).string();
                if(ec)
                    rootDir = ".";
            }
            return client.startServer(server, rootDir,
                                      csharp_lsp::serverArgs(server, args));
        });
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
