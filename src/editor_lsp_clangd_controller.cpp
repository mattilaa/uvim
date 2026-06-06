#include "editor.h"
#include "enablelog.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace
{
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
} // namespace

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
