#pragma once

#include "file_entry.h"
#include "search_types.h"
#include "state_machine.h"
#include <ctime>
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

// The variant holding all possible states
using ModeState =
    std::variant<WelcomeMode, NormalMode, InsertMode, ReplaceMode, VisualMode,
                 VisualLineMode, VisualBlockMode, CommandMode,
                 SearchForwardMode, SearchBackwardMode, FileBrowserMode,
                 FuzzyFindMode, BufferBrowserMode, GrepSearchMode,
                 OperatorPendingMode, ReferencesMode, LspInfoMode>;

ModeState defaultExitMode(const Editor* editor);

// ============================================================================
// State Definitions
// ============================================================================

struct WelcomeMode
{
    static constexpr const char* name()
    {
        return "WELCOME";
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);

    void draw(Editor& editor) const;
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
