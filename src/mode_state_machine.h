#pragma once

#include "file_entry.h"
#include "mode.h"
#include "search_types.h"
#include "state_machine.h"
#include <chrono>
#include <ctime>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Forward declarations
class Editor;

// ============================================================================
// Editor Context - Shared state accessible by all mode handlers
// ============================================================================

struct ModeContext
{
    Editor* editor;

    // Command/search buffer
    std::string& commandBuffer;

    // Repeat count for normal mode commands
    int& repeatCount;

    // Pending operator state
    char& pendingOperator;
    bool& pendingAwaitingObject;
    char& pendingObjectType;
    int& pendingCount;

    // Search state
    std::string& searchQuery;
    bool& searchForward;
    int& savedCursorX;
    int& savedCursorY;

    // Status message
    void setStatusMessage(const std::string& msg);

    // Cursor access (delegates to editor's current buffer)
    int& cursorX();
    int& cursorY();
    int& offsetX();
    int& offsetY();
    int& wantedX();

    // Lines access
    std::vector<std::string>& lines();
    const std::vector<std::string>& lines() const;

    // Buffer dirty flag
    bool& dirty();

    // Screen dimensions
    int screenRows() const;
    int screenCols() const;

    // UI helpers
    void requestFullRedraw();
    void forceFullRedraw();

    // Editor state helpers
    bool hasBuffer() const;
    bool hasCurrentBuffer() const;
    bool hasFilename() const;
    std::string_view currentFilename() const;
    Mode currentMode() const;

    std::chrono::steady_clock::time_point& lastEscTime();
    bool hasSearchMatches() const;
    bool hasSearchQuery() const;

    bool completionActive() const;
    bool completionFromLsp() const;
    bool autoCompletion() const;
    bool autoBraces() const;
    int tabSpaces() const;
    bool respectGitignore() const;
    void setRespectGitignore(bool value);
    void setFuzzyInitialized(bool value);

    // Command helpers
    void executeCommand(std::string_view cmd);
    bool takeCommandRequest(Mode& mode, std::string& path);
    std::optional<std::string> commandHistoryUp();
    std::optional<std::string> commandHistoryDown();
    void startCommandHistorySearch(std::string_view seed);
    std::string cancelCommandHistorySearch();
    std::string acceptCommandHistorySearch();
    void updateCommandHistorySearchQuery(std::string_view query);
    void moveCommandHistorySearchCursor(int delta);
    bool isCommandHistorySearchActive() const;
    std::string_view commandHistorySearchQuery() const;
    std::vector<std::string> getCommandCompletions(std::string_view prefix);
    std::vector<std::string> getPathCompletions(std::string_view path);

    // Buffer/file helpers
    void openFile(std::string_view path);
    void openFileBrowser(std::string_view path);
    void switchToBuffer(int index);
    void closeCurrentBuffer();
    void saveFile();
    void deleteFilePrompt();
    void renameFilePrompt();
    void createNewFilePrompt();
    void createNewDirectoryPrompt();

    // Formatting and linting
    bool pythonFormatBuffer();
    void pythonLintBuffer();
    bool robotFormatBuffer();
    bool jsonFormatBuffer();
    bool yamlFormatBuffer();
    bool clangFormatWithArgs(const std::string& extraArgs,
                             const std::string& successMessage);
    void clangFormatVisualSelection();
    void clangFormatVisualBlockSelection();

    // Search helpers
    void performSearch();
    void performIncrementalSearch(const std::string& query, bool forward);
    void addSearchToHistory(const std::string& query);
    std::string getPreviousSearch();
    std::string getNextSearch();
    void findAllMatches();
    void jumpToMatch(int index);
    void searchNext();
    void searchPrevious();
    void searchWordUnderCursor(bool forward);
    void clearSearch();

    // Completion helpers
    void nextCompletion();
    void previousCompletion();
    void acceptCompletion();
    void cancelCompletion();
    void rebuildCompletionFilter();
    void triggerCompletion();
    void requestCompletion();
    bool shouldTriggerCompletion() const;

    // Change recording helpers
    void beginChangeRecording(int count);
    void recordChangeKey(int key);
    void deferChangeRecordingCommit();
    void finishChangeRecordingIfDeferred();
    bool isRecordingChange() const;
    bool isReplayingChange() const;
    void cancelChangeRecording();
    void commitChangeRecording();
    int readKeyRecorded();
    void repeatLastChange(int times);

    // Movement helpers
    void moveLeft();
    void moveRight();
    void moveUp(int count = 1);
    void moveDown(int count = 1);
    void moveWordForward();
    void moveWordBackward();
    void moveWordForwardBig();
    void moveWordBackwardBig();
    void moveToEndOfWord();
    void moveToEndOfWordBig();
    void moveToLineStart();
    void moveToLineEnd();
    void moveToFirstLine();
    void moveToLastLine();
    void moveToLine(int line);
    void moveToFirstNonBlank();
    void moveToMatchingBracket();
    void moveParagraphForward();
    void moveParagraphBackward();
    void moveToScreenTop();
    void moveToScreenMiddle();
    void moveToScreenBottom();
    void scrollPageUp();
    void scrollPageDown();
    void scrollHalfPageUp();
    void scrollHalfPageDown();
    void scrollToTop();
    void scrollToBottom();
    void centerScreen();

    // Editing helpers
    void insertLineAbove();
    void insertLineBelow();
    void insertNewline();
    void insertTab();
    void insertChar(char c);
    void insertUtf8Char(int c);
    void replaceCharAtCursor(char c);
    void deleteCharAtCursor();
    void deleteCharBeforeCursor();
    void deleteCurrentLine();
    void deleteToEndOfLine();
    void deleteWordBackward();
    void deleteToLineStart();
    void joinLines();
    void toggleCase();
    void pasteAfter();
    void pasteBefore();
    void pasteFromSystemClipboard();
    void yankLine();
    void yankToSystemClipboard();
    void saveState();

    // Visual helpers
    void deleteSelection();
    void yankSelection();
    void deleteVisualBlock();
    void yankVisualBlock();
    void indentSelection();
    void dedentSelection();
    void autoIndentSelection();
    void indentLineSelection();
    void dedentLineSelection();
    void autoIndentLineSelection();
    void lowercaseSelection();
    void uppercaseSelection();
    void toggleCaseSelection();
    void yankLineSelection();
    void deleteLineSelection();
    void prepareBlockInsert(bool atEnd);
    void swapVisualBlockCorner();
    void changeVisualBlock();

    // Operator helpers
    bool getTextObjectRange(char objChar, bool around, int& outStartY,
                            int& outStartX, int& outEndY, int& outEndX);
    void applyOperatorToRange(char op, int startY, int startX, int endY,
                              int endX);
    void handleLinewiseOperator(char op, int count);

    // Navigation / refs / info
    void jumpBack();
    void jumpForward();
    void jumpToAlternateFile();
    void switchToAlternateFile();
    void setMark(char mark);
    void jumpToMark(char mark);
    void goToDefinition();
    void findReferences();
    bool hasReferences() const;
    void clearReferences();
    void referencesUp();
    void referencesDown();
    void referencesFirst();
    void referencesLast();
    void referencesHalfPageUp();
    void referencesHalfPageDown();
    void selectReference();
    void toggleReferencesPreview();
    void openReferencePreview();
    void showFileInfo();
    void goToFile();
    void showLspInfo();
    void clearLspInfo();
    void forceQuit();

    // Language/LSP checks
    bool isCppFile() const;
    bool isPythonFile() const;
    bool isRobotFile() const;
    bool isJsonFile() const;
    bool isYamlFile() const;
    bool isClangdLspEnabled() const;
    bool isPythonLspEnabled() const;
    bool isRobotLspEnabled() const;
};

// ============================================================================
// Mode States - Each mode is a struct with handler methods
// ============================================================================

// Forward declare all states for the variant
struct NormalMode;
struct WelcomeMode;
struct InsertMode;
struct ReplaceMode;
struct VisualMode;
struct VisualLineMode;
struct VisualBlockMode;
struct CommandMode;
struct SearchForwardMode;
struct SearchBackwardMode;
struct FileBrowserMode;
struct FuzzyFindMode;
struct BufferBrowserMode;
struct GrepSearchMode;
struct OperatorPendingMode;
struct ReferencesMode;
struct LspInfoMode;
struct HelpMode;

// The variant holding all possible states
using ModeState =
    std::variant<WelcomeMode, NormalMode, InsertMode, ReplaceMode, VisualMode,
                 VisualLineMode, VisualBlockMode, CommandMode,
                 SearchForwardMode, SearchBackwardMode, FileBrowserMode,
                 FuzzyFindMode, BufferBrowserMode, GrepSearchMode,
                 OperatorPendingMode, ReferencesMode, LspInfoMode, HelpMode>;

ModeState defaultExitMode(const Editor* editor);

struct CommandPrompt
{
    bool isActive() const;
    const std::string& getInput() const;
    bool handle(ModeContext& ctx, int key,
                const std::function<std::optional<ModeState>(std::string_view)>&
                    execute,
                std::optional<ModeState>& nextState);

private:
    bool active = false;
    std::string input;
};

// ============================================================================
// State Definitions
// ============================================================================

struct WelcomeMode
{
    static constexpr const char* name()
    {
        return "WELCOME";
    }

    CommandPrompt commandPrompt;

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);

    void draw(Editor& editor) const;

private:
    std::optional<ModeState> executeCommand(ModeContext& ctx,
                                            std::string_view commandLine);
};

struct NormalMode
{
    static constexpr const char* name()
    {
        return "NORMAL";
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);

private:
    // Helper methods for complex key sequences
    std::optional<ModeState> handleLeaderKey(ModeContext& ctx, int c);
    std::optional<ModeState> handleGCommand(ModeContext& ctx, int c);
    std::optional<ModeState> handleZCommand(ModeContext& ctx, int c);
};

struct InsertMode
{
    static constexpr const char* name()
    {
        return "INSERT";
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);
};

struct ReplaceMode
{
    static constexpr const char* name()
    {
        return "REPLACE";
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);
};

struct VisualMode
{
    static constexpr const char* name()
    {
        return "VISUAL";
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);
};

struct VisualLineMode
{
    static constexpr const char* name()
    {
        return "VISUAL LINE";
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);
};

struct VisualBlockMode
{
    static constexpr const char* name()
    {
        return "VISUAL BLOCK";
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);
};

struct CommandMode
{
    static constexpr const char* name()
    {
        return "COMMAND";
    }

    // Tab completion state
    std::vector<std::string> completions;
    int completionIndex = -1;
    std::string originalInput;

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);

private:
    // Helper methods
    void handleTabCompletion(ModeContext& ctx);
    void handleReverseTabCompletion(ModeContext& ctx);
    void deleteWordBackward(ModeContext& ctx);
};

struct SearchForwardMode
{
    static constexpr const char* name()
    {
        return "/";
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);

    // Helper methods
    void deleteWordBackward(ModeContext& ctx);
};

struct SearchBackwardMode
{
    static constexpr const char* name()
    {
        return "?";
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);

    // Helper methods
    void deleteWordBackward(ModeContext& ctx);
};

struct FileBrowserMode
{
    static constexpr const char* name()
    {
        return "BROWSE";
    }

    std::vector<FileEntry> fileList;
    std::string currentDirectory;
    std::string previousFile;
    int browserCursor = 0;
    int browserOffset = 0;
    bool showHidden = false;
    CommandPrompt commandPrompt;

    FileBrowserMode() = default;
    explicit FileBrowserMode(std::string startDir, std::string prevFile = {})
        : currentDirectory(std::move(startDir)),
          previousFile(std::move(prevFile))
    {
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);

    void draw(Editor& editor) const;

private:
    void loadDirectory(ModeContext& ctx, const std::string& pathStr);
    std::string formatFileSize(size_t size) const;
    std::string formatFileTime(time_t time) const;
    std::optional<ModeState> executeCommand(ModeContext& ctx,
                                            std::string_view commandLine);
};

struct FuzzyFindMode
{
    static constexpr const char* name()
    {
        return "FUZZY";
    }

    std::vector<FuzzyMatch> matches;
    std::string query;
    int cursor = 0;
    int offset = 0;

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);

    void draw(Editor& editor) const;

private:
    void initializeFiles(Editor& editor);
    void updateMatches(Editor& editor);
    void moveDown(Editor& editor);
    void moveUp(Editor& editor);
    void halfPageDown(Editor& editor);
    void halfPageUp(Editor& editor);
    void addChar(Editor& editor, char c);
    void backspace(Editor& editor);
    void deleteWord(Editor& editor);
    void clearQuery(Editor& editor);
    void toggleGitignore(Editor& editor);
    bool select(Editor& editor);
};

struct BufferBrowserMode
{
    static constexpr const char* name()
    {
        return "BUFFERS";
    }

    std::vector<BufferMatch> bufferMatches;
    std::string bufferQuery;
    int bufferCursor = 0;
    int bufferOffset = 0;

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);

    void draw(Editor& editor) const;

private:
    void updateMatches(Editor& editor);
    void selectMatch(Editor& editor);
};

struct GrepSearchMode
{
    static constexpr const char* name()
    {
        return "GREP";
    }

    std::vector<GrepMatch> matches;
    std::string query;
    int cursor = 0;
    int offset = 0;
    bool searching = false;
    bool caseSensitive = false;
    bool previewEnabled = false;

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);

    void draw(Editor& editor) const;

private:
    void initialize(Editor& editor);
    void performSearch(Editor& editor);
    void searchInFile(const std::string& filepath, std::string_view query);
    bool isTextFile(const std::string& filepath) const;
    bool isBinaryFile(const std::string& filepath) const;
    std::string trimString(const std::string& str) const;
    bool selectMatch(Editor& editor);
    void resultUp(Editor& editor);
    void resultDown(Editor& editor);
    void resultHalfPageUp(Editor& editor);
    void resultHalfPageDown(Editor& editor);
    void searchAddChar(Editor& editor, char c);
    void searchBackspace(Editor& editor);
    void searchDeleteWord(Editor& editor);
    void searchClear();
    void toggleGitignore(Editor& editor);
    void togglePreview();
};

struct OperatorPendingMode
{
    static constexpr const char* name()
    {
        return "OP_PENDING";
    }

    char op = 0;
    int count = 1;
    bool awaitingObject = false;
    char objectType = 0;

    explicit OperatorPendingMode(char pendingOp = 0, int cnt = 1)
        : op(pendingOp), count(cnt)
    {
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);
};

struct ReferencesMode
{
    static constexpr const char* name()
    {
        return "REFERENCES";
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);
};

struct LspInfoMode
{
    static constexpr const char* name()
    {
        return "LSP INFO";
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);
};

struct HelpMode
{
    static constexpr const char* name()
    {
        return "HELP";
    }

    std::string topic;
    std::vector<std::string> lines;
    int scrollOffset = 0;
    std::string previousFile;
    CommandPrompt commandPrompt;

    HelpMode() = default;
    explicit HelpMode(std::string helpTopic, std::string prevFile = {})
        : topic(std::move(helpTopic)), previousFile(std::move(prevFile))
    {
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);

    void draw(Editor& editor) const;

private:
    void loadHelpContent(const std::string& helpTopic);
    std::optional<ModeState> executeCommand(ModeContext& ctx,
                                            std::string_view commandLine);
};

// ============================================================================
// Mode State Machine - Inherits from generic StateMachine
// ============================================================================

// Base type alias for the generic state machine
using ModeStateMachineBase = StateMachine<ModeState, ModeContext, KeyEvent>;

class ModeStateMachine : public ModeStateMachineBase
{
public:
    // Constructor - starts in NormalMode by default
    explicit ModeStateMachine(ModeContext ctx)
        : ModeStateMachineBase(std::move(ctx), NormalMode{})
    {
    }

    // Constructor with explicit initial state
    template <typename InitialState>
    ModeStateMachine(ModeContext ctx, InitialState&& initial)
        : ModeStateMachineBase(std::move(ctx),
                               std::forward<InitialState>(initial))
    {
    }

    // Convenience method to dispatch by key code (wraps in KeyEvent)
    void dispatch(int key)
    {
        ModeStateMachineBase::dispatch(KeyEvent{key});
    }

    // Also allow dispatching KeyEvent directly (from base class)
    using ModeStateMachineBase::dispatch;
};

// ============================================================================
// Helper: Create initial context from Editor
// ============================================================================

ModeContext createModeContext(Editor* editor);
