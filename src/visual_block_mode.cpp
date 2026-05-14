#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include <algorithm>

// ============================================================================
// VisualBlockMode Implementation
// ============================================================================

void VisualBlockMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;

    // Initialize visual block selection
    ed->currentBuffer->visualBlockStartX = ctx.cursorX();
    ed->currentBuffer->visualBlockStartY = ctx.cursorY();
    ed->currentBuffer->visualBlockEndX = ctx.cursorX();
    ed->currentBuffer->visualBlockEndY = ctx.cursorY();

    ed->needsFullRedraw = true;
}

void VisualBlockMode::on_exit(ModeContext& ctx)
{
    ctx.editor->needsFullRedraw = true;
}

std::optional<ModeState> VisualBlockMode::handle(ModeContext& ctx, int key)
{
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    // ========================================================================
    // Count Prefix Accumulation
    // ========================================================================

    if(ctx.repeatCount == 0 && c >= keyCode(typed::TypedKey::KEY_1) &&
       c <= keyCode(typed::TypedKey::KEY_9))
    {
        ctx.repeatCount = c - keyCode(typed::TypedKey::KEY_0);
        return std::nullopt;
    }
    if(ctx.repeatCount > 0 && c >= keyCode(typed::TypedKey::KEY_0) &&
       c <= keyCode(typed::TypedKey::KEY_9))
    {
        ctx.repeatCount =
            ctx.repeatCount * 10 + (c - keyCode(typed::TypedKey::KEY_0));
        return std::nullopt;
    }
    int count = std::max(1, ctx.repeatCount);

    // ========================================================================
    // Exit
    // ========================================================================

    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(control::ControlKey::CTRL_V))
    {
        if(c == keyCode(control::ControlKey::ESC))
            ed->noteDoubleEscStatusClear();
        return NormalMode{};
    }

    // ========================================================================
    // Leader Key (Space)
    // ========================================================================

    if(ctx.commandBuffer == " ")
    {
        if(c == keyCode(typed::TypedKey::KEY_F))
        {
            ctx.commandBuffer.clear();
            ctx.repeatCount = 0;
            if(ed->isFileType<FileType::Python>())
            {
                ed->pythonFormatBuffer();
            }
            else if(ed->isFileType<FileType::Robot>())
            {
                ed->robotFormatBuffer();
            }
            else if(ed->isFileType<FileType::Json>())
            {
                ed->jsonFormatBuffer();
            }
            else if(ed->isFileType<FileType::Yaml>())
            {
                ed->yamlFormatBuffer();
            }
            else if(ed->isFileType<FileType::Mla>())
            {
                ed->mlangFormatBuffer();
            }
            else
            {
                ed->clangFormatVisualBlockSelection();
            }
            return NormalMode{};
        }
        if(c == keyCode(control::ControlKey::SPACE))
        {
            ctx.commandBuffer.clear();
            ctx.setStatusMessage("");
            ctx.repeatCount = 0;
            return std::nullopt;
        }

        ctx.commandBuffer.clear();
        ctx.setStatusMessage("");
        ctx.repeatCount = 0;
    }

    if(c == keyCode(control::ControlKey::SPACE))
    {
        ctx.commandBuffer = " ";
        ctx.setStatusMessage("Leader");
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Movement
    // ========================================================================

    bool didMove = false;
    switch(c)
    {
    case keyCode(typed::TypedKey::KEY_H):
    case keyCode(navigation::NavigationKey::ARROW_LEFT):
        ed->moveLeft(count);
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_J):
    case keyCode(navigation::NavigationKey::ARROW_DOWN):
        ed->moveDown(count);
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_K):
    case keyCode(navigation::NavigationKey::ARROW_UP):
        ed->moveUp(count);
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_L):
    case keyCode(navigation::NavigationKey::ARROW_RIGHT):
        ed->moveRight(count);
        didMove = true;
        break;

        // ========================================================================
        // Block Operations
        // ========================================================================

    case keyCode(typed::TypedKey::KEY_D):
    case keyCode(typed::TypedKey::KEY_X):
        ed->deleteVisualBlock();
        ed->saveState();
        return NormalMode{};

    case keyCode(typed::TypedKey::KEY_Y):
        ed->yankVisualBlock();
        return NormalMode{};

    case keyCode(typed::TypedKey::KEY_C):
        ed->changeVisualBlock();
        return InsertMode{};

    // Insert at block start
    case keyCode(typed::TypedKey::KEY_CAP_I):
        ed->prepareBlockInsert(false);
        return InsertMode{};

    // Append at block end
    case keyCode(typed::TypedKey::KEY_CAP_A):
        ed->prepareBlockInsert(true);
        return InsertMode{};

    // Swap corners
    case keyCode(typed::TypedKey::KEY_O):
    case keyCode(typed::TypedKey::KEY_CAP_O):
        ed->swapVisualBlockCorner();
        didMove = true;
        break;
    }

    if(didMove)
        ctx.repeatCount = 0;

    // Update block end
    ed->currentBuffer->visualBlockEndX = ctx.cursorX();
    ed->currentBuffer->visualBlockEndY = ctx.cursorY();
    ed->needsFullRedraw = true;

    return std::nullopt;
}
