#include "comment_leader_pending_actions.h"

#include "editor.h"
#include "insert_mode.h"
#include "key_enums.h"
#include "mode_state_machine.h"
#include "normal_mode.h"

#include <algorithm>

namespace editor::statemachine::commentleader
{
bool isEscape(int key)
{
    return key == keyCode(control::ControlKey::ESC);
}

bool isEnter(int key)
{
    return key == keyCode(control::ControlKey::ENTER);
}

std::optional<ModeState> applyLineComment(ModeContext& ctx,
                                          CommentLeaderOrigin origin)
{
    Editor* ed = ctx.editor;
    if(origin == CommentLeaderOrigin::Normal)
    {
        ed->commentLines(*ed->cursorY, *ed->cursorY);
        return std::nullopt;
    }

    const int startY = std::min(ed->currentBuffer->visualStartY,
                                ed->currentBuffer->visualEndY);
    const int endY = std::max(ed->currentBuffer->visualStartY,
                              ed->currentBuffer->visualEndY);
    ed->commentLines(startY, endY);
    return NormalMode{};
}

void toggleNormalLineComment(ModeContext& ctx)
{
    ctx.editor->commentLines(*ctx.editor->cursorY, *ctx.editor->cursorY);
}

std::optional<ModeState> applyBlockComment(ModeContext& ctx,
                                           CommentLeaderOrigin origin)
{
    Editor* ed = ctx.editor;
    if(origin == CommentLeaderOrigin::Normal)
    {
        ed->commentBlock(*ed->cursorY, *ed->cursorY);
        return std::nullopt;
    }

    if(origin == CommentLeaderOrigin::Visual)
    {
        int startY = 0;
        int startX = 0;
        int endY = 0;
        int endX = 0;
        ed->getSelectionBounds(startY, startX, endY, endX);
        ed->commentBlockRange(startY, startX, endY, endX);
        return NormalMode{};
    }

    const int startY = std::min(ed->currentBuffer->visualStartY,
                                ed->currentBuffer->visualEndY);
    const int endY = std::max(ed->currentBuffer->visualStartY,
                              ed->currentBuffer->visualEndY);
    ed->commentBlock(startY, endY);
    return NormalMode{};
}

void toggleNormalBlockComment(ModeContext& ctx)
{
    ctx.editor->commentBlock(*ctx.editor->cursorY, *ctx.editor->cursorY);
}

std::optional<ModeState> applyTodoLineComment(ModeContext& ctx,
                                              CommentLeaderOrigin origin)
{
    if(origin != CommentLeaderOrigin::Normal)
        return applyLineComment(ctx, origin);

    if(ctx.editor->insertTodoLineComment(*ctx.editor->cursorY))
        return InsertMode{};
    return std::nullopt;
}

std::optional<ModeState> applyTodoBlockComment(ModeContext& ctx,
                                               CommentLeaderOrigin origin)
{
    if(origin != CommentLeaderOrigin::Normal)
        return applyBlockComment(ctx, origin);

    if(ctx.editor->insertTodoBlockComment(*ctx.editor->cursorY))
        return InsertMode{};
    return std::nullopt;
}
} // namespace editor::statemachine::commentleader
