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
mla::log::FileLogger LSP_LOG("TS");
}

void Editor::enableTsLspImpl(
    bool enable,
    const std::string& serverPath,
    const std::vector<std::string>& serverArgs)
{
    tsLspEnabled = false;
    this->tsLspPath = serverPath;
    this->tsLspArgs = serverArgs;

#ifdef UVIM_ENABLE_TS_LSP
    if(!enable)
    {
        lspController->disable(tsLspClient, tsLspEnabled);
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

    lspController->start(
        "TypeScript",
        tsLspClient,
        tsLspEnabled,
        [path = this->tsLspPath, rootDir, args](LspClient& client)
        { return client.startServer(path, rootDir, args); });
#else
    (void)enable;
    (void)serverPath;
    (void)serverArgs;
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
