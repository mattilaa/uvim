#include "editor.h"
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
        if(cssLspClient)
        {
            cssLspClient->stop();
            cssLspClient.reset();
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

    std::vector<std::string> args = this->cssLspArgs;
    if(args.empty())
        args.push_back("--stdio");

    cssLspClient = std::make_unique<LspClient>();
    cssLspClient->setLogSignature("CSS");
    cssLspClient->setDiagnosticsEnabled(emitLspDiagnostics);
    if(!cssLspClient->startServer(this->cssLspPath, rootDir, args))
    {
        cssLspClient.reset();
        LOG_ERROR(LSP_LOG, "CSS LSP failed to start. LSP path: {}",
                  this->cssLspPath);
        return;
    }

    cssLspEnabled = true;
    LOG_DEBUG(LSP_LOG, "CSS LSP enabled");
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
