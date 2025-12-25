#pragma once

#include "buffer_manager.h"
#include "code_formatter.h"
#include "cursor_movement.h"
#include "editor_context.h"
#include "file_io.h"
#include "lsp_integration.h"
#include "operator_pending.h"
#include "renderer.h"
#include "search_engine.h"
#include "syntax_highlighter.h"
#include "text_operations.h"
#include "undo_manager.h"
#include "visual_mode.h"

#include <memory>
#include <string>

class Editor
{
public:
    Editor();
    ~Editor();

    // Main loop
    void run();

    // Public interface
    void openFile(const std::string& fname);
    void enableClangdLsp(bool enable, const std::string& compileCommandsDir,
                         const std::string& clangdPath,
                         const std::string& queryDriverAllowList);

private:
    // Shared context - must be first so it's initialized before components
    EditorContext ctx;

    // Component objects (order matters for initialization)
    BufferManager bufferMgr;
    CursorMovement cursor;
    TextOperations textOps;
    UndoManager undoMgr;
    SearchEngine search;
    FileIO fileIO;
    VisualMode visualMode;
    CodeFormatter formatter;
    SyntaxHighlighter syntax;
    Renderer renderer;
    OperatorPending opPending;
    LspIntegration lsp;

    // Drawing
    void draw();

    // Mode management
    void setMode(Mode mode);
    std::string getModeString() const;
    void setStatusMessage(const std::string& msg);

    // Keypress dispatch
    void handleKeypress();

    // Mode handlers
    void handleNormalMode(int c);
    void handleInsertMode(int c);
    void handleVisualMode(int c);
    void handleVisualBlockMode(int c);
    void handleCommandMode(int c);
    void handleSearchMode(int c);
    void handleFileBrowserMode(int c);
    void handleFuzzyFindMode(int c);
    void handleBufferBrowserMode(int c);
    void handleGrepSearchMode(int c);
    void handleOperatorPendingMode(int c);

    // Normal mode helpers
    void handleLeaderKey(int c);
    void handleGCommand(int c);
    void handleZCommand(int c);

    // File browser
    void openFileBrowser(const std::string& path = "");
    void loadDirectory(const std::string& path);
    void navigateFileBrowser(int direction);
    void selectFileBrowserEntry();
    std::string formatFileSize(size_t size);

    // Fuzzy finder
    void initializeFuzzyFind();
    void collectProjectFiles(const std::string& dir, int depth = 0);
    void updateFuzzyMatches();
    int fuzzyScore(const std::string& needle, const std::string& haystack,
                   std::vector<size_t>& matchPositions);
    void selectFuzzyMatch();

    // Buffer browser
    void initializeBufferBrowser();
    void updateBufferMatches();
    void selectBufferMatch();

    // Grep search
    void initializeGrepSearch();
    void performGrepSearch();
    void searchFileContent(const std::string& filepath);
    void selectGrepMatch();
    bool isTextFile(const std::string& filepath);
    bool isBinaryFile(const std::string& filepath);

    // Command execution
    void executeCommand(const std::string& cmd);

    // Utility
    void forceQuit();
    std::string trimString(const std::string& str);
};
