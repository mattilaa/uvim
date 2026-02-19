#pragma once
#include <optional>
#include <string>
#include <string_view>
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

    struct Diagnostic
    {
        int line = 0;         // 0-based
        int character = 0;    // 0-based (UTF-16 code units)
        int endLine = 0;      // 0-based
        int endCharacter = 0; // 0-based (UTF-16 code units)
        int severity = 0;     // 1=Error, 2=Warning, 3=Info, 4=Hint
        std::string message;
        std::string source;
        std::string codeJson;
        std::string dataJson;
    };

    struct TextEdit
    {
        int startLine = 0;
        int startCharacter = 0;
        int endLine = 0;
        int endCharacter = 0;
        std::string newText;
    };

    struct CodeAction
    {
        std::string title;
        std::vector<TextEdit> edits;
        std::string command;
        std::vector<std::string> commandArgsJson;
    };

    struct SemanticToken
    {
        int line = 0;
        int character = 0; // UTF-16 code units
        int length = 0;    // UTF-16 code units
        std::string tokenType;
        int modifiers = 0;
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
    bool startServer(const std::string& serverPath, const std::string& rootDir,
                     const std::vector<std::string>& args = {});

    void stop();
    bool running() const;
    std::string serverName() const;
    std::string serverVersion() const;

    // Document sync
    void didOpen(const std::string& filePath, const std::string& languageId,
                 const std::string& text);
    void didChange(const std::string& filePath, const std::string& text);
    void didChange(const std::string& filePath, const std::string& text,
                   const std::string& languageId);
    void didSave(const std::string& filePath);

    // Queries
    std::optional<Location> definition(const std::string& filePath, int line,
                                       int characterUtf8ByteOffset,
                                       std::string_view lineText = {});
    std::optional<Location> declaration(const std::string& filePath, int line,
                                        int characterUtf8ByteOffset,
                                        std::string_view lineText = {});
    std::optional<Location> typeDefinition(const std::string& filePath, int line,
                                           int characterUtf8ByteOffset,
                                           std::string_view lineText = {});

    // Find all references to symbol at position
    // Returns list of locations where the symbol is used
    std::vector<Location> references(const std::string& filePath, int line,
                                     int characterUtf8ByteOffset,
                                     bool includeDeclaration = true);

    struct CompletionItem
    {
        std::string label;
        std::string insertText; // may contain snippet syntax
        bool isSnippet = false; // insertTextFormat == 2
        int kind = 0;
        std::string detail;
        std::string labelDetail;
        std::string labelDescription;
        std::string documentation;
    };

    // Completion items at a given cursor position.
    // characterUtf8ByteOffset is a UTF-8 byte offset within the line.
    std::vector<CompletionItem>
    completion(const std::string& filePath, int line,
               int characterUtf8ByteOffset, std::string_view lineText = {},
               int triggerKind = 1, char triggerCharacter = '\0');

    // Latest diagnostics for a file path (from publishDiagnostics).
    std::vector<Diagnostic> diagnostics(const std::string& filePath) const;
    void clearDiagnostics(const std::string& filePath);
    size_t diagnosticsRevision(const std::string& filePath) const;

    std::vector<CodeAction>
    codeActions(const std::string& filePath, int line,
                std::string_view lineText,
                const std::vector<Diagnostic>& diagnostics);
    std::vector<TextEdit>
    executeCommand(const std::string& command,
                   const std::vector<std::string>& argumentsJson,
                   const std::string& filePath);

    // Document formatting (textDocument/formatting)
    std::vector<TextEdit> formatting(const std::string& filePath,
                                     int tabSize = 4, bool insertSpaces = true);

    // Semantic tokens (textDocument/semanticTokens/full)
    bool requestSemanticTokens(const std::string& filePath);
    std::vector<SemanticToken>
    semanticTokens(const std::string& filePath) const;
    size_t semanticTokensRevision(const std::string& filePath) const;
    bool semanticTokenHasModifier(int modifiers, std::string_view name) const;
    void clearSemanticTokens(const std::string& filePath);

private:
    struct Impl;
    Impl* impl;
};
