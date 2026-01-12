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

bool CommandPrompt::isActive() const
{
    return active;
}

const std::string& CommandPrompt::getInput() const
{
    return input;
}

bool CommandPrompt::handle(
    ModeContext& ctx, int key,
    const std::function<std::optional<ModeState>(std::string_view)>& execute,
    std::optional<ModeState>& nextState)
{
    Editor* ed = ctx.editor;

    if(!active)
    {
        if(key == ':')
        {
            active = true;
            input.clear();
            ed->needsFullRedraw = true;
            nextState.reset();
            return true;
        }
        return false;
    }

    if(key == Terminal::ESC)
    {
        active = false;
        input.clear();
        ed->needsFullRedraw = true;
        nextState.reset();
        return true;
    }

    if(key == Terminal::ENTER)
    {
        nextState = execute(input);
        active = false;
        input.clear();
        ed->needsFullRedraw = true;
        return true;
    }

    if(key == Terminal::BACKSPACE || key == Terminal::DEL)
    {
        if(!input.empty())
        {
            input.pop_back();
            ed->needsFullRedraw = true;
        }
        nextState.reset();
        return true;
    }

    if(key >= 32 && key < 127)
    {
        input += static_cast<char>(key);
        ed->needsFullRedraw = true;
        nextState.reset();
        return true;
    }

    nextState.reset();
    return true;
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
