#pragma once

#include <string>
#include <vector>

// Finding and starting a C# language server: csharp-ls (default,
// `dotnet tool install -g csharp-ls`) or OmniSharp.
namespace csharp_lsp
{
// The server to run: `configured` when it resolves to an executable;
// otherwise csharp-ls (on PATH or in ~/.dotnet/tools) and then OmniSharp.
// Empty when none is installed.
std::string resolveServer(const std::string& configured);

// Arguments for `server`: OmniSharp needs "-lsp" to speak LSP; extra
// user arguments follow.
std::vector<std::string> serverArgs(
    const std::string& server,
    std::vector<std::string> extra);

// Directory of the nearest *.sln, else *.csproj, at or above the
// directory of `file`; empty when there is none.
std::string projectRoot(const std::string& file);
} // namespace csharp_lsp
