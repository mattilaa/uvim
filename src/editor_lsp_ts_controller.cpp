#include "editor.h"
#include "enablelog.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif
#include <filesystem>
#include <memory>

namespace
{
mla::log::FileLogger LSP_LOG("TS");
}

void Editor::enableTsLspImpl(bool enable, const std::string& tsLspPath,
                             const std::vector<std::string>& tsLspArgs)
{
    tsLspEnabled = false;
    this->tsLspPath = tsLspPath;
    this->tsLspArgs = tsLspArgs;

#ifdef UVIM_ENABLE_TS_LSP
    if(!enable)
    {
        if(tsLspClient)
        {
            tsLspClient->stop();
            tsLspClient.reset();
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

    std::vector<std::string> args = this->tsLspArgs;
    if(args.empty())
        args.push_back("--stdio");

    tsLspClient = std::make_unique<LspClient>();
    tsLspClient->setLogSignature("TS");
    if(!tsLspClient->startServer(this->tsLspPath, rootDir, args))
    {
        tsLspClient.reset();
        LOG_ERROR(LSP_LOG, "TypeScript LSP failed to start. LSP path: {}",
                  this->tsLspPath);
        return;
    }

    tsLspEnabled = true;
    LOG_DEBUG(LSP_LOG, "TypeScript LSP enabled");
#else
    (void)enable;
    (void)tsLspPath;
    (void)tsLspArgs;
    LOG_ERROR(LSP_LOG, "TypeScript LSP is not compiled");
#endif
}

bool Editor::isTsLspEnabledImpl() const
{
#ifdef UVIM_ENABLE_TS_LSP
    return tsLspEnabled && tsLspClient && tsLspClient->running();
#else
    return false;
#endif
}
