#include "editor.h"
#include "enablelog.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif
#include <filesystem>
#include <memory>

namespace
{
mla::log::FileLogger LSP_LOG("ROBOT");
}

void Editor::enableRobotLspImpl(bool enable, const std::string& robotLspPath,
                                const std::vector<std::string>& robotLspArgs)
{
    robotLspEnabled = false;
    this->robotLspPath = robotLspPath;
    this->robotLspArgs = robotLspArgs;

#ifdef UVIM_ENABLE_ROBOT_LSP
    if(!enable)
    {
        if(robotLspClient)
        {
            robotLspClient->stop();
            robotLspClient.reset();
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

    std::vector<std::string> args = this->robotLspArgs;
    if(args.empty())
    {
        args.push_back("--stdio");
    }

    robotLspClient = std::make_unique<LspClient>();
    robotLspClient->setLogSignature("ROBOT");
    robotLspClient->setDiagnosticsEnabled(emitLspDiagnostics);
    if(!robotLspClient->startServer(this->robotLspPath, rootDir, args))
    {
        LOG_ERROR(LSP_LOG, "Robot LSP failed to start, LSP path: {}",
                  this->robotLspPath.c_str());
        robotLspClient.reset();
        return;
    }

    robotLspEnabled = true;
    LOG_DEBUG(LSP_LOG, "Robot LSP enabled");
#else
    (void)enable;
    (void)robotLspPath;
    (void)robotLspArgs;
    LOG_ERROR(LSP_LOG, "Robot LSP is not compiled in");
#endif
}

bool Editor::isRobotLspEnabledImpl() const
{
#ifdef UVIM_ENABLE_ROBOT_LSP
    return robotLspEnabled && robotLspClient && robotLspClient->running();
#else
    return false;
#endif
}
