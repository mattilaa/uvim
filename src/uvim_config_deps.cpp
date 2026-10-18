#include "uvim_config_deps.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace uvim_config_deps
{
namespace
{
// Package names per package manager; empty = not packaged there.
struct Packages
{
    const char* brew = "";
    const char* apt = "";
    const char* dnf = "";
    const char* pacman = "";
    const char* winget = "";
};

bool isRoot()
{
#ifdef _WIN32
    return false;
#else
    return ::geteuid() == 0;
#endif
}

// System package install command, or "" when `manager` has no package.
std::string systemInstall(PackageManager manager, const Packages& packages)
{
    const std::string sudo = isRoot() ? "" : "sudo ";
    switch(manager)
    {
    case PackageManager::Brew:
        return *packages.brew ? std::string("brew install ") + packages.brew
                              : "";
    case PackageManager::Apt:
        return *packages.apt
                   ? sudo + "apt-get install -y " + packages.apt
                   : "";
    case PackageManager::Dnf:
        return *packages.dnf ? sudo + "dnf install -y " + packages.dnf : "";
    case PackageManager::Pacman:
        return *packages.pacman
                   ? sudo + "pacman -S --needed --noconfirm " + packages.pacman
                   : "";
    case PackageManager::Winget:
        return *packages.winget ? std::string("winget install --id ") +
                                      packages.winget + " -e"
                                : "";
    case PackageManager::None:
        break;
    }
    return "";
}

// Python tools: pipx when available (works where pip refuses to touch a
// system Python), otherwise a user pip install.
std::string pythonInstall(const std::string& package, const Finder& find)
{
    if(find("pipx"))
        return "pipx install " + package;
    return std::string(find("python3") ? "python3" : "python") +
           " -m pip install --user " + package;
}
} // namespace

std::vector<Dependency> required(const Features& f, Platform platform,
                                 PackageManager pm, const Finder& find)
{
    std::vector<Dependency> deps;
    auto add = [&](std::string name, std::string feature,
                   std::vector<std::string> programs, std::string install,
                   bool optional = false)
    {
        deps.push_back({std::move(name), std::move(feature),
                        std::move(programs), std::move(install), optional});
    };

    // Prerequisites first.
    const bool needsNpm =
        f.htmlLsp || f.cssLsp || f.jsonLsp || f.tsLsp || f.pythonLsp;
    if(needsNpm)
        add("Node.js (npm)", "npm based language servers", {"npm"},
            systemInstall(pm, {.brew = "node",
                               .apt = "nodejs npm",
                               .dnf = "nodejs npm",
                               .pacman = "nodejs npm",
                               .winget = "OpenJS.NodeJS"}));
    if(f.csharpLsp)
        add(".NET SDK (dotnet)", "C# LSP", {"dotnet"},
            pm == PackageManager::Brew
                ? "brew install --cask dotnet-sdk"
                : systemInstall(pm, {.apt = "dotnet-sdk-8.0",
                                     .dnf = "dotnet-sdk-8.0",
                                     .pacman = "dotnet-sdk",
                                     .winget = "Microsoft.DotNet.SDK.8"}));

    if(f.clangdLsp)
        add("clangd", "C/C++ LSP", {"clangd"},
            systemInstall(pm, {.brew = "llvm",
                               .apt = "clangd",
                               .dnf = "clang-tools-extra",
                               .pacman = "clang",
                               .winget = "LLVM.LLVM"}));
    if(f.pythonLsp)
        add("pyright", "Python LSP", {"pyright-langserver", "pylsp"},
            "npm install -g pyright");
    if(f.robotLsp)
        add("robotframework-lsp", "Robot Framework LSP",
            {"robotframework-lsp", "robotframework_ls"},
            pythonInstall("robotframework-lsp", find));
    if(f.htmlLsp || f.cssLsp || f.jsonLsp)
    {
        std::vector<std::string> programs;
        std::string features;
        auto use = [&](bool on, const char* program, const char* label)
        {
            if(!on)
                return;
            programs.push_back(program);
            features += features.empty() ? label : std::string("/") + label;
        };
        use(f.htmlLsp, "vscode-html-language-server", "HTML");
        use(f.cssLsp, "vscode-css-language-server", "CSS");
        use(f.jsonLsp, "vscode-json-language-server", "JSON");
        add("vscode-langservers-extracted", features + " LSP", programs,
            "npm install -g vscode-langservers-extracted");
    }
    if(f.tsLsp)
        add("typescript-language-server", "TypeScript/JavaScript LSP",
            {"typescript-language-server"},
            "npm install -g typescript-language-server typescript");
    if(f.csharpLsp)
        add("csharp-ls", "C# LSP", {"csharp-ls"},
            "dotnet tool install --global csharp-ls");

    if(f.gitTools)
        add("git", "git views", {"git"},
            systemInstall(pm, {.brew = "git",
                               .apt = "git",
                               .dnf = "git",
                               .pacman = "git",
                               .winget = "Git.Git"}));
    if(f.searchTools)
    {
        add("ripgrep", "project grep (Ctrl-A)", {"rg"},
            systemInstall(pm, {.brew = "ripgrep",
                               .apt = "ripgrep",
                               .dnf = "ripgrep",
                               .pacman = "ripgrep",
                               .winget = "BurntSushi.ripgrep.MSVC"}),
            true);
        add("fzf", "fuzzy search", {"fzf"},
            systemInstall(pm, {.brew = "fzf",
                               .apt = "fzf",
                               .dnf = "fzf",
                               .pacman = "fzf",
                               .winget = "junegunn.fzf"}),
            true);
    }
    if(f.formatters)
    {
        add("clang-format", "C/C++/C# formatting", {"clang-format"},
            systemInstall(pm, {.brew = "clang-format",
                               .apt = "clang-format",
                               .dnf = "clang-tools-extra",
                               .pacman = "clang",
                               .winget = "LLVM.LLVM"}),
            true);
        add("ruff", "Python formatting", {"ruff"},
            pm == PackageManager::Brew ? "brew install ruff"
            : pm == PackageManager::Winget
                ? "winget install --id astral-sh.ruff -e"
                : pythonInstall("ruff", find),
            true);
    }
    if(f.systemClipboard && platform == Platform::Linux)
    {
        const bool wayland = std::getenv("WAYLAND_DISPLAY") != nullptr;
        add(wayland ? "wl-clipboard" : "xclip", "system clipboard",
            {"wl-copy", "xclip", "xsel"},
            systemInstall(pm, {.apt = wayland ? "wl-clipboard" : "xclip",
                               .dnf = wayland ? "wl-clipboard" : "xclip",
                               .pacman = wayland ? "wl-clipboard" : "xclip"}),
            true);
    }
    return deps;
}

std::vector<Dependency> missing(const std::vector<Dependency>& deps,
                                const Finder& find)
{
    std::vector<Dependency> out;
    for(const Dependency& dep : deps)
    {
        bool found = false;
        for(const std::string& program : dep.programs)
            found = found || find(program);
        if(!found)
            out.push_back(dep);
    }
    return out;
}

Platform currentPlatform()
{
#if defined(_WIN32)
    return Platform::Windows;
#elif defined(__APPLE__)
    return Platform::MacOS;
#else
    return Platform::Linux;
#endif
}

PackageManager detectPackageManager(Platform platform, const Finder& find)
{
    switch(platform)
    {
    case Platform::MacOS:
        return find("brew") ? PackageManager::Brew : PackageManager::None;
    case Platform::Windows:
        return find("winget") ? PackageManager::Winget : PackageManager::None;
    case Platform::Linux:
        if(find("apt-get"))
            return PackageManager::Apt;
        if(find("dnf"))
            return PackageManager::Dnf;
        if(find("pacman"))
            return PackageManager::Pacman;
        // Linuxbrew.
        return find("brew") ? PackageManager::Brew : PackageManager::None;
    }
    return PackageManager::None;
}

bool programExists(const std::string& program)
{
    std::error_code ec;
    const char* pathEnv = std::getenv("PATH");
    std::vector<std::string> dirs;
    {
        std::string path = pathEnv ? pathEnv : "";
#ifdef _WIN32
        const char separator = ';';
#else
        const char separator = ':';
#endif
        std::stringstream in(path);
        std::string dir;
        while(std::getline(in, dir, separator))
            if(!dir.empty())
                dirs.push_back(dir);
    }
    // `dotnet tool install -g` puts tools here; uvim looks there too.
    if(const char* home = std::getenv(
#ifdef _WIN32
           "USERPROFILE"
#else
           "HOME"
#endif
           ))
        dirs.push_back((fs::path(home) / ".dotnet" / "tools").string());

    std::vector<std::string> names = {program};
#ifdef _WIN32
    for(const char* ext : {".exe", ".cmd", ".bat"})
        names.push_back(program + ext);
#endif
    for(const std::string& dir : dirs)
    {
        for(const std::string& name : names)
        {
            const fs::path candidate = fs::path(dir) / name;
            if(fs::is_regular_file(candidate, ec))
                return true;
        }
    }
    return false;
}
} // namespace uvim_config_deps
