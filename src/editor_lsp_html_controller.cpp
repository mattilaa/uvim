#include "editor.h"
#include "enablelog.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif
#include <filesystem>
#include <memory>

namespace
{
mla::log::FileLogger LSP_LOG("HTML");
}

void Editor::enableHtmlLspImpl(bool enable, const std::string& htmlLspPath,
                               const std::vector<std::string>& htmlLspArgs)
{
    htmlLspEnabled = false;
    this->htmlLspPath = htmlLspPath;
    this->htmlLspArgs = htmlLspArgs;

#ifdef UVIM_ENABLE_HTML_LSP
    if(!enable)
    {
        if(htmlLspClient)
        {
            htmlLspClient->stop();
            htmlLspClient.reset();
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

    std::vector<std::string> args = this->htmlLspArgs;
    if(args.empty())
        args.push_back("--stdio");

    htmlLspClient = std::make_unique<LspClient>();
    htmlLspClient->setLogSignature("HTML");
    htmlLspClient->setDiagnosticsEnabled(emitLspDiagnostics);
    if(!htmlLspClient->startServer(this->htmlLspPath, rootDir, args))
    {
        htmlLspClient.reset();
        LOG_ERROR(LSP_LOG, "HTML LSP failed to start. LSP path: {}",
                  this->htmlLspPath);
        return;
    }

    htmlLspEnabled = true;
    LOG_DEBUG(LSP_LOG, "HTML LSP enabled");
#else
    (void)enable;
    (void)htmlLspPath;
    (void)htmlLspArgs;
    LOG_ERROR(LSP_LOG, "HTML LSP is not compiled");
#endif
}

bool Editor::isHtmlLspEnabledImpl() const
{
#ifdef UVIM_ENABLE_HTML_LSP
    return htmlLspEnabled && htmlLspClient && htmlLspClient->running();
#else
    return false;
#endif
}
