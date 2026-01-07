#pragma once
#include "buffer.h"
#include "completion_entry.h"
#include "file_entry.h"
#include "jump_location.h"
#include "mode.h"
#include "search_types.h"
#include "theme.h"
#include "token_type.h"
#include <chrono>
#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#ifdef UVIM_ENABLE_CLANGD_LSP
class LspClient;
#endif

class GitIgnore;
class ModeStateMachine;

class Editor
{
public:
    // Mode enum is now in mode.h
    // JumpLocation struct is now in jump_location.h

    Editor(bool skipInitialBuffer = false, const std::string& configPath = "");
    ~Editor();

    // Optional clangd LSP integration (compiled in when UVIM_ENABLE_CLANGD_LSP
    // is set). Enable at runtime via: uvim --clangd [--ccdir <build_dir>]
    // [files...]
    void enableClangdLsp(bool enable,
                         const std::string& compileCommandsDir = "",
                         const std::string& clangdPath = "clangd",
                         const std::string& queryDriverAllowList = "");
    bool isClangdLspEnabled() const;
    void enableRobotLsp(bool enable,
                        const std::string& robotLspPath = "robotframework-lsp",
                        const std::vector<std::string>& robotLspArgs = {});
    bool isRobotLspEnabled() const;
    void enablePythonLsp(bool enable,
                         const std::string& pythonLspPath = "pylsp",
                         const std::vector<std::string>& pythonLspArgs = {});
    bool isPythonLspEnabled() const;

    void run();
    void openFile(std::string_view filename);

    // Buffer struct is now in buffer.h
    // JumpLocation struct is now in jump_location.h

    bool visualBlockChanging = false;
    // Buffer management
    std::vector<std::unique_ptr<Buffer>> buffers;
    int currentBufferIndex = -1;
    Buffer* currentBuffer = nullptr;

    // References to current buffer's data (for easier access)
    std::vector<std::string>* lines = nullptr;
    std::string* filename = nullptr;
    bool* dirty = nullptr;
    int* cursorX = nullptr;
    int* cursorY = nullptr;
    int* wantedX = nullptr;
    int* offsetX = nullptr;
    int* offsetY = nullptr;

    // Fuzzy finder (FuzzyMatch struct is now in search_types.h)
    std::vector<FileEntry> allProjectFiles; // All files in project
    bool fuzzyInitialized = false;

    bool respectGitignore = true;

    // References browser (LSP find references)
    struct ReferenceEntry
    {
        std::string path;        // Full file path
        std::string displayPath; // Relative/shortened path for display
        int line = 0;            // 0-based line number
        int col = 0;             // 0-based column
        std::string lineContent; // Content of the line for preview
    };
    std::vector<ReferenceEntry> referencesList;
    int referencesCursor = 0;
    int referencesOffset = 0;
    bool referencesPreview = true;

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
    bool commandRequestedModeSet = false;
    Mode commandRequestedMode = NORMAL;
    std::string commandRequestedPath;
    std::string symbolPrefix;

    // clangd LSP (optional, runtime-enabled)
    bool clangdLspEnabled = false;
    std::string clangdLspCompileCommandsDir;
    std::string clangdLspPath = "clangd";
    std::string clangdLspQueryDriverAllowList;
    bool robotLspEnabled = false;
    std::string robotLspPath = "robotframework-lsp";
    std::vector<std::string> robotLspArgs;
    bool pythonLspEnabled = false;
    std::string pythonLspPath = "pylsp";
    std::vector<std::string> pythonLspArgs;
#ifdef UVIM_ENABLE_CLANGD_LSP
    std::unique_ptr<LspClient> lspClient;
    std::unique_ptr<LspClient> robotLspClient;
    std::unique_ptr<LspClient> pythonLspClient;
#endif

    Theme theme;

    // LSP completion popup (CompletionEntry struct is now in
    // completion_entry.h)
    bool completionActive = false;
    std::vector<CompletionEntry> completionAll; // full list from clangd
    std::vector<int> completionFiltered; // indices into completionAll (sorted
                                         // by fuzzy score)
    std::string completionQuery; // derived from buffer [anchorX, cursorX)
    int completionSelected = 0;  // index into completionFiltered
    int completionScroll = 0;  // first visible index (into completionFiltered)
    int completionAnchorX = 0; // start of current identifier/prefix
    int completionAnchorY = 0;

    void requestCompletion();
    void cancelCompletion();
    void acceptCompletion();
    void completionNext();
    void completionPrev();
    void rebuildCompletionFilter();
    void drawCompletionPopup(std::string& output) const;
    // Search (global state) - SearchMatch struct is now in search_types.h
    std::string searchQuery;
    bool searchForward = true;
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
    bool autoBraces = true;
    int tabSpaces = 4;
    std::string configPath;
    bool syntaxJson = true;
    bool syntaxYaml = true;

    // Drawing - Original functions
    void draw();
    void drawRows();
    void drawStatusBar();
    void drawMessageBar();
    void refreshScreen();
    void drawIncrementalUpdate();
    void drawFullScreen();
    void updateCursorPosition();

    // Drawing - NEW OPTIMIZATION FUNCTIONS
    void drawScrollUpdate(int scrollDelta);
    void drawStatusBarQuick();
    void drawMessageBarQuick(); // Add this to redraw message bar
    bool handleSetCommand(std::string_view cmd);

    // Mode handlers
    void handleNormalMode(int c);
    void handleInsertMode(int c);
    void handleVisualMode(int c);
    void handleCommandMode(int c);
    void handleSearchMode(int c);
    void handleKeypress(int c);
    void handleResize();

    // Buffer management functions
    void createNewBuffer();
    void switchToBuffer(int index);
    void closeCurrentBuffer();
    void nextBuffer();
    void previousBuffer();
    void listBuffers();
    int findBufferByFilename(const std::string& filename);
    void updateCurrentBufferPointers();
    void clearCurrentBufferPointers();
    bool hasBuffer() const;
    void ensureBufferForMode(Mode mode);
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
    void openFileBrowser(std::string_view path = ".");
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
    void collectProjectFiles(const std::string& dir, int depth,
                             const GitIgnore& gitignore);
    int fuzzyScore(const std::string& needle, const std::string& haystack,
                   std::vector<int>& matchPositions);

    // File browser navigation helpers (for mode handlers)
    void deleteFilePrompt();
    void renameFilePrompt();
    void createNewFilePrompt();
    void createNewDirectoryPrompt();

    // Grep search is now handled by GrepSearchMode state.
    void goToDefinition();

    // References browser functions (LSP find references)
    void findReferences();
    void clearReferences();
    bool selectReference();
    void openReferencePreview();
    void referencesUp();
    void referencesDown();
    void referencesHalfPageUp();
    void referencesHalfPageDown();
    void referencesFirst();
    void referencesLast();
    void toggleReferencesPreview();
    void drawReferences();
    bool hasReferences() const;
    std::string readLineFromFile(const std::string& path, int lineNum);

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
    void pushJumpLocation();
    void jumpForward();
    void jumpBack();
    void restoreJumpLocation(const JumpLocation& loc);
    void scrollHalfPageDown(bool visual);
    void scrollHalfPageUp(bool visual);
    void scrollHalfPageDown()
    {
        scrollHalfPageDown(false);
    }
    void scrollHalfPageUp()
    {
        scrollHalfPageUp(false);
    }
    void moveToMatchingBracket();
    void findCharForward(char c);
    void findCharBackward(char c);

    // Extended movement commands (for mode handlers)
    void moveToFirstNonBlank();
    void moveParagraphForward();
    void moveParagraphBackward();
    void moveWordForwardBig();
    void moveWordBackwardBig();
    void moveToEndOfWordBig();
    void findCharForwardBefore(char c); // 't' motion
    void findCharBackwardAfter(char c); // 'T' motion

    // Scrolling commands
    void scrollToTop();
    void scrollToBottom();
    void scrollPageUp();
    void scrollPageDown();
    void moveToScreenTop();
    void moveToScreenMiddle();
    void moveToScreenBottom();

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

    // Extended search commands (for mode handlers)
    void performSearch(const std::string& query, bool forward);
    void performIncrementalSearch(const std::string& query, bool forward);
    void searchWordUnderCursor(bool forward);
    void addSearchToHistory(const std::string& query);
    std::string getPreviousSearch();
    std::string getNextSearch();

    // Search history
    std::vector<std::string> searchHistory;
    int searchHistoryIndex = -1;

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

    // Extended editing commands (for mode handlers)
    void insertTab();
    void toggleCase();
    void joinLines();
    void insertLineAbove(); // Alias for insertNewlineAbove + position
    void insertLineBelow(); // Alias for insertNewlineBelow + position
    void deleteCurrentLine();
    void deleteToLineStart();
    void deleteCharAtCursor();
    void deleteCharBeforeCursor();
    void deleteWordBackward();
    void handleBackspace();
    void replaceCharAtCursor(char c);
    void repeatLastChange();
    void insertUtf8Char(int codepoint);
    void indentCurrentLine();
    void dedentCurrentLine();
    void handleLinewiseOperator(char op, int count);

    // Completion helpers (for insert mode)
    bool shouldTriggerCompletion();
    void triggerCompletion();
    void nextCompletion();
    void previousCompletion();

    // Aliases for compatibility
    void deleteToEndOfLine();     // Alias for deleteToLineEnd
    void switchToAlternateFile(); // Alias for jumpToAlternateFile

    // Copy/Paste
    void yankLine();
    void yankToLineEnd();
    void yankSelection();
    void yankWord();
    void pasteAfter();
    void pasteBefore();

    // System clipboard
    std::string getSystemClipboard();
    void setSystemClipboard(const std::string& text);
    void yankToSystemClipboard();
    void pasteFromSystemClipboard();

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

    // Extended visual mode commands (for mode handlers)
    void setVisualRange();
    void swapVisualEnds();
    void swapVisualBlockCorner();
    void prepareBlockInsert(bool atEnd);
    void indentSelection();
    void dedentSelection();
    void autoIndentSelection();
    void lowercaseSelection();
    void uppercaseSelection();
    void toggleCaseSelection();
    void yankLineSelection();
    void deleteLineSelection();
    void indentLineSelection();
    void dedentLineSelection();
    void autoIndentLineSelection();

    // File operations
    void saveFile();
    void executeCommand(std::string_view cmd);
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

    // Marks
    struct MarkLocation
    {
        std::string filename;
        int line = 0;
        int col = 0;
    };
    std::map<char, MarkLocation> marks;
    void setMark(char mark);
    void jumpToMark(char mark);

    // Misc utilities (for mode handlers)
    void goToFile();
    void showFileInfo();
    void forceFullRedraw();
    void executeOneNormalCommand(int key);

    // Command history
    std::vector<std::string> commandHistory;
    int commandHistoryIndex = -1;
    std::string commandInput;
    void commandHistoryUp();
    void commandHistoryDown();
    std::vector<std::string> getCommandCompletions(std::string_view prefix);
    std::vector<std::string> getPathCompletions(std::string_view path);

    // Syntax highlighting (TokenType enum and Token struct are now in
    // token_type.h)
    std::vector<JumpLocation> jumpBackStack;
    std::vector<JumpLocation> jumpForwardStack;

    bool isCppFile() const;
    bool isMlaFile() const;
    bool isRobotFile() const;
    bool isPythonFile() const;
    bool isJsonFile() const;
    bool isYamlFile() const;
    bool pythonFormatBuffer();
    void pythonLintBuffer();
    bool robotFormatBuffer();
    bool jsonFormatBuffer();
    bool yamlFormatBuffer();

    // clang-format helpers (used by Leader+f in visual modes)
    size_t byteOffsetForPosition(int y, int x) const;
    bool clangFormatWithArgs(const std::string& extraArgs,
                             const std::string& successMessage);
    void clangFormatVisualSelection();
    void clangFormatVisualBlockSelection();

    std::vector<Token> tokenizeLine(const std::string& line,
                                    bool& inBlockComment) const;
    std::string getColorCode(TokenType type) const;
    void renderLineWithSyntax(std::string& output, const std::string& line,
                              int start, int len, int fileRow);

    // Undo/Redo (now per-buffer, accessed through currentBuffer)
    void saveState();
    void undo();
    void redo();
    bool lastFindTill{false};

private:
    bool dispatchModeKey(int c);
    void syncModeFromStateMachine();
    std::unique_ptr<ModeStateMachine> modeStateMachine;
};
