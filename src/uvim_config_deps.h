#pragma once

#include <functional>
#include <string>
#include <vector>

// Runtime dependencies of the features chosen in uvim-config: the external
// programs they use (language servers, ripgrep, git, formatters, ...), how
// to install them on this system, and which are missing.
namespace uvim_config_deps
{
struct Features
{
    bool clangdLsp = false;
    bool robotLsp = false;
    bool pythonLsp = false;
    bool htmlLsp = false;
    bool cssLsp = false;
    bool jsonLsp = false;
    bool tsLsp = false;
    bool csharpLsp = false;
    bool gitTools = false;
    bool searchTools = false;
    bool formatters = false;
    bool systemClipboard = false;
};

enum class Platform
{
    MacOS,
    Linux,
    Windows,
};

enum class PackageManager
{
    None,
    Brew,
    Apt,
    Dnf,
    Pacman,
    Winget,
};

struct Dependency
{
    std::string name;    // what gets installed, e.g. "csharp-ls"
    std::string feature; // why: "C# LSP"
    // Any of these programs on PATH satisfies the dependency.
    std::vector<std::string> programs;
    // Shell command installing it; empty when there is no known way.
    std::string install;
    bool optional = false; // the feature works without it (degraded)
};

// True when `program` is on PATH (Windows: also with PATHEXT extensions).
using Finder = std::function<bool(const std::string& program)>;

// The dependencies of `features`, prerequisites first (npm before the npm
// based servers, dotnet before csharp-ls).
std::vector<Dependency> required(const Features& features, Platform platform,
                                 PackageManager packageManager,
                                 const Finder& find);

// The entries of `deps` none of whose programs is found.
std::vector<Dependency> missing(const std::vector<Dependency>& deps,
                                const Finder& find);

Platform currentPlatform();
PackageManager detectPackageManager(Platform platform, const Finder& find);
// PATH lookup; csharp-ls is also found in ~/.dotnet/tools.
bool programExists(const std::string& program);

// A runtime-only installation or an older SDK cannot install current csharp-ls.
bool hasSupportedDotnetSdk(const std::string& sdkList);
bool dotnetSdkAvailable();
} // namespace uvim_config_deps
