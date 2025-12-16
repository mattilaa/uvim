#pragma once
#include <chrono>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Editor
{
public:
    enum Mode
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
        OP_PENDING,
    };

    Editor();
    ~Editor();

    void run();
    void openFile(const std::string& filename);

    // Buffer structure to hold file data
    struct Buffer
    {
        std::vector<std::string> lines;
        std::string filename;
        bool dirty = false;

        // Cursor position per buffer
        int cursorX = 0;
        int cursorY = 0;
        int wantedX = 0;

        // View position per buffer
        int offsetX = 0;
        int offsetY = 0;

        // Marks per buffer
        std::unordered_map<char, std::pair<int, int>> marks;

        // Undo/redo stack per buffer
        struct EditState
        {
            std::vector<std::string> lines;
            int cursorX, cursorY;
        };
        std::vector<EditState> undoStack;
        int undoIndex = -1;
        int savedUndoIndex = -1; // Track which undo state was last saved

        // Search state per buffer
        std::string lastSearchQuery;
        bool lastSearchForward = true;

        // Visual mode state per buffer
        int visualStartX = 0;
        int visualStartY = 0;
        int visualEndX = 0;
        int visualEndY = 0;

        // Visual block mode state per buffer
        int visualBlockStartX = 0;
        int visualBlockStartY = 0;
        int visualBlockEndX = 0;
        int visualBlockEndY = 0;
        std::string
            visualBlockInsertText; // Text to insert in visual block mode

        Buffer()
        {
            lines.push_back("");
        }
    };

    bool visualBlockChanging = false;
    // Buffer management
    std::vector<std::unique_ptr<Buffer>> buffers;
    int currentBufferIndex = -1;
    Buffer* currentBuffer = nullptr;

    // References to current buffer's data (for easier access)
    std::vector<std::string>* lines;
    std::string* filename;
    bool* dirty;
    int* cursorX;
    int* cursorY;
    int* wantedX;
    int* offsetX;
    int* offsetY;

    // File browser
    struct FileEntry
    {
        std::string name;
        std::string path;
        bool isDirectory;
        size_t size;
        time_t modTime;
    };
    std::vector<FileEntry> fileList;
    std::string currentDirectory;
    std::string previousFile;
    int browserCursor = 0;
    int browserOffset = 0;
    bool showHidden = false;

    // Fuzzy finder
    struct FuzzyMatch
    {
        FileEntry file;
        int score;
        std::vector<int> matchPositions; // Character positions that matched
    };
    std::vector<FuzzyMatch> fuzzyMatches;
    std::vector<FileEntry> allProjectFiles; // All files in project
    std::string fuzzyQuery;
    int fuzzyCursor = 0;
    int fuzzyOffset = 0;
    bool fuzzyInitialized = false;

    // Buffer browser (fzf-style)
    struct BufferMatch
    {
        int bufferIndex = -1;
        int score = 0;
        std::string display;
        std::vector<int> matchPositions; // positions within display string
    };
    std::vector<BufferMatch> bufferMatches;
    std::string bufferQuery;
    int bufferCursor = 0;
    int bufferOffset = 0;

    // Grep search
    struct GrepMatch
    {
        std::string filename;
        std::string filepath;
        int lineNumber;
        std::string lineContent;
        std::vector<std::pair<int, int>>
            highlightRanges; // Start and end positions for highlighting
    };
    std::vector<GrepMatch> grepMatches;
    std::string grepQuery;
    int grepCursor = 0;
    int grepOffset = 0;
    bool grepSearching = false;
    bool grepCaseSensitive = false;

    // Screen
    int screenRows;
    int screenCols;

    // Operator-pending state
    char pendingOperator = 0;           // 'd', 'c', 'y', etc.
    bool pendingAwaitingObject = false; // After 'd' then 'i'/'a'
    char pendingObjectType = 0; // 'i' or 'a' when awaiting a text object
    int pendingCount = 0;       // support counts like 2dw if present

    // Optimization for drawing
    bool needsFullRedraw = true;
    int lastCursorScreenY = -1;
    int lastCursorScreenX = -1;
    std::vector<std::string> screenBuffer;
    std::string lastStatusBar;
    std::string lastMessageBar;

    // Modes
    Mode currentMode = NORMAL;
    std::string commandBuffer;
    std::string statusMessage;

    // Search (global state)
    std::string searchQuery;
    bool searchForward = true;
    struct SearchMatch
    {
        int row;
        int col;
        int len;
    };
    std::vector<SearchMatch> searchMatches;
    int currentMatchIndex = -1;
    int savedCursorX = 0;
    int savedCursorY = 0;

    // Double ESC detection
    std::chrono::steady_clock::time_point lastEscTime;
    static constexpr int DOUBLE_ESC_TIMEOUT_MS = 300;

    // Vim-like features (global)
    std::string yankBuffer;
    int repeatCount = 0;
    char lastFindChar = 0;
    bool lastFindForward = true;

    // Drawing - Original functions
    void draw();
    void drawRows();
    void drawStatusBar();
    void drawMessageBar();
    void drawFileBrowser();
    void refreshScreen();
    void drawIncrementalUpdate();
    void drawFullScreen();
    void updateCursorPosition();

    // Drawing - NEW OPTIMIZATION FUNCTIONS
    void drawScrollUpdate(int scrollDelta);
    void drawStatusBarQuick();
    void drawMessageBarQuick(); // Add this to redraw message bar

    // Mode handlers
    void handleNormalMode(int c);
    void handleInsertMode(int c);
    void handleVisualMode(int c);
    void handleCommandMode(int c);
    void handleSearchMode(int c);
    void handleFileBrowserMode(int c);
    void handleFuzzyFindMode(int c);
    void handleBufferBrowserMode(int c);
    void handleGrepSearchMode(int c);
    void handleKeypress();

    // Buffer management functions
    void createNewBuffer();
    void switchToBuffer(int index);
    void closeCurrentBuffer();
    void nextBuffer();
    void previousBuffer();
    void listBuffers();
    int findBufferByFilename(const std::string& filename);
    void updateCurrentBufferPointers();
    void saveBufferState();
    void restoreBufferState();
    bool searchDefinitionInBuffer(Buffer* buf, const std::string& symbol,
                                  int& outY, int& outX);

    // Operator-pending / text-object support
    void enterOperatorPending(char op);
    void handleOperatorPendingMode(int c);
    bool getTextObjectRange(char objChar, bool around, int& outStartY,
                            int& outStartX, int& outEndY, int& outEndX);
    void applyOperatorToRange(char op, int startY, int startX, int endY,
                              int endX);
    void deleteRange(int startY, int startX, int endY, int endX);
    void yankRange(int startY, int startX, int endY, int endX);

    // File browser functions
    void openFileBrowser(const std::string& path = ".");
    void loadDirectory(const std::string& path);
    void sortFileList();
    void navigateTo(const FileEntry& entry);
    void toggleHidden();
    std::string formatFileSize(size_t size);
    std::string formatFileTime(time_t time);
    std::string getFilePermissions(const std::string& path);
    std::string getRelativePath(const std::string& path);
    void createNewFile();
    void deleteCurrentFile();

    // Jump between header and source
    void jumpToAlternateFile();
    std::string findAlternateFile(const std::string& currentFile);
    bool fileExists(const std::string& path);
    std::string getSymbolUnderCursor();

    // Fuzzy finder functions
    void initializeFuzzyFind();
    void collectProjectFiles(const std::string& dir, int depth = 0);
    int fuzzyScore(const std::string& needle, const std::string& haystack,
                   std::vector<int>& matchPositions);
    void updateFuzzyMatches();
    void drawFuzzyFind();
    void selectFuzzyMatch();

    // Buffer browser functions
    void initializeBufferBrowser();
    void updateBufferMatches();
    void drawBufferBrowser();
    void selectBufferMatch();

    // Grep search functions
    void initializeGrepSearch();
    void performGrepSearch();
    void searchFileContent(const std::string& filepath);
    bool isTextFile(const std::string& filepath);
    bool isBinaryFile(const std::string& filepath);
    void drawGrepSearch();
    void selectGrepMatch();
    std::string trimString(const std::string& str);
    void highlightGrepMatches(const std::string& line, const std::string& query,
                              std::vector<std::pair<int, int>>& ranges);
    void goToDefinition();

    // Movement commands
    void moveCursor(int key);
    void moveLeft(int count = 1);
    void moveRight(int count = 1);
    void moveUp(int count = 1);
    void moveDown(int count = 1);
    void moveWordForward();
    void moveWordBackward();
    void moveToEndOfWord();
    void moveToLineStart();
    void moveToLineEnd();
    void moveToFirstLine();
    void moveToLastLine();
    void moveToLine(int line);
    void scrollHalfPageDown(bool visual);
    void scrollHalfPageUp(bool visual);
    void moveToMatchingBracket();
    void findCharForward(char c);
    void findCharBackward(char c);

    // Search commands
    void startSearchForward();
    void startSearchBackward();
    void performSearch();
    void findAllMatches();
    void searchNext();
    void searchPrevious();
    void clearSearch();
    void highlightMatch(int index);
    bool isInSearchMatch(int row, int col);
    void jumpToMatch(int index);
    void cancelSearch();

    // Editing commands
    void insertChar(char c);
    void insertNewline();
    void insertNewlineAbove();
    void insertNewlineBelow();
    void deleteChar();
    void deleteCharForward();
    void deleteLine();
    void deleteToLineEnd();
    void deleteWord();
    void deleteSelection();
    void changeWord();
    void changeLine();
    void changeToLineEnd();
    void replaceChar(char c);

    // Copy/Paste
    void yankLine();
    void yankToLineEnd();
    void yankSelection();
    void yankWord();
    void pasteAfter();
    void pasteBefore();

    // Visual mode
    void startVisualMode();
    void startVisualLineMode();
    void startVisualBlockMode();
    void updateVisualSelection();
    void updateVisualBlockSelection();
    bool isInSelection(int row, int col);
    bool isInVisualBlock(int row, int col);
    void getSelectionBounds(int& startY, int& startX, int& endY, int& endX);
    void getVisualBlockBounds(int& startY, int& startX, int& endY, int& endX);
    void deleteVisualBlock();
    void changeVisualBlock();
    void yankVisualBlock();
    void handleVisualBlockMode(int c);
    void applyVisualBlockInsert();

    // File operations
    void saveFile();
    void executeCommand(const std::string& cmd);
    void forceQuit();
    std::string getAlternateFilePath();

    // Utilities
    void setStatusMessage(const std::string& msg);
    void setMode(Mode mode);
    std::string getModeString() const;
    bool isWordChar(char c) const;
    void adjustViewport();
    void centerScreen();
    int getLineIndent(int line);
    void indentLine(int line, int spaces);
    void autoIndentLine(int line);
    void autoIndentRange(int startLine, int endLine);
    std::string toLowerCase(const std::string& str);

    // Syntax highlighting
    enum TokenType
    {
        TOKEN_NORMAL,
        TOKEN_KEYWORD,
        TOKEN_TYPE,
        TOKEN_STRING,
        TOKEN_CHAR,
        TOKEN_COMMENT,
        TOKEN_PREPROCESSOR,
        TOKEN_NUMBER,
        TOKEN_OPERATOR,
        TOKEN_FUNCTION
    };

    struct Token
    {
        TokenType type;
        int start;
        int length;
    };

    bool isCppFile() const;
    std::vector<Token> tokenizeLine(const std::string& line,
                                    bool& inBlockComment);
    std::string getColorCode(TokenType type) const;
    void renderLineWithSyntax(std::string& output, const std::string& line,
                              int start, int len, int fileRow);

    // Undo/Redo (now per-buffer, accessed through currentBuffer)
    void saveState();
    void undo();
    void redo();
};
