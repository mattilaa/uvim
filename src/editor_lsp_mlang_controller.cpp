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
mla::log::FileLogger LSP_LOG("MLANG");
}

void Editor::enableMlangLspImpl(
    bool enable,
    const std::string& serverPath,
    const std::vector<std::string>& serverArgs)
{
    mlangLspEnabled = false;
    this->mlangLspPath = serverPath;
    this->mlangLspArgs = serverArgs;

#ifdef UVIM_ENABLE_MLANG_LSP
    if(!enable)
    {
        lspController->disable(mlangLspClient, mlangLspEnabled);
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

    std::vector<std::string> args = this->mlangLspArgs;
    if(args.empty())
        args.push_back("--stdio");

    lspController->start(
        "Mlang",
        mlangLspClient,
        mlangLspEnabled,
        [path = this->mlangLspPath, rootDir, args](LspClient& client)
        { return client.startServer(path, rootDir, args); });
#else
    (void)enable;
    (void)serverPath;
    (void)serverArgs;
    LOG_ERROR(LSP_LOG, "Mlang LSP is not compiled");
#endif
}

bool Editor::isMlangLspEnabledImpl() const
{
#ifdef UVIM_ENABLE_MLANG_LSP
    return mlangLspEnabled && mlangLspClient && mlangLspClient->running();
#else
    return false;
#endif
}
