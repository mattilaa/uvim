#include "editor.h"
#include "editor_execution_command_helpers.h"
#include "editor_execution_commands.h"
#include "editor_path_utilities.h"
#include "editor_utils.h"
#include "file_browser_mode.h"
#include "gitignore.h"
#include "mode_state_machine.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace command::execution
{
using editor::helper::collectLocFiles;
using editor::helper::expandTildePath;
using editor::helper::locCommentRulesForPath;
using editor::helper::locCountInFile;
using editor::helper::locCountInLines;
using editor::helper::locIsTextFile;
using editor::helper::parse_int;
using editor::helper::trim_view;
using namespace detail;

bool SplitExplorerCommand::execute(Editor& editor,
                                   const CommandRequest& request) const
{
    std::string browsePath = ".";
    editor::statemachine::FileBrowserMode* browser = nullptr;
    if(editor.getModeStateMachine())
    {
        browser = editor.getModeStateMachine()
                      ->getState<editor::statemachine::FileBrowserMode>();
        if(browser)
        {
            if(!browser->currentDirectory.empty())
                browsePath = browser->currentDirectory;
        }
    }
    if(request.text != "se" && request.text != "ve")
        return false;

    const bool vertical = request.text == "ve";

    const bool hasNamedBuffer =
        editor.currentBuffer && !editor.currentBuffer->filename.empty();
    if(!hasNamedBuffer)
    {
        requestMode(editor, FILE_BROWSER, browsePath);
        return true;
    }

    editor.enableSplit(vertical);
    if(editor.splitActive)
        editor.switchPaneDirection(vertical ? 1 : 0, vertical ? 0 : 1);
    requestMode(editor, FILE_BROWSER, browsePath);
    return true;
}
} // namespace command::execution
