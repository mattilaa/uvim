#include "editor.h"
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

void Editor::enableMlangLspImpl(bool enable, const std::string& mlangLspPath,
                                const std::vector<std::string>& mlangLspArgs,
                                const std::string& projectRootOverride)
{
    mlangLspEnabled = false;
    this->mlangLspPath = mlangLspPath;
    this->mlangLspArgs = mlangLspArgs;

#ifdef UVIM_ENABLE_MLANG_LSP
    if(!enable)
    {
        if(mlangLspClient)
        {
            mlangLspClient->stop();
            mlangLspClient.reset();
        }
        mlangLspActiveProjectRoot.clear();
        return;
    }

    std::string rootDir = ".";
    if(!projectRootOverride.empty())
    {
        rootDir = projectRootOverride;
    }
    else if(!projectRoot.empty())
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

    mlangLspClient = std::make_unique<LspClient>();
    mlangLspClient->setLogSignature("MLANG");
    mlangLspClient->setDiagnosticsEnabled(emitLspDiagnostics);
    if(!mlangLspClient->startServer(this->mlangLspPath, rootDir, args))
    {
        mlangLspClient.reset();
        LOG_ERROR(LSP_LOG, "Mlang LSP failed to start. LSP path: {}",
                  this->mlangLspPath);
        return;
    }

    mlangLspEnabled = true;
    mlangLspActiveProjectRoot = rootDir;
    LOG_DEBUG(LSP_LOG, "Mlang LSP enabled");
#else
    (void)enable;
    (void)mlangLspPath;
    (void)mlangLspArgs;
    (void)projectRootOverride;
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
