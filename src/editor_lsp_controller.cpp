#include "editor_lsp_controller.h"
#include "editor.h"
#include "enablelog.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif
#include <filesystem>
#include <memory>

EditorLspController::EditorLspController(Editor& editor) : editor(editor) {}

void EditorLspController::enableClangdLsp(
    bool enable, const std::string& compileCommandsDir,
    const std::string& clangdPath, const std::string& queryDriverAllowList)
{
    editor.enableClangdLspImpl(enable, compileCommandsDir, clangdPath,
                               queryDriverAllowList);
}

bool EditorLspController::isClangdLspEnabled() const
{
    return editor.isClangdLspEnabledImpl();
}

void EditorLspController::enableRobotLsp(
    bool enable, const std::string& robotLspPath,
    const std::vector<std::string>& robotLspArgs)
{
    editor.enableRobotLspImpl(enable, robotLspPath, robotLspArgs);
}

bool EditorLspController::isRobotLspEnabled() const
{
    return editor.isRobotLspEnabledImpl();
}

void EditorLspController::enablePythonLsp(
    bool enable, const std::string& pythonLspPath,
    const std::vector<std::string>& pythonLspArgs)
{
    editor.enablePythonLspImpl(enable, pythonLspPath, pythonLspArgs);
}

bool EditorLspController::isPythonLspEnabled() const
{
    return editor.isPythonLspEnabledImpl();
}

void EditorLspController::enableMlangLsp(
    bool enable, const std::string& mlangLspPath,
    const std::vector<std::string>& mlangLspArgs)
{
    editor.enableMlangLspImpl(enable, mlangLspPath, mlangLspArgs);
}

bool EditorLspController::isMlangLspEnabled() const
{
    return editor.isMlangLspEnabledImpl();
}

void EditorLspController::enableHtmlLsp(
    bool enable, const std::string& htmlLspPath,
    const std::vector<std::string>& htmlLspArgs)
{
    editor.enableHtmlLspImpl(enable, htmlLspPath, htmlLspArgs);
}

bool EditorLspController::isHtmlLspEnabled() const
{
    return editor.isHtmlLspEnabledImpl();
}

void EditorLspController::enableCssLsp(
    bool enable, const std::string& cssLspPath,
    const std::vector<std::string>& cssLspArgs)
{
    editor.enableCssLspImpl(enable, cssLspPath, cssLspArgs);
}

bool EditorLspController::isCssLspEnabled() const
{
    return editor.isCssLspEnabledImpl();
}

void EditorLspController::enableJsonLsp(
    bool enable, const std::string& jsonLspPath,
    const std::vector<std::string>& jsonLspArgs)
{
    editor.enableJsonLspImpl(enable, jsonLspPath, jsonLspArgs);
}

bool EditorLspController::isJsonLspEnabled() const
{
    return editor.isJsonLspEnabledImpl();
}

void EditorLspController::enableTsLsp(bool enable, const std::string& tsLspPath,
                                      const std::vector<std::string>& tsLspArgs)
{
    editor.enableTsLspImpl(enable, tsLspPath, tsLspArgs);
}

bool EditorLspController::isTsLspEnabled() const
{
    return editor.isTsLspEnabledImpl();
}

void Editor::enableClangdLspImpl(bool enable,
                                 const std::string& compileCommandsDir,
                                 const std::string& clangdPath,
                                 const std::string& queryDriverAllowList)
{
    clangdLspEnabled = false;
    clangdLspCompileCommandsDir = compileCommandsDir;
    clangdLspPath = clangdPath;
    clangdLspQueryDriverAllowList = queryDriverAllowList;

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(!enable)
    {
        if(lspClient)
        {
            lspClient->stop();
            lspClient.reset();
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

    // Auto-detect compile_commands.json if caller didn't specify --ccdir
    std::string ccdir = clangdLspCompileCommandsDir;
    auto exists = [](const std::string& p)
    {
        std::error_code ec;
        return std::filesystem::exists(p, ec);
    };

    if(ccdir.empty())
    {
        if(exists(rootDir + "/compile_commands.json"))
            ccdir = rootDir;
        else if(exists(rootDir + "/build/compile_commands.json"))
            ccdir = rootDir + "/build";
    }

    // If not provided, use a conservative default query-driver allowlist so
    // clangd can discover system include paths (standard library headers etc)
    // from common compilers referenced in compile_commands.json. Users with
    // custom toolchains can pass:
    //   --query-driver "/opt/toolchain/bin/*g++*,/opt/toolchain/bin/*gcc*"
    std::string qd = clangdLspQueryDriverAllowList;
    if(qd.empty())
    {
        // Only allow executing compilers from typical system locations.
        // clangd expects a comma-separated list of globs/paths.
        qd =
            "/usr/bin/*clang*,/usr/bin/*clang++*,/usr/bin/*gcc*,/usr/bin/*g++*,"
            "/bin/*gcc*,/bin/*g++*,"
            "/usr/local/bin/*clang*,/usr/local/bin/*clang++*,/usr/local/bin/"
            "*gcc*,/usr/local/bin/*g++*,"
            "/opt/homebrew/bin/*clang*,/opt/homebrew/bin/*clang++*,/opt/"
            "homebrew/bin/*gcc*,/opt/homebrew/bin/*g++*";
    }

    lspClient = std::make_unique<LspClient>();
    if(!lspClient->start(clangdLspPath, rootDir, ccdir, qd))
    {
        lspClient.reset();
        setStatusMessage("clangd LSP: failed to start");
        return;
    }

    clangdLspEnabled = true;
    clangdLspCompileCommandsDir = ccdir;
#else
    (void)enable;
    (void)compileCommandsDir;
    (void)clangdPath;
    setStatusMessage("clangd LSP: not compiled in");
#endif
}

bool Editor::isClangdLspEnabledImpl() const
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    return clangdLspEnabled && lspClient && lspClient->running();
#else
    return false;
#endif
}

void Editor::enableRobotLspImpl(bool enable, const std::string& robotLspPath,
                                const std::vector<std::string>& robotLspArgs)
{
    robotLspEnabled = false;
    this->robotLspPath = robotLspPath;
    this->robotLspArgs = robotLspArgs;

#ifdef UVIM_ENABLE_CLANGD_LSP
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
    if(!robotLspClient->startServer(this->robotLspPath, rootDir, args))
    {
        LOG_ERROR(LOG, "Robot LSP failed to start, LSP path: {}",
                  this->robotLspPath.c_str());
        robotLspClient.reset();
        return;
    }

    robotLspEnabled = true;
    LOG_DEBUG(LOG, "Robot LSP enabled");
#else
    (void)enable;
    (void)robotLspPath;
    (void)robotLspArgs;
    LOG_ERROR(LOG, "Robot LSP is not compiled in");
#endif
}

bool Editor::isRobotLspEnabledImpl() const
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    return robotLspEnabled && robotLspClient && robotLspClient->running();
#else
    return false;
#endif
}

void Editor::enablePythonLspImpl(bool enable, const std::string& pythonLspPath,
                                 const std::vector<std::string>& pythonLspArgs)
{
    pythonLspEnabled = false;
    this->pythonLspPath = pythonLspPath;
    this->pythonLspArgs = pythonLspArgs;

#ifdef UVIM_ENABLE_CLANGD_LSP
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
    if(!pythonLspClient->startServer(this->pythonLspPath, rootDir, args))
    {
        pythonLspClient.reset();

        LOG_ERROR(LOG, "Python LSP failed to start. Python LSP path: {}",
                  this->pythonLspPath);
        return;
    }

    pythonLspEnabled = true;
    LOG_DEBUG(LOG, "Python LSP enabled");
#else
    (void)enable;
    (void)pythonLspPath;
    (void)pythonLspArgs;
    LOG_ERROR(LOG, "python LSP support is not compiled");
#endif
}

bool Editor::isPythonLspEnabledImpl() const
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    return pythonLspEnabled && pythonLspClient && pythonLspClient->running();
#else
    return false;
#endif
}

void Editor::enableMlangLspImpl(bool enable, const std::string& mlangLspPath,
                                const std::vector<std::string>& mlangLspArgs)
{
    mlangLspEnabled = false;
    this->mlangLspPath = mlangLspPath;
    this->mlangLspArgs = mlangLspArgs;

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(!enable)
    {
        if(mlangLspClient)
        {
            mlangLspClient->stop();
            mlangLspClient.reset();
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

    std::vector<std::string> args = this->mlangLspArgs;
    if(args.empty())
        args.push_back("--stdio");

    mlangLspClient = std::make_unique<LspClient>();
    if(!mlangLspClient->startServer(this->mlangLspPath, rootDir, args))
    {
        mlangLspClient.reset();
        LOG_ERROR(LOG, "Mlang LSP failed to start. LSP path: {}",
                  this->mlangLspPath);
        return;
    }

    mlangLspEnabled = true;
    LOG_DEBUG(LOG, "Mlang LSP enabled");
#else
    (void)enable;
    (void)mlangLspPath;
    (void)mlangLspArgs;
    LOG_ERROR(LOG, "Mlang LSP is not compiled");
#endif
}

bool Editor::isMlangLspEnabledImpl() const
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    return mlangLspEnabled && mlangLspClient && mlangLspClient->running();
#else
    return false;
#endif
}

void Editor::enableHtmlLspImpl(bool enable, const std::string& htmlLspPath,
                               const std::vector<std::string>& htmlLspArgs)
{
    htmlLspEnabled = false;
    this->htmlLspPath = htmlLspPath;
    this->htmlLspArgs = htmlLspArgs;

#ifdef UVIM_ENABLE_CLANGD_LSP
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
    if(!htmlLspClient->startServer(this->htmlLspPath, rootDir, args))
    {
        htmlLspClient.reset();
        LOG_ERROR(LOG, "HTML LSP failed to start. LSP path: {}",
                  this->htmlLspPath);
        return;
    }

    htmlLspEnabled = true;
    LOG_DEBUG(LOG, "HTML LSP enabled");
#else
    (void)enable;
    (void)htmlLspPath;
    (void)htmlLspArgs;
    LOG_ERROR(LOG, "HTML LSP is not compiled");
#endif
}

bool Editor::isHtmlLspEnabledImpl() const
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    return htmlLspEnabled && htmlLspClient && htmlLspClient->running();
#else
    return false;
#endif
}

void Editor::enableCssLspImpl(bool enable, const std::string& cssLspPath,
                              const std::vector<std::string>& cssLspArgs)
{
    cssLspEnabled = false;
    this->cssLspPath = cssLspPath;
    this->cssLspArgs = cssLspArgs;

#ifdef UVIM_ENABLE_CLANGD_LSP
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
    if(!cssLspClient->startServer(this->cssLspPath, rootDir, args))
    {
        cssLspClient.reset();
        LOG_ERROR(LOG, "CSS LSP failed to start. LSP path: {}",
                  this->cssLspPath);
        return;
    }

    cssLspEnabled = true;
    LOG_DEBUG(LOG, "CSS LSP enabled");
#else
    (void)enable;
    (void)cssLspPath;
    (void)cssLspArgs;
    LOG_ERROR(LOG, "CSS LSP is not compiled");
#endif
}

bool Editor::isCssLspEnabledImpl() const
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    return cssLspEnabled && cssLspClient && cssLspClient->running();
#else
    return false;
#endif
}

void Editor::enableJsonLspImpl(bool enable, const std::string& jsonLspPath,
                               const std::vector<std::string>& jsonLspArgs)
{
    jsonLspEnabled = false;
    this->jsonLspPath = jsonLspPath;
    this->jsonLspArgs = jsonLspArgs;

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(!enable)
    {
        if(jsonLspClient)
        {
            jsonLspClient->stop();
            jsonLspClient.reset();
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

    std::vector<std::string> args = this->jsonLspArgs;
    if(args.empty())
        args.push_back("--stdio");

    jsonLspClient = std::make_unique<LspClient>();
    if(!jsonLspClient->startServer(this->jsonLspPath, rootDir, args))
    {
        jsonLspClient.reset();
        LOG_ERROR(LOG, "JSON LSP failed to start. LSP path: {}",
                  this->jsonLspPath);
        return;
    }

    jsonLspEnabled = true;
    LOG_DEBUG(LOG, "JSON LSP enabled");
#else
    (void)enable;
    (void)jsonLspPath;
    (void)jsonLspArgs;
    LOG_ERROR(LOG, "JSON LSP is not compiled");
#endif
}

bool Editor::isJsonLspEnabledImpl() const
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    return jsonLspEnabled && jsonLspClient && jsonLspClient->running();
#else
    return false;
#endif
}

void Editor::enableTsLspImpl(bool enable, const std::string& tsLspPath,
                             const std::vector<std::string>& tsLspArgs)
{
    tsLspEnabled = false;
    this->tsLspPath = tsLspPath;
    this->tsLspArgs = tsLspArgs;

#ifdef UVIM_ENABLE_CLANGD_LSP
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
    if(!tsLspClient->startServer(this->tsLspPath, rootDir, args))
    {
        tsLspClient.reset();
        LOG_ERROR(LOG, "TypeScript LSP failed to start. LSP path: {}",
                  this->tsLspPath);
        return;
    }

    tsLspEnabled = true;
    LOG_DEBUG(LOG, "TypeScript LSP enabled");
#else
    (void)enable;
    (void)tsLspPath;
    (void)tsLspArgs;
    LOG_ERROR(LOG, "TypeScript LSP is not compiled");
#endif
}

bool Editor::isTsLspEnabledImpl() const
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    return tsLspEnabled && tsLspClient && tsLspClient->running();
#else
    return false;
#endif
}
