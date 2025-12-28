#include "mode_state_machine.h"
#include "editor_lsp_query.h"
#include "terminal.h"

// ============================================================================
// ModeContext Implementation
// ============================================================================

void ModeContext::setStatusMessage(const std::string& msg)
{
    editor->setStatusMessage(msg);
}

int& ModeContext::cursorX()
{
    return *editor->cursorX;
}

int& ModeContext::cursorY()
{
    return *editor->cursorY;
}

int& ModeContext::offsetX()
{
    return *editor->offsetX;
}

int& ModeContext::offsetY()
{
    return *editor->offsetY;
}

int& ModeContext::wantedX()
{
    return *editor->wantedX;
}

std::vector<std::string>& ModeContext::lines()
{
    return *editor->lines;
}

const std::vector<std::string>& ModeContext::lines() const
{
    return *editor->lines;
}

bool& ModeContext::dirty()
{
    return *editor->dirty;
}

int ModeContext::screenRows() const
{
    return editor->screenRows;
}

int ModeContext::screenCols() const
{
    return editor->screenCols;
}

// ============================================================================
// ModeStateMachine Implementation
// ============================================================================

ModeStateMachine::ModeStateMachine(ModeContext ctx)
    : currentState_(NormalMode{}), context_(std::move(ctx))
{
    // Enter initial state
    std::visit([this](auto& state) { state.on_enter(context_); },
               currentState_);
}

void ModeStateMachine::dispatch(int key)
{
    KeyEvent event(key);

    auto newState =
        std::visit([this, &event](auto& state) -> std::optional<ModeState>
                   { return state.handle(context_, event); }, currentState_);

    if(newState)
    {
        // Exit current state
        std::visit([this](auto& state) { state.on_exit(context_); },
                   currentState_);

        currentState_ = std::move(*newState);

        // Enter new state
        std::visit([this](auto& state) { state.on_enter(context_); },
                   currentState_);
    }
}

const char* ModeStateMachine::currentStateName() const
{
    return std::visit([](const auto& state) { return state.name(); },
                      currentState_);
}

// ============================================================================
// Helper: Create context from Editor
// ============================================================================

ModeContext createModeContext(Editor* editor)
{
    return ModeContext{
        .editor = editor,
        .commandBuffer = editor->commandBuffer,
        .repeatCount = editor->repeatCount,
        .pendingOperator = editor->pendingOperator,
        .pendingAwaitingObject = editor->pendingAwaitingObject,
        .pendingObjectType = editor->pendingObjectType,
        .pendingCount = editor->pendingCount,
        .searchQuery = editor->searchQuery,
        .searchForward = editor->searchForward,
        .savedCursorX = editor->savedCursorX,
        .savedCursorY = editor->savedCursorY,
    };
}
