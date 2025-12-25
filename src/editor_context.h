#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

// Forward declarations
class LspClient;

// Editor modes
enum class Mode
{
    NORMAL,
    INSERT,
    VISUAL,
    VISUAL_LINE,
    VISUAL_BLOCK,
    COMMAND,
    SEARCH_FORWARD,
    SEARCH_BACKWARD,
    FILE_BROWSER,
    FUZZY_FIND,
    BUFFER_BROWSER,
    GREP_SEARCH,
    OP_PENDING
};

// Buffer represents a single file/document
struct Buffer
{
    std::vector<std::string> lines;
    std::string filename;
    bool dirty = false;
    int cursorX = 0;
    int cursorY = 0;
    int wantedX = 0; // For vertical movement column memory
    int offsetX = 0; // Horizontal scroll
    int offsetY = 0; // Vertical scroll

    // Visual mode state
    int visualStartX = 0;
    int visualStartY = 0;
    int visualEndX = 0;
    int visualEndY = 0;

    // Search state per buffer
    std::string lastSearchQuery;
    bool lastSearchForward = true;

    // Undo/redo state
    struct EditState
    {
        std::vector<std::string> lines;
        int cursorX;
        int cursorY;
    };
    std::vector<EditState> undoStack;
    int undoIndex = -1;
    int savedUndoIndex = -1; // Index when file was last saved

    Buffer()
    {
        lines.push_back("");
    }
};

// Jump location for Ctrl-O/Ctrl-I navigation
struct JumpLocation
{
    int bufferIndex;
    int cursorX;
    int cursorY;
    int offsetX;
    int offsetY;
};

// Search match position
struct SearchMatch
{
    int line;
    int startCol;
    int endCol;
};

// File entry for file browser
struct FileEntry
{
    std::string name;
    bool isDirectory;
    size_t size;
    time_t modTime;
};

// Fuzzy find match
struct FuzzyMatch
{
    std::string path;
    int score;
    std::vector<size_t> matchPositions;
};

// Grep match
struct GrepMatch
{
    std::string filepath;
    int lineNumber;
    std::string lineContent;
    int matchStart;
    int matchEnd;
};

// LSP Completion item
struct CompletionItem
{
    std::string label;
    std::string insertText;
    std::string detail;
    std::string documentation;
    int kind;
};

// EditorContext - shared state for all editor components
struct EditorContext
{
    // Buffers
    std::vector<std::unique_ptr<Buffer>> buffers;
    int currentBufferIndex = 0;
    Buffer* currentBuffer = nullptr;

    // Convenience pointers to current buffer data
    std::vector<std::string>* lines = nullptr;
    std::string* filename = nullptr;
    bool* dirty = nullptr;
    int* cursorX = nullptr;
    int* cursorY = nullptr;
    int* wantedX = nullptr;
    int* offsetX = nullptr;
    int* offsetY = nullptr;

    // Screen dimensions
    int screenRows = 24;
    int screenCols = 80;

    // Current mode
    Mode currentMode = Mode::NORMAL;

    // Status message
    std::string statusMessage;

    // Yank buffer (clipboard)
    std::string yankBuffer;

    // Search state
    std::string searchQuery;
    bool searchForward = true;
    std::vector<SearchMatch> searchMatches;
    int currentMatchIndex = -1;

    // Jump stacks for Ctrl-O/Ctrl-I
    std::vector<JumpLocation> jumpBackStack;
    std::vector<JumpLocation> jumpForwardStack;

    // Command buffer for multi-key commands
    std::string commandBuffer;
    int repeatCount = 0;

    // Operator-pending state
    char pendingOperator = 0;
    bool pendingAwaitingObject = false;
    char pendingObjectType = 0;
    int pendingCount = 0;

    // Find char state (f/F/t/T)
    char lastFindChar = 0;
    bool lastFindForward = true;

    // File browser state
    std::string currentDirectory;
    std::vector<FileEntry> fileList;
    int fileBrowserIndex = 0;
    int fileBrowserOffset = 0;
    bool showHiddenFiles = false;

    // Fuzzy find state
    std::string fuzzyQuery;
    std::vector<std::string> projectFiles;
    std::vector<FuzzyMatch> fuzzyMatches;
    int fuzzySelectedIndex = 0;
    int fuzzyScrollOffset = 0;

    // Buffer browser state
    std::string bufferQuery;
    std::vector<FuzzyMatch> bufferMatches;
    int bufferSelectedIndex = 0;

    // Grep search state
    std::string grepQuery;
    std::vector<GrepMatch> grepMatches;
    int grepSelectedIndex = 0;
    int grepScrollOffset = 0;
    bool grepSearching = false;

    // LSP completion state
    bool completionActive = false;
    std::vector<CompletionItem> completionItems;
    std::vector<CompletionItem> filteredCompletions;
    int completionIndex = 0;
    int completionStartX = 0;
    std::string completionPrefix;

    // LSP client
#ifdef UVIM_ENABLE_CLANGD_LSP
    std::unique_ptr<LspClient> lspClient;
#endif
    bool clangdLspEnabled = false;
    std::string clangdLspCompileCommandsDir;
    std::string clangdLspPath;
    std::string clangdLspQueryDriverAllowList;

    // Rendering state
    bool needsFullRedraw = true;
    int lastDrawnOffsetY = -1;

    // Timing for double-ESC detection
    std::chrono::steady_clock::time_point lastEscTime;
    static constexpr int DOUBLE_ESC_TIMEOUT_MS = 300;

    // Visual block insert state
    std::string visualBlockInsertText;
    int visualBlockInsertStartX = 0;

    // Helper methods
    void updateCurrentBufferPointers()
    {
        if(currentBuffer)
        {
            lines = &currentBuffer->lines;
            filename = &currentBuffer->filename;
            dirty = &currentBuffer->dirty;
            cursorX = &currentBuffer->cursorX;
            cursorY = &currentBuffer->cursorY;
            wantedX = &currentBuffer->wantedX;
            offsetX = &currentBuffer->offsetX;
            offsetY = &currentBuffer->offsetY;
        }
    }

    bool isWordChar(char c) const
    {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    }
};
