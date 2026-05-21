#pragma once

#include "mode_state.h"

#include <optional>
#include <variant>

namespace editor::statemachine
{
enum class CommentLeaderOrigin
{
    Normal,
    Visual,
    VisualLine,
};

class CommentLeaderPendingMachine;

struct CommentLeaderAfterC
{
    std::optional<ModeState> handle(CommentLeaderPendingMachine& machine,
                                    ModeContext& ctx, int key);
};

struct CommentLeaderAfterCi
{
    std::optional<ModeState> handle(CommentLeaderPendingMachine& machine,
                                    ModeContext& ctx, int key);
};

struct CommentLeaderAfterCiApplied
{
    std::optional<ModeState> handle(CommentLeaderPendingMachine& machine,
                                    ModeContext& ctx, int key);
};

struct CommentLeaderAfterCii
{
    std::optional<ModeState> handle(CommentLeaderPendingMachine& machine,
                                    ModeContext& ctx, int key);
};

struct CommentLeaderAfterCiiApplied
{
    std::optional<ModeState> handle(CommentLeaderPendingMachine& machine,
                                    ModeContext& ctx, int key);
};

class CommentLeaderPendingMachine
{
public:
    explicit CommentLeaderPendingMachine(CommentLeaderOrigin origin);

    std::optional<ModeState> handle(ModeContext& ctx, int key);

    bool done() const;
    CommentLeaderOrigin origin() const;

    void finish();
    void setAfterCi(ModeContext& ctx);
    void setAfterCiApplied(ModeContext& ctx);
    void setAfterCii(ModeContext& ctx);
    void setAfterCiiApplied(ModeContext& ctx);

private:
    using PendingState =
        std::variant<CommentLeaderAfterC, CommentLeaderAfterCi,
                     CommentLeaderAfterCiApplied, CommentLeaderAfterCii,
                     CommentLeaderAfterCiiApplied>;

    PendingState state;
    CommentLeaderOrigin source;
    bool finished = false;
};
} // namespace editor::statemachine
