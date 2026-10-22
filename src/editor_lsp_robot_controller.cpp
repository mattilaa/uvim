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
mla::log::FileLogger LSP_LOG("ROBOT");
}

void Editor::enableRobotLspImpl(bool enable, const std::string& serverPath,
                                const std::vector<std::string>& serverArgs)
{
    robotLspEnabled = false;
    this->robotLspPath = serverPath;
    this->robotLspArgs = serverArgs;

#ifdef UVIM_ENABLE_ROBOT_LSP
    if(!enable)
    {
        lspController->disable(robotLspClient, robotLspEnabled);
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

    lspController->start(
        "Robot", robotLspClient, robotLspEnabled,
        [path = this->robotLspPath, rootDir, args](LspClient& client)
        { return client.startServer(path, rootDir, args); });
#else
    (void)enable;
    (void)serverPath;
    (void)serverArgs;
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
