#include "comment_leader_pending_actions.h"

#include "editor.h"
#include "editor_utils.h"
#include "insert_mode.h"
#include "key_enums.h"
#include "mode_state_machine.h"
#include "normal_mode.h"
#include "text_utils.h"

#include <algorithm>
#include <utility>

namespace editor::statemachine::commentleader
{
namespace
{
std::string_view trimAsciiView(std::string_view value)
{
    while(!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
    while(!value.empty() && (value.back() == ' ' || value.back() == '\t'))
        value.remove_suffix(1);
    return value;
}

std::string commentRulesPath(const Editor& editor)
{
    if(editor.filename && !editor.filename->empty())
        return *editor.filename;
    if(editor.currentBuffer && !editor.currentBuffer->filename.empty())
        return editor.currentBuffer->filename;
    return {};
}

std::string leadingWhitespace(std::string_view line)
{
    const size_t pos = line.find_first_not_of(" \t");
    if(text_utils::is_not_found(pos))
        return std::string(line);
    return std::string(line.substr(0, pos));
}

std::pair<int, int> visualLineSelection(const Editor& editor)
{
    return {std::min(editor.currentBuffer->visualStartY,
                     editor.currentBuffer->visualEndY),
            std::max(editor.currentBuffer->visualStartY,
                     editor.currentBuffer->visualEndY)};
}

std::optional<ModeState> insertVisualLineTodoLineComment(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    if(!ed || !ed->currentBuffer || !ed->lines)
        return std::nullopt;

    const auto rules =
        editor::helper::locCommentRulesForPath(commentRulesPath(*ed));
    if(!rules.hasLine)
    {
        ed->setStatusMessage("todo comment: unsupported filetype");
        return std::nullopt;
    }

    auto [startY, endY] = visualLineSelection(*ed);
    (void)endY;
    startY = std::clamp(startY, 0, static_cast<int>(ed->lines->size()));
    const std::string indent = startY < static_cast<int>(ed->lines->size())
                                   ? leadingWhitespace((*ed->lines)[startY])
                                   : "";
    const std::string todoLine = indent + std::string(rules.line) + " TODO: ";

    ed->saveState();
    ed->lines->insert(ed->lines->begin() + startY, todoLine);
    *ed->cursorY = startY;
    *ed->cursorX = static_cast<int>(todoLine.size());
    *ed->wantedX = *ed->cursorX;
    *ed->dirty = true;
    ed->currentBuffer->lspSyncNeeded = true;
    ed->needsFullRedraw = true;
    ed->saveState();
    return InsertMode{};
}

std::optional<ModeState> insertVisualLineTodoBlockComment(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    if(!ed || !ed->currentBuffer || !ed->lines)
        return std::nullopt;

    const auto rules =
        editor::helper::locCommentRulesForPath(commentRulesPath(*ed));
    if(!rules.hasBlock || rules.blockStart != "/*" || rules.blockEnd != "*/")
    {
        ed->setStatusMessage("todo block comment: unsupported filetype");
        return std::nullopt;
    }

    auto [startY, endY] = visualLineSelection(*ed);
    startY = std::clamp(startY, 0, static_cast<int>(ed->lines->size()));
    endY = std::clamp(endY, startY, static_cast<int>(ed->lines->size()) - 1);

    int indentLine = startY;
    while(indentLine <= endY &&
          text_utils::is_not_found(
              (*ed->lines)[indentLine].find_first_not_of(" \t")))
        ++indentLine;

    const std::string indent =
        indentLine <= endY ? leadingWhitespace((*ed->lines)[indentLine]) : "";
    const std::string todoLine = indent + "/** TODO: ";

    ed->saveState();
    ed->lines->insert(ed->lines->begin() + endY + 1, indent + " */");
    ed->lines->insert(ed->lines->begin() + startY, todoLine);
    *ed->cursorY = startY;
    *ed->cursorX = static_cast<int>(todoLine.size());
    *ed->wantedX = *ed->cursorX;
    *ed->dirty = true;
    ed->currentBuffer->lspSyncNeeded = true;
    ed->needsFullRedraw = true;
    ed->saveState();
    return InsertMode{};
}
} // namespace

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
    if(origin == CommentLeaderOrigin::VisualLine)
        return insertVisualLineTodoLineComment(ctx);

    if(origin != CommentLeaderOrigin::Normal)
        return applyLineComment(ctx, origin);

    if(ctx.editor->insertTodoLineComment(*ctx.editor->cursorY))
        return InsertMode{};
    return std::nullopt;
}

std::optional<ModeState> applyTodoBlockComment(ModeContext& ctx,
                                               CommentLeaderOrigin origin)
{
    if(origin == CommentLeaderOrigin::VisualLine)
        return insertVisualLineTodoBlockComment(ctx);

    if(origin != CommentLeaderOrigin::Normal)
        return applyBlockComment(ctx, origin);

    if(ctx.editor->insertTodoBlockComment(*ctx.editor->cursorY))
        return InsertMode{};
    return std::nullopt;
}

std::optional<ModeState>
replaceNormalAppliedBlockWithTodoComment(ModeContext& ctx, int row, int column)
{
    Editor* ed = ctx.editor;
    if(!ed || !ed->currentBuffer)
        return std::nullopt;

    auto& bufferLines = ed->currentBuffer->lines;
    if(row < 0 || row + 2 >= static_cast<int>(bufferLines.size()))
        return std::nullopt;

    const std::string_view opening = trimAsciiView(bufferLines[row]);
    if((opening != "/*" && opening != "/**") ||
       trimAsciiView(bufferLines[row + 2]) != "*/")
    {
        return std::nullopt;
    }

    bufferLines.erase(bufferLines.begin() + row + 2);
    bufferLines.erase(bufferLines.begin() + row);
    *ed->cursorY = row;
    *ed->cursorX = std::max(0, column);

    if(ed->insertTodoBlockComment(row))
        return InsertMode{};
    return std::nullopt;
}
} // namespace editor::statemachine::commentleader
