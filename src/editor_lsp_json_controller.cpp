#include "editor.h"
#include "editor_lsp_controller.h"
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

void Editor::enableJsonLspImpl(bool enable, const std::string& serverPath,
                               const std::vector<std::string>& serverArgs)
{
    jsonLspEnabled = false;
    this->jsonLspPath = serverPath;
    this->jsonLspArgs = serverArgs;

#ifdef UVIM_ENABLE_JSON_LSP
    if(!enable)
    {
        lspController->disable(jsonLspClient, jsonLspEnabled);
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

    lspController->start(
        "JSON", jsonLspClient, jsonLspEnabled,
        [path = this->jsonLspPath, rootDir, args](LspClient& client)
        { return client.startServer(path, rootDir, args); });
#else
    (void)enable;
    (void)serverPath;
    (void)serverArgs;
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
