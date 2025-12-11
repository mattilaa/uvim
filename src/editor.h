#pragma once
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
        COMMAND,
        SEARCH_FORWARD,
        SEARCH_BACKWARD,
        FILE_BROWSER,
        FUZZY_FIND,
        GREP_SEARCH,
    };

    Editor();
    ~Editor();

    void run();
    void openFile(const std::string& filename);

private:
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

        // Search state per buffer
        std::string lastSearchQuery;
        bool lastSearchForward = true;

        // Visual mode state per buffer
        int visualStartX = 0;
        int visualStartY = 0;
        int visualEndX = 0;
        int visualEndY = 0;

        Buffer()
        {
            lines.push_back("");
        }
    };

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

    // Fuzzy finder functions
    void initializeFuzzyFind();
    void collectProjectFiles(const std::string& dir, int depth = 0);
    int fuzzyScore(const std::string& needle, const std::string& haystack,
                   std::vector<int>& matchPositions);
    void updateFuzzyMatches();
    void drawFuzzyFind();
    void selectFuzzyMatch();

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
    void scrollHalfPageDown();
    void scrollHalfPageUp();
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
    void updateVisualSelection();
    bool isInSelection(int row, int col);
    void getSelectionBounds(int& startY, int& startX, int& endY, int& endX);

    // File operations
    void saveFile();
    void executeCommand(const std::string& cmd);

    // Utilities
    void setStatusMessage(const std::string& msg);
    void setMode(Mode mode);
    std::string getModeString() const;
    bool isWordChar(char c) const;
    void adjustViewport();
    void centerScreen();
    int getLineIndent(int line);
    void indentLine(int line, int spaces);
    std::string toLowerCase(const std::string& str);

    // Undo/Redo (now per-buffer, accessed through currentBuffer)
    void saveState();
    void undo();
    void redo();
};
