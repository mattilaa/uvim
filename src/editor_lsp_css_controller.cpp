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
mla::log::FileLogger LSP_LOG("CSS");
}

void Editor::enableCssLspImpl(bool enable, const std::string& cssLspPath,
                              const std::vector<std::string>& cssLspArgs)
{
    cssLspEnabled = false;
    this->cssLspPath = cssLspPath;
    this->cssLspArgs = cssLspArgs;

#ifdef UVIM_ENABLE_CSS_LSP
    if(!enable)
    {
        lspController->disable(cssLspClient, cssLspEnabled);
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

    std::vector<std::string> args = this->cssLspArgs;
    if(args.empty())
        args.push_back("--stdio");

    lspController->start(
        "CSS", cssLspClient, cssLspEnabled,
        [path = this->cssLspPath, rootDir, args](LspClient& client)
        { return client.startServer(path, rootDir, args); });
#else
    (void)enable;
    (void)cssLspPath;
    (void)cssLspArgs;
    LOG_ERROR(LSP_LOG, "CSS LSP is not compiled");
#endif
}

bool Editor::isCssLspEnabledImpl() const
{
#ifdef UVIM_ENABLE_CSS_LSP
    return cssLspEnabled && cssLspClient && cssLspClient->running();
#else
    return false;
#endif
}
