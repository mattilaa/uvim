#include "visual_text_object_pending.h"

#include "editor.h"
#include "key_enums.h"
#include "mode_context.h"
#include "mode_state_machine.h"
#include "terminal.h"

namespace editor::statemachine
{
namespace
{
bool isEscape(int key)
{
    return key == keyCode(control::ControlKey::ESC);
}

std::optional<ModeState>
applyTextObject(VisualTextObjectPendingMachine& machine, ModeContext& ctx,
                int objectKey)
{
    Editor* ed = ctx.editor;
    int startY = 0;
    int startX = 0;
    int endY = 0;
    int endX = 0;

    if(!ed->getTextObjectRange(static_cast<char>(objectKey),
                               machine.aroundObject(), startY, startX, endY,
                               endX))
    {
        ctx.setStatusMessage("No object found");
        ctx.repeatCount = 0;
        machine.finish();
        return std::nullopt;
    }

    ed->currentBuffer->visualStartY = startY;
    ed->currentBuffer->visualStartX = startX;
    ed->currentBuffer->visualEndY = endY;
    ed->currentBuffer->visualEndX = endX;
    *ed->cursorY = endY;
    *ed->cursorX = endX;
    ed->adjustViewport();
    ed->needsFullRedraw = true;

    ctx.commandBuffer.clear();
    ctx.setStatusMessage("");
    ctx.repeatCount = 0;
    machine.finish();
    return std::nullopt;
}
} // namespace

std::optional<ModeState> VisualTextObjectDelimiterDispatcher::dispatch(
    VisualTextObjectPendingMachine& machine, ModeContext& ctx,
    const ModeKeyEvent& event)
{
    const int key = event.key;
    if(isEscape(key))
    {
        machine.cancel(ctx);
        return std::nullopt;
    }

    switch(key)
    {
    case keyCode(command::CommandKey::KEY_LEFT_PAREN):
        machine.setLeftParen();
        return machine.redispatch(ctx, event);
    case keyCode(command::CommandKey::KEY_RIGHT_PAREN):
        machine.setRightParen();
        return machine.redispatch(ctx, event);
    case keyCode(command::CommandKey::KEY_LEFT_BRACKET):
        machine.setLeftBracket();
        return machine.redispatch(ctx, event);
    case keyCode(command::CommandKey::KEY_RIGHT_BRACKET):
        machine.setRightBracket();
        return machine.redispatch(ctx, event);
    case keyCode(command::CommandKey::KEY_LEFT_BRACE):
        machine.setLeftBrace();
        return machine.redispatch(ctx, event);
    case keyCode(command::CommandKey::KEY_RIGHT_BRACE):
        machine.setRightBrace();
        return machine.redispatch(ctx, event);
    case keyCode(command::CommandKey::KEY_DOUBLE_QUOTE):
        machine.setDoubleQuote();
        return machine.redispatch(ctx, event);
    case keyCode(command::CommandKey::KEY_APOSTROPHE):
        machine.setSingleQuote();
        return machine.redispatch(ctx, event);
    default:
        machine.cancel(ctx);
        Terminal::unreadKey(key);
        return std::nullopt;
    }
}

VisualTextObjectPendingMachine::VisualTextObjectPendingMachine(
    bool aroundObject)
    : state(aroundObject ? PendingState{VisualTextObjectAfterA{}}
                         : PendingState{VisualTextObjectAfterI{}}),
      around(aroundObject)
{
}

std::optional<ModeState>
VisualTextObjectPendingMachine::handle(ModeContext& ctx,
                                       const ModeKeyEvent& event)
{
    return std::visit([&](auto& activeState) -> std::optional<ModeState>
                      { return activeState.handle(*this, ctx, event); }, state);
}

bool VisualTextObjectPendingMachine::done() const
{
    return finished;
}

bool VisualTextObjectPendingMachine::aroundObject() const
{
    return around;
}

void VisualTextObjectPendingMachine::finish()
{
    finished = true;
}

void VisualTextObjectPendingMachine::cancel(ModeContext& ctx)
{
    ctx.commandBuffer.clear();
    ctx.setStatusMessage("");
    ctx.repeatCount = 0;
    finish();
}

std::optional<ModeState>
VisualTextObjectPendingMachine::redispatch(ModeContext& ctx,
                                           const ModeKeyEvent& event)
{
    return handle(ctx, event);
}

void VisualTextObjectPendingMachine::setLeftParen()
{
    state = VisualTextObjectLeftParen{};
}

void VisualTextObjectPendingMachine::setRightParen()
{
    state = VisualTextObjectRightParen{};
}

void VisualTextObjectPendingMachine::setLeftBracket()
{
    state = VisualTextObjectLeftBracket{};
}

void VisualTextObjectPendingMachine::setRightBracket()
{
    state = VisualTextObjectRightBracket{};
}

void VisualTextObjectPendingMachine::setLeftBrace()
{
    state = VisualTextObjectLeftBrace{};
}

void VisualTextObjectPendingMachine::setRightBrace()
{
    state = VisualTextObjectRightBrace{};
}

void VisualTextObjectPendingMachine::setDoubleQuote()
{
    state = VisualTextObjectDoubleQuote{};
}

void VisualTextObjectPendingMachine::setSingleQuote()
{
    state = VisualTextObjectSingleQuote{};
}

std::optional<ModeState>
VisualTextObjectAfterI::handle(VisualTextObjectPendingMachine& machine,
                               ModeContext& ctx, const ModeKeyEvent& event)
{
    return VisualTextObjectDelimiterDispatcher{}.dispatch(machine, ctx, event);
}

std::optional<ModeState>
VisualTextObjectAfterA::handle(VisualTextObjectPendingMachine& machine,
                               ModeContext& ctx, const ModeKeyEvent& event)
{
    return VisualTextObjectDelimiterDispatcher{}.dispatch(machine, ctx, event);
}

std::optional<ModeState>
VisualTextObjectLeftParen::handle(VisualTextObjectPendingMachine& machine,
                                  ModeContext& ctx, const ModeKeyEvent&)
{
    return applyTextObject(machine, ctx,
                           keyCode(command::CommandKey::KEY_LEFT_PAREN));
}

std::optional<ModeState>
VisualTextObjectRightParen::handle(VisualTextObjectPendingMachine& machine,
                                   ModeContext& ctx, const ModeKeyEvent&)
{
    return applyTextObject(machine, ctx,
                           keyCode(command::CommandKey::KEY_RIGHT_PAREN));
}

std::optional<ModeState>
VisualTextObjectLeftBracket::handle(VisualTextObjectPendingMachine& machine,
                                    ModeContext& ctx, const ModeKeyEvent&)
{
    return applyTextObject(machine, ctx,
                           keyCode(command::CommandKey::KEY_LEFT_BRACKET));
}

std::optional<ModeState>
VisualTextObjectRightBracket::handle(VisualTextObjectPendingMachine& machine,
                                     ModeContext& ctx, const ModeKeyEvent&)
{
    return applyTextObject(machine, ctx,
                           keyCode(command::CommandKey::KEY_RIGHT_BRACKET));
}

std::optional<ModeState>
VisualTextObjectLeftBrace::handle(VisualTextObjectPendingMachine& machine,
                                  ModeContext& ctx, const ModeKeyEvent&)
{
    return applyTextObject(machine, ctx,
                           keyCode(command::CommandKey::KEY_LEFT_BRACE));
}

std::optional<ModeState>
VisualTextObjectRightBrace::handle(VisualTextObjectPendingMachine& machine,
                                   ModeContext& ctx, const ModeKeyEvent&)
{
    return applyTextObject(machine, ctx,
                           keyCode(command::CommandKey::KEY_RIGHT_BRACE));
}

std::optional<ModeState>
VisualTextObjectDoubleQuote::handle(VisualTextObjectPendingMachine& machine,
                                    ModeContext& ctx, const ModeKeyEvent&)
{
    return applyTextObject(machine, ctx,
                           keyCode(command::CommandKey::KEY_DOUBLE_QUOTE));
}

std::optional<ModeState>
VisualTextObjectSingleQuote::handle(VisualTextObjectPendingMachine& machine,
                                    ModeContext& ctx, const ModeKeyEvent&)
{
    return applyTextObject(machine, ctx,
                           keyCode(command::CommandKey::KEY_APOSTROPHE));
}
} // namespace editor::statemachine
