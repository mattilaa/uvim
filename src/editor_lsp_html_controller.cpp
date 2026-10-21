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
        lspController->disable(htmlLspClient, htmlLspEnabled);
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

    lspController->start(
        "HTML", htmlLspClient, htmlLspEnabled,
        [path = this->htmlLspPath, rootDir, args](LspClient& client)
        { return client.startServer(path, rootDir, args); });
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
