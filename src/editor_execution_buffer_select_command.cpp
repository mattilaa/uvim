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

bool BufferSelectCommand::execute(Editor& editor,
                                  const CommandRequest& request) const
{
    if(request.text.rfind("b ", 0) != 0 &&
       request.text.rfind("buffer ", 0) != 0)
        return false;
    std::string_view arg = request.text.rfind("b ", 0) == 0
                               ? std::string_view(request.text).substr(2)
                               : std::string_view(request.text).substr(7);
    arg = trim_view(arg);

    int bufNum = 0;
    if(parse_int(arg, bufNum))
    {
        bufNum -= 1;
        if(bufNum >= 0 && bufNum < (int)editor.buffers.size())
            editor.switchToBuffer(bufNum);
        else
            editor.setStatusMessage("Buffer " + std::string(arg) +
                                    " does not exist");
        return true;
    }

    std::string needle(arg);
    for(size_t i = 0; i < editor.buffers.size(); i++)
    {
        if(editor.buffers[i] &&
           editor.buffers[i]->filename.find(needle) != std::string::npos)
        {
            editor.switchToBuffer((int)i);
            return true;
        }
    }
    editor.setStatusMessage("No matching buffer for " + needle);
    return true;
}
} // namespace command::execution
