#include "editor_definition_controller.h"

#include "ascii.h"
#include "editor.h"
#include "editor_utils.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif

#include <filesystem>
#include <system_error>

using editor::helper::find_robot_keyword_in_file;
using editor::helper::is_skip_dir;
using editor::helper::robot_first_cell;

bool EditorDefinitionController::goToRobotDefinition()
{
    const std::string gdArrow = ascii::utf8(ascii::RIGHT_ARROW_PADDED);

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(editor.isRobotLspEnabled() && editor.robotLspClient)
    {
        editor.robotLspClient->didChange(editor.currentBuffer->filename,
                                         bufferText(), "robotframework");
        auto loc = editor.robotLspClient->definition(
            editor.currentBuffer->filename, *editor.cursorY, *editor.cursorX);
        if(loc)
            return jumpToLocation(loc->path, loc->line, loc->character,
                                  "robot");
    }
#endif

    std::string_view lineView;
    if(*editor.cursorY >= 0 &&
       *editor.cursorY < static_cast<int>(editor.lines->size()))
        lineView = (*editor.lines)[*editor.cursorY];
    std::string_view keyword = robot_first_cell(lineView);
    if(keyword.empty())
    {
        editor.setStatusMessage("gd (robot): no keyword");
        return true;
    }

    int defY = -1;
    int defX = 0;
    if(find_robot_keyword_in_file(editor.currentBuffer->filename, keyword, defY,
                                  defX))
    {
        *editor.cursorY = defY;
        *editor.cursorX = defX;
        applyViewport();
        editor.setStatusMessage(std::string("gd (robot)") + gdArrow +
                                *editor.filename + ":" +
                                std::to_string(defY + 1));
        return true;
    }

    std::filesystem::path root = std::filesystem::current_path();
    std::error_code ec;
    for(std::filesystem::recursive_directory_iterator
            it(root, std::filesystem::directory_options::skip_permission_denied,
               ec),
        end;
        it != end; ++it)
    {
        if(it->is_directory(ec) && is_skip_dir(it->path()))
        {
            it.disable_recursion_pending();
            continue;
        }
        if(!it->is_regular_file(ec))
            continue;
        const auto& p = it->path();
        std::string ext = p.extension().string();
        if(ext != ".robot" && ext != ".resource" && ext != ".robotframework")
            continue;
        if(find_robot_keyword_in_file(p.string(), keyword, defY, defX))
        {
            editor.pushJumpLocation();
            editor.openFile(p.string());
            *editor.cursorY = defY;
            *editor.cursorX = defX;
            applyViewport();
            editor.setStatusMessage(std::string("gd (robot)") + gdArrow +
                                    p.string() + ":" +
                                    std::to_string(defY + 1));
            return true;
        }
    }

    editor.setStatusMessage("gd (robot): not found");
    return true;
}
