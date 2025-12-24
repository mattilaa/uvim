#pragma once
#include <optional>
#include <string>

// Minimal clangd LSP client (stdio JSON-RPC) used by uvim.
// Designed for one-shot queries (e.g. gd) with full-text sync.

class LspClient
{
public:
    struct Location
    {
        std::string path; // local filesystem path
        int line = 0;     // 0-based
        int character = 0; // 0-based (UTF-16 code units)
    };

    LspClient();
    ~LspClient();

    // Start clangd. If compileCommandsDir is non-empty, clangd is started with
    // --compile-commands-dir=<dir>.
    bool start(const std::string& clangdPath,
               const std::string& rootDir,
               const std::string& compileCommandsDir = "");

    void stop();
    bool running() const;

    // Document sync
    void didOpen(const std::string& filePath, const std::string& languageId,
                 const std::string& text);
    void didChange(const std::string& filePath, const std::string& text);
    void didSave(const std::string& filePath);

    // Queries
    std::optional<Location> definition(const std::string& filePath, int line,
                                       int characterUtf8ByteOffset);

private:
    struct Impl;
    Impl* impl;
};
