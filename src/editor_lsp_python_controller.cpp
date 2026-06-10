#include "editor.h"
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

void Editor::enablePythonLspImpl(bool enable, const std::string& pythonLspPath,
                                 const std::vector<std::string>& pythonLspArgs)
{
    pythonLspEnabled = false;
    this->pythonLspPath = pythonLspPath;
    this->pythonLspArgs = pythonLspArgs;

#ifdef UVIM_ENABLE_PYTHON_LSP
    if(!enable)
    {
        if(pythonLspClient)
        {
            pythonLspClient->stop();
            pythonLspClient.reset();
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

    std::vector<std::string> args = this->pythonLspArgs;

    pythonLspClient = std::make_unique<LspClient>();
    pythonLspClient->setLogSignature("PYTHON");
    if(!pythonLspClient->startServer(this->pythonLspPath, rootDir, args))
    {
        pythonLspClient.reset();

        LOG_ERROR(LSP_LOG, "Python LSP failed to start. Python LSP path: {}",
                  this->pythonLspPath);
        return;
    }

    pythonLspEnabled = true;
    LOG_DEBUG(LSP_LOG, "Python LSP enabled");
#else
    (void)enable;
    (void)pythonLspPath;
    (void)pythonLspArgs;
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
