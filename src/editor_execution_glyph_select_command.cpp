#include "editor.h"
#include "editor_execution_commands.h"
#include "glyph_select_mode.h"
#include "mode_state_machine.h"

namespace command::execution
{
bool GlyphSelectCommand::execute(Editor& editor,
                                 const CommandRequest& request) const
{
    if(request.text != "glyphselect")
        return false;

    if(!editor.hasBuffer())
    {
        editor.setStatusMessage("glyphselect: no buffer");
        return true;
    }

    if(auto* stateMachine = editor.getModeStateMachine())
        stateMachine->transitionTo(editor::statemachine::GlyphSelectMode{});

    editor.needsFullRedraw = true;
    return true;
}
} // namespace command::execution
