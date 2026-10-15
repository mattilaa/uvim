#include "task_list_mode.h"
#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"

#include <algorithm>

namespace editor::statemachine
{
namespace
{
// Re-anchors the cursor on the selected job after the list changed.
void syncCursor(TaskListMode& mode,
                const std::vector<TaskQueue::TaskInfo>& rows)
{
    if(rows.empty())
    {
        mode.cursor = 0;
        mode.selected = 0;
        return;
    }
    auto it = std::find_if(rows.begin(), rows.end(),
                           [&](const TaskQueue::TaskInfo& row)
                           { return row.id == mode.selected; });
    if(it != rows.end())
        mode.cursor = static_cast<int>(it - rows.begin());
    mode.cursor = std::clamp(mode.cursor, 0, (int)rows.size() - 1);
    mode.selected = rows[mode.cursor].id;
}
} // namespace

void TaskListMode::on_enter(ModeContext& ctx)
{
    ctx.editor->needsFullRedraw = true;
}

void TaskListMode::on_exit(ModeContext& ctx)
{
    (void)ctx;
    Terminal::setCursorBlock();
}

std::optional<ModeState> TaskListMode::handle(ModeContext& ctx,
                                              const ModeKeyEvent& event)
{
    Editor* ed = ctx.editor;
    const int c = keyCode(event.key);
    auto rows = ed->tasks.snapshot();
    syncCursor(*this, rows);
    ed->needsFullRedraw = true;

    const bool wasPendingDelete = pendingDelete;
    pendingDelete = false;

    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(typed::TypedKey::KEY_Q))
    {
        if(c == keyCode(control::ControlKey::ESC))
            ed->noteDoubleEscStatusClear();
        return ctx.hasBuffer() ? ModeState{NormalMode{}}
                               : ModeState{WelcomeMode{}};
    }
    if(rows.empty())
        return std::nullopt;

    auto select = [&](int index)
    {
        cursor = std::clamp(index, 0, (int)rows.size() - 1);
        selected = rows[cursor].id;
    };
    const TaskQueue::TaskInfo& row = rows[cursor];

    if(c == keyCode(typed::TypedKey::KEY_J) ||
       c == keyCode(navigation::NavigationKey::ARROW_DOWN))
        select(cursor + 1);
    else if(c == keyCode(typed::TypedKey::KEY_K) ||
            c == keyCode(navigation::NavigationKey::ARROW_UP))
        select(cursor - 1);
    else if(c == keyCode(typed::TypedKey::KEY_G))
        select(0);
    else if(c == keyCode(typed::TypedKey::KEY_CAP_G))
        select((int)rows.size() - 1);
    else if(c == keyCode(typed::TypedKey::KEY_CAP_K))
    {
        if(!ed->tasks.moveEarlier(row.id))
            ed->setStatusMessage(
                "Only waiting tasks can move, and not past running ones");
    }
    else if(c == keyCode(typed::TypedKey::KEY_CAP_J))
    {
        if(!ed->tasks.moveLater(row.id))
            ed->setStatusMessage(
                "Only waiting tasks can move, and not past running ones");
    }
    else if(c == keyCode(typed::TypedKey::KEY_X) ||
            (c == keyCode(typed::TypedKey::KEY_D) && wasPendingDelete))
    {
        if(ed->tasks.cancel(row.id))
            ed->setStatusMessage(
                (row.state == TaskQueue::State::Running ? "Cancelling '"
                                                        : "Removed '") +
                row.label + "'");
    }
    else if(c == keyCode(typed::TypedKey::KEY_D))
        pendingDelete = true;

    return std::nullopt;
}
} // namespace editor::statemachine
