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
mla::log::FileLogger LSP_LOG("PYTHON");
}

void Editor::enablePythonLspImpl(
    bool enable,
    const std::string& serverPath,
    const std::vector<std::string>& serverArgs)
{
    pythonLspEnabled = false;
    this->pythonLspPath = serverPath;
    this->pythonLspArgs = serverArgs;

#ifdef UVIM_ENABLE_PYTHON_LSP
    if(!enable)
    {
        lspController->disable(pythonLspClient, pythonLspEnabled);
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

    std::vector<std::string> args = this->pythonLspArgs;

    lspController->start(
        "Python",
        pythonLspClient,
        pythonLspEnabled,
        [path = this->pythonLspPath, rootDir, args](LspClient& client)
        { return client.startServer(path, rootDir, args); });
#else
    (void)enable;
    (void)serverPath;
    (void)serverArgs;
    LOG_ERROR(LSP_LOG, "python LSP support is not compiled");
#endif
}

bool Editor::isPythonLspEnabledImpl() const
{
#ifdef UVIM_ENABLE_PYTHON_LSP
    return pythonLspEnabled && pythonLspClient && pythonLspClient->running();
#else
    return false;
#endif
}
