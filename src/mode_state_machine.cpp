#include "mode_state_machine.h"
#include "editor.h"
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
//
// Note: ModeStateMachine now inherits from StateMachine<ModeState, ModeContext,
// KeyEvent> so all the dispatch/transition logic is provided by the base class
// template. Only the createModeContext helper needs implementation here.
//
// The constructors are defined inline in the header using the base class.
//

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

ModeState defaultExitMode(const Editor* editor)
{
    if(!editor || editor->buffers.empty())
        return WelcomeMode{};
    return NormalMode{};
}
