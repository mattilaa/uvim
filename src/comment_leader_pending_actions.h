#pragma once

#include "comment_leader_pending.h"

namespace editor::statemachine::commentleader
{
bool isEscape(int key);
bool isEnter(int key);

std::optional<ModeState> applyLineComment(ModeContext& ctx,
                                          CommentLeaderOrigin origin);
void toggleNormalLineComment(ModeContext& ctx);

std::optional<ModeState> applyBlockComment(ModeContext& ctx,
                                           CommentLeaderOrigin origin);
void toggleNormalBlockComment(ModeContext& ctx);

std::optional<ModeState> applyTodoLineComment(ModeContext& ctx,
                                              CommentLeaderOrigin origin);
std::optional<ModeState> applyTodoBlockComment(ModeContext& ctx,
                                               CommentLeaderOrigin origin);
} // namespace editor::statemachine::commentleader
