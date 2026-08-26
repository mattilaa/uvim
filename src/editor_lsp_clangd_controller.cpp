#include "editor.h"
#include "enablelog.h"
#include "project_lsp_discovery.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace
{
mla::log::FileLogger LSP_LOG("CLANGD");

#ifdef _WIN32
std::string path_for_clangd_glob(std::filesystem::path path)
{
    std::string out = path.lexically_normal().string();
    for(char& c : out)
    {
        if(c == '\\')
            c = '/';
    }
    return out;
}
#endif

std::string detect_compile_commands_dir(const std::string& rootDir)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root(rootDir);
    fs::path direct = root / "compile_commands.json";
    if(fs::exists(direct, ec) && fs::is_regular_file(direct, ec))
        return root.string();

    const std::vector<fs::path> buildDirs = {
        "build",
        "build-debug",
        "build-release",
        "build_Debug",
        "build_Release",
        "Debug",
        "Release",
        "cmake-build-debug",
        "cmake-build-release",
        fs::path("out") / "build",
    };
    for(const fs::path& buildDir : buildDirs)
    {
        fs::path cc = root / buildDir / "compile_commands.json";
        if(fs::exists(cc, ec) && fs::is_regular_file(cc, ec))
            return cc.parent_path().string();
        ec.clear();
    }

    fs::recursive_directory_iterator it(
        root, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
    for(; it != end; it.increment(ec))
    {
        if(ec)
        {
            ec.clear();
            continue;
        }
        const fs::path p = it->path();
        if(it->is_directory(ec))
        {
            const std::string name = p.filename().string();
            if(name == ".git" || name == ".uvim")
                it.disable_recursion_pending();
            continue;
        }
        if(!it->is_regular_file(ec))
            continue;
        if(p.filename() == "compile_commands.json")
            return p.parent_path().string();
    }

    return {};
}
} // namespace

void Editor::enableClangdLspImpl(bool enable,
                                 const std::string& compileCommandsDir,
                                 const std::string& clangdPath,
                                 const std::string& queryDriverAllowList,
                                 const std::string& projectRootOverride)
{
    clangdLspEnabled = false;
    clangdLspCompileCommandsDir = compileCommandsDir;
    clangdLspPath = clangdPath;
    clangdLspQueryDriverAllowList = queryDriverAllowList;
    clangdLspStartupAttempted = enable;
    clangdLspLastError.clear();

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(!enable)
    {
        if(lspClient)
        {
            lspClient->stop();
            lspClient.reset();
        }
        clangdLspStartupAttempted = false;
        clangdLspActiveProjectRoot.clear();
        return;
    }

    std::string rootDir = ".";
    if(!projectRootOverride.empty())
    {
        rootDir = projectRootOverride;
    }
    else if(!projectRoot.empty())
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
    if(ccdir.empty())
    {
        ccdir = detect_compile_commands_dir(rootDir);
    }

    // If not provided, use a conservative default query-driver allowlist so
    // clangd can discover system include paths (standard library headers etc)
    // from common compilers referenced in compile_commands.json. Users with
    // custom toolchains can pass:
    //   --query-driver "/opt/toolchain/bin/*g++*,/opt/toolchain/bin/*gcc*"
    std::string qd = clangdLspQueryDriverAllowList;
    if(qd.empty())
    {
#ifdef _WIN32
        std::vector<std::string> globs = {
            "C:/Program Files/LLVM/bin/*clang*.exe",
            "C:/Program Files/LLVM/bin/clang++.exe",
            "C:/Program Files/LLVM/bin/clang-cl.exe",
            "C:/Program Files/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/bin/*/*/cl.exe",
            "C:/PROGRA~1/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/bin/*/*/cl.exe"};
        std::filesystem::path clangdDir =
            std::filesystem::path(clangdLspPath).parent_path();
        if(!clangdDir.empty())
            globs.push_back(path_for_clangd_glob(clangdDir / "*clang*.exe"));

        for(size_t i = 0; i < globs.size(); ++i)
        {
            if(i > 0)
                qd += ",";
            qd += globs[i];
        }
#else
        // Only allow executing compilers from typical system locations.
        // clangd expects a comma-separated list of globs/paths.
        qd =
            "/usr/bin/*clang*,/usr/bin/*clang++*,/usr/bin/*gcc*,/usr/bin/*g++*,"
            "/bin/*gcc*,/bin/*g++*,"
            "/usr/local/bin/*clang*,/usr/local/bin/*clang++*,/usr/local/bin/"
            "*gcc*,/usr/local/bin/*g++*,"
            "/opt/homebrew/bin/*clang*,/opt/homebrew/bin/*clang++*,/opt/"
            "homebrew/bin/*gcc*,/opt/homebrew/bin/*g++*";
#endif
    }

    lspClient = std::make_unique<LspClient>();
    lspClient->setLogSignature("CLANGD");
    lspClient->setDiagnosticsEnabled(emitLspDiagnostics);
    if(!lspClient->start(clangdLspPath, rootDir, ccdir, qd))
    {
        std::string error = lspClient->lastError();
        LOG_ERROR(LSP_LOG,
                  "clangd LSP failed to start. path='{}' root='{}' ccdir='{}' error='{}'",
                  clangdLspPath, rootDir, ccdir,
                  error.empty() ? std::string{"failed to start"} : error);
        lspClient.reset();
        if(error.empty())
        {
            clangdLspLastError = "failed to start";
            setStatusMessage("clangd LSP: failed to start");
        }
        else
        {
            clangdLspLastError = error;
            setStatusMessage("clangd LSP failed; see :lspinfo");
        }
        return;
    }

    clangdLspEnabled = true;
    clangdLspCompileCommandsDir = ccdir;
    clangdLspActiveProjectRoot = rootDir;
    clangdLspLastError.clear();
#else
    (void)enable;
    (void)compileCommandsDir;
    (void)clangdPath;
    (void)queryDriverAllowList;
    (void)projectRootOverride;
    setStatusMessage("clangd LSP: not compiled in");
#endif
}

void Editor::activateProjectLspForCurrentBuffer()
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    if(!currentBuffer || !filename || filename->empty())
        return;

    if(isFileType<FileType::Cpp>() && isClangdLspEnabled())
    {
        auto config = project_lsp::findCompileCommandsForFile(*filename);
        if(config)
        {
            const std::string root = config->root.string();
            const std::string commandsDirectory =
                config->commandsDirectory.string();
            if(root != clangdLspActiveProjectRoot ||
               commandsDirectory != clangdLspCompileCommandsDir)
            {
                enableClangdLspImpl(true, commandsDirectory, clangdLspPath,
                                   clangdLspQueryDriverAllowList, root);
            }
        }
        return;
    }

#ifdef UVIM_ENABLE_MLANG_LSP
    if(isFileType<FileType::Mla>() && isMlangLspEnabled())
    {
        auto config = project_lsp::findMlangCommandsForFile(*filename);
        if(config)
        {
            const std::string root = config->root.string();
            if(root != mlangLspActiveProjectRoot)
            {
                enableMlangLspImpl(true, mlangLspPath, mlangLspArgs, root);
            }
        }
    }
#endif
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
