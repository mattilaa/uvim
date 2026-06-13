#include "editor.h"
#include "enablelog.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif
#include <filesystem>
#include <memory>

namespace
{
mla::log::FileLogger LSP_LOG("JSON");
}

void Editor::enableJsonLspImpl(bool enable, const std::string& jsonLspPath,
                               const std::vector<std::string>& jsonLspArgs)
{
    jsonLspEnabled = false;
    this->jsonLspPath = jsonLspPath;
    this->jsonLspArgs = jsonLspArgs;

#ifdef UVIM_ENABLE_JSON_LSP
    if(!enable)
    {
        if(jsonLspClient)
        {
            jsonLspClient->stop();
            jsonLspClient.reset();
        }
        return;
    }

    std::string rootDir = ".";
    if(!projectRoot.empty())
    {
        rootDir = projectRoot;
    }
    else
    {
        std::error_code cwdEc;
        auto cwd = std::filesystem::current_path(cwdEc);
        if(!cwdEc)
            rootDir = cwd.string();
    }

    std::vector<std::string> args = this->jsonLspArgs;
    if(args.empty())
        args.push_back("--stdio");

    jsonLspClient = std::make_unique<LspClient>();
    jsonLspClient->setLogSignature("JSON");
    jsonLspClient->setDiagnosticsEnabled(emitLspDiagnostics);
    if(!jsonLspClient->startServer(this->jsonLspPath, rootDir, args))
    {
        jsonLspClient.reset();
        LOG_ERROR(LSP_LOG, "JSON LSP failed to start. LSP path: {}",
                  this->jsonLspPath);
        return;
    }

    jsonLspEnabled = true;
    LOG_DEBUG(LSP_LOG, "JSON LSP enabled");
#else
    (void)enable;
    (void)jsonLspPath;
    (void)jsonLspArgs;
    LOG_ERROR(LSP_LOG, "JSON LSP is not compiled");
#endif
}

bool Editor::isJsonLspEnabledImpl() const
{
#ifdef UVIM_ENABLE_JSON_LSP
    return jsonLspEnabled && jsonLspClient && jsonLspClient->running();
#else
    return false;
#endif
}
