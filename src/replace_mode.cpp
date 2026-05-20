#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#include <cctype>
#include <chrono>
#include <optional>

// ============================================================================
// ReplaceMode Implementation
// ============================================================================

namespace editor::statemachine
{
void ReplaceMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    ed->needsFullRedraw = true;

    // Set cursor to bar for replace mode (no underline available)
    Terminal::setCursorBarBlinking();
}

void ReplaceMode::on_exit(ModeContext& ctx)
{
    ctx.editor->finishChangeRecordingIfDeferred();

    // Restore block cursor
    Terminal::setCursorBlock();
}

std::optional<ModeState> ReplaceMode::handle(ModeContext& ctx, int key)
{
    Editor* ed = ctx.editor;
    int c = keyCode(key);
    if(ed->isRecordingChange() && !ed->isReplayingChange())
    {
        ed->recordChangeKey(c);
    }

    // ========================================================================
    // Exit Replace Mode
    // ========================================================================

    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(control::ControlKey::CTRL_C))
    {
        if(ctx.cursorX() > 0)
        {
            ctx.cursorX()--;
        }
        ed->saveState();
        return NormalMode{};
    }

    // ========================================================================
    // Backspace
    // ========================================================================

    if(c == keyCode(control::ControlKey::BACKSPACE) || c == 127 ||
       c == keyCode(control::ControlKey::CTRL_H))
    {
        if(ctx.cursorX() > 0)
        {
            ctx.cursorX()--;
            // In replace mode, backspace doesn't delete, just moves back
        }
        return std::nullopt;
    }

    // ========================================================================
    // Character Replacement
    // ========================================================================

    if(c >= 32 && c < 127)
    {
        auto& lines = ctx.lines();
        int& cursorX = ctx.cursorX();
        int cursorY = ctx.cursorY();

        if(cursorY < (int)lines.size())
        {
            std::string& line = lines[cursorY];
            if(cursorX < (int)line.length())
            {
                line[cursorX] = static_cast<char>(c);
            }
            else
            {
                line += static_cast<char>(c);
            }
            cursorX++;
            *ed->dirty = true;
        }
        return std::nullopt;
    }

    // ========================================================================
    // Enter - Insert Newline
    // ========================================================================

    if(c == keyCode(control::ControlKey::ENTER))
    {
        ed->insertNewline();
        return std::nullopt;
    }

    return std::nullopt;
}
} // namespace editor::statemachine
