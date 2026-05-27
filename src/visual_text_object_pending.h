#pragma once

#include "mode_state.h"

#include <optional>
#include <variant>

namespace editor::statemachine
{
class VisualTextObjectPendingMachine;

struct VisualTextObjectDelimiterDispatcher
{
    std::optional<ModeState> dispatch(VisualTextObjectPendingMachine& machine,
                                      ModeContext& ctx,
                                      const ModeKeyEvent& event);
};

struct VisualTextObjectAfterI
{
    std::optional<ModeState> handle(VisualTextObjectPendingMachine& machine,
                                    ModeContext& ctx,
                                    const ModeKeyEvent& event);
};

struct VisualTextObjectAfterA
{
    std::optional<ModeState> handle(VisualTextObjectPendingMachine& machine,
                                    ModeContext& ctx,
                                    const ModeKeyEvent& event);
};

struct VisualTextObjectLeftParen
{
    std::optional<ModeState> handle(VisualTextObjectPendingMachine& machine,
                                    ModeContext& ctx,
                                    const ModeKeyEvent& event);
};

struct VisualTextObjectRightParen
{
    std::optional<ModeState> handle(VisualTextObjectPendingMachine& machine,
                                    ModeContext& ctx,
                                    const ModeKeyEvent& event);
};

struct VisualTextObjectLeftBracket
{
    std::optional<ModeState> handle(VisualTextObjectPendingMachine& machine,
                                    ModeContext& ctx,
                                    const ModeKeyEvent& event);
};

struct VisualTextObjectRightBracket
{
    std::optional<ModeState> handle(VisualTextObjectPendingMachine& machine,
                                    ModeContext& ctx,
                                    const ModeKeyEvent& event);
};

struct VisualTextObjectLeftBrace
{
    std::optional<ModeState> handle(VisualTextObjectPendingMachine& machine,
                                    ModeContext& ctx,
                                    const ModeKeyEvent& event);
};

struct VisualTextObjectRightBrace
{
    std::optional<ModeState> handle(VisualTextObjectPendingMachine& machine,
                                    ModeContext& ctx,
                                    const ModeKeyEvent& event);
};

struct VisualTextObjectDoubleQuote
{
    std::optional<ModeState> handle(VisualTextObjectPendingMachine& machine,
                                    ModeContext& ctx,
                                    const ModeKeyEvent& event);
};

struct VisualTextObjectSingleQuote
{
    std::optional<ModeState> handle(VisualTextObjectPendingMachine& machine,
                                    ModeContext& ctx,
                                    const ModeKeyEvent& event);
};

class VisualTextObjectPendingMachine
{
public:
    explicit VisualTextObjectPendingMachine(bool around);

    std::optional<ModeState> handle(ModeContext& ctx,
                                    const ModeKeyEvent& event);

    bool done() const;
    bool aroundObject() const;

    void finish();
    void cancel(ModeContext& ctx);
    std::optional<ModeState> redispatch(ModeContext& ctx,
                                        const ModeKeyEvent& event);
    void setLeftParen();
    void setRightParen();
    void setLeftBracket();
    void setRightBracket();
    void setLeftBrace();
    void setRightBrace();
    void setDoubleQuote();
    void setSingleQuote();

private:
    using PendingState =
        std::variant<VisualTextObjectAfterI, VisualTextObjectAfterA,
                     VisualTextObjectLeftParen, VisualTextObjectRightParen,
                     VisualTextObjectLeftBracket, VisualTextObjectRightBracket,
                     VisualTextObjectLeftBrace, VisualTextObjectRightBrace,
                     VisualTextObjectDoubleQuote, VisualTextObjectSingleQuote>;

    PendingState state;
    bool around = false;
    bool finished = false;
};
} // namespace editor::statemachine
