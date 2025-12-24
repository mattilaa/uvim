#pragma once
#include <optional>
#include <string>
#include <vector>

// Minimal clangd LSP client (stdio JSON-RPC) used by uvim.
// Designed for one-shot queries (e.g. gd) with full-text sync.

class LspClient
{
public:
    struct Location
    {
        std::string path;  // local filesystem path
        int line = 0;      // 0-based
        int character = 0; // 0-based (UTF-16 code units)
    };

    struct CompletionItem
    {
        std::string label;
        std::string insertText; // may contain snippet syntax
        bool isSnippet = false; // insertTextFormat == 2

        int kind = 0;            // LSP CompletionItemKind (if provided)
        std::string detail;      // often signature or type
        std::string labelDetail; // LSP 3.17 labelDetails.detail (often "(...)")
        std::string labelDescription; // LSP 3.17 labelDetails.description
                                      // (often return/type)
        std::string filterText;       // optional hint for filtering
    };

    LspClient();
    ~LspClient();

    // Start clangd. If compileCommandsDir is non-empty, clangd is started with
    // --compile-commands-dir=<dir>.
    // If queryDriverAllowList is non-empty, clangd is started with
    // --query-driver=<glob1,glob2,...> so it can discover system include paths
    // from the compiler in compile_commands.json.
    bool start(const std::string& clangdPath, const std::string& rootDir,
               const std::string& compileCommandsDir = "",
               const std::string& queryDriverAllowList = "");

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

    // Completion items at a given cursor position.
    // characterUtf8ByteOffset is a UTF-8 byte offset within the line.
    std::vector<CompletionItem> completion(const std::string& filePath,
                                           int line,
                                           int characterUtf8ByteOffset,
                                           char triggerChar = 0);

private:
    struct Impl;
    Impl* impl;
};
