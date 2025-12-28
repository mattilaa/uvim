#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

// Forward declarations
class Editor;

// ============================================================================
// Events - Input events that trigger state transitions
// ============================================================================

struct KeyEvent
{
    int key;
    explicit KeyEvent(int k) : key(k) {}
};

struct EscapeEvent
{
};

struct EnterEvent
{
};

struct BackspaceEvent
{
};

struct TabEvent
{
};

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
struct InsertMode;
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

// The variant holding all possible states
using ModeState =
    std::variant<NormalMode, InsertMode, VisualMode, VisualLineMode,
                 VisualBlockMode, CommandMode, SearchForwardMode,
                 SearchBackwardMode, FileBrowserMode, FuzzyFindMode,
                 BufferBrowserMode, GrepSearchMode, OperatorPendingMode>;

// ============================================================================
// State Definitions
// ============================================================================

struct NormalMode
{
    static constexpr const char* name()
    {
        return "NORMAL";
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);
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

    // Command-line completion state
    std::vector<std::string> completions;
    int completionIndex = -1;
    std::string originalInput;

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);
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
    void deleteWordBackward(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);
};

struct SearchBackwardMode
{
    static constexpr const char* name()
    {
        return "?";
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);
    void deleteWordBackward(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);
};

struct FileBrowserMode
{
    static constexpr const char* name()
    {
        return "BROWSE";
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);
};

struct FuzzyFindMode
{
    static constexpr const char* name()
    {
        return "FUZZY";
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);
};

struct BufferBrowserMode
{
    static constexpr const char* name()
    {
        return "BUFFERS";
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);
};

struct GrepSearchMode
{
    static constexpr const char* name()
    {
        return "GREP";
    }

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx, const KeyEvent& event);
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

// ============================================================================
// Mode State Machine
// ============================================================================

class ModeStateMachine
{
public:
    explicit ModeStateMachine(ModeContext ctx);

    // Dispatch a key event to the current state
    void dispatch(int key);

    // Get current state name
    const char* currentStateName() const;

    // Check if in a specific state
    template <typename State>
    bool isIn() const
    {
        return std::holds_alternative<State>(currentState_);
    }

    // Access current state (for state-specific operations)
    template <typename State>
    State* getState()
    {
        return std::get_if<State>(&currentState_);
    }

    // Force transition to a specific state
    template <typename State>
    void transitionTo(State&& newState)
    {
        // Exit current state
        std::visit([this](auto& state) { state.on_exit(context_); },
                   currentState_);

        currentState_ = std::forward<State>(newState);

        // Enter new state
        std::visit([this](auto& state) { state.on_enter(context_); },
                   currentState_);
    }

    // Access context
    ModeContext& context()
    {
        return context_;
    }
    const ModeContext& context() const
    {
        return context_;
    }

private:
    ModeState currentState_;
    ModeContext context_;
};

// ============================================================================
// Helper: Create initial context from Editor
// ============================================================================

ModeContext createModeContext(Editor* editor);
