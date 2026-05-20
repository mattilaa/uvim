#include "editor_definition_controller.h"

#include "ascii.h"
#include "constants.h"
#include "editor.h"
#include "editor_utils.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif

#include <cctype>
#include <filesystem>
#include <system_error>

using editor::helper::find_python_def_in_file;
using editor::helper::is_skip_dir;

bool EditorDefinitionController::goToPythonDefinition(const std::string& symbol)
{
    const std::string gdArrow = ascii::utf8(ascii::RIGHT_ARROW_PADDED);

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(editor.isPythonLspEnabled() && editor.pythonLspClient)
    {
        editor.pythonLspClient->didChange(editor.currentBuffer->filename,
                                          bufferText(), "python");
        int lspX = *editor.cursorX;
        if(*editor.cursorY >= 0 &&
           *editor.cursorY < static_cast<int>(editor.lines->size()))
        {
            const std::string& line = (*editor.lines)[*editor.cursorY];
            auto isSym = [](char ch)
            {
                unsigned char u = static_cast<unsigned char>(ch);
                return std::isalnum(u) || ch == '_';
            };

            if(!line.empty())
            {
                if(lspX >= static_cast<int>(line.size()))
                    lspX = static_cast<int>(line.size()) - 1;
                if(lspX < 0)
                    lspX = 0;

                if(line[lspX] == '.' &&
                   lspX + 1 < static_cast<int>(line.size()) &&
                   isSym(line[lspX + 1]))
                    ++lspX;
                else if(!isSym(line[lspX]))
                {
                    if(lspX > 0 && isSym(line[lspX - 1]))
                        --lspX;
                    else if(lspX + 1 < static_cast<int>(line.size()) &&
                            isSym(line[lspX + 1]))
                        ++lspX;
                }
            }
        }

        std::string_view lineForLsp;
        if(*editor.cursorY >= 0 &&
           *editor.cursorY < static_cast<int>(editor.lines->size()))
            lineForLsp = (*editor.lines)[*editor.cursorY];

        auto loc = editor.pythonLspClient->definition(
            editor.currentBuffer->filename, *editor.cursorY, lspX, lineForLsp);
        if(!loc)
            loc = editor.pythonLspClient->declaration(
                editor.currentBuffer->filename, *editor.cursorY, lspX,
                lineForLsp);
        if(!loc)
            loc = editor.pythonLspClient->typeDefinition(
                editor.currentBuffer->filename, *editor.cursorY, lspX,
                lineForLsp);
        if(loc)
            return jumpToLocation(loc->path, loc->line, loc->character,
                                  "python");
    }
#endif

    if(symbol.empty())
    {
        editor.setStatusMessage("gd (python): no symbol");
        return true;
    }

    int defY = -1;
    int defX = 0;
    if(find_python_def_in_file(editor.currentBuffer->filename, symbol, defY,
                               defX))
    {
        *editor.cursorY = defY;
        *editor.cursorX = defX;
        applyViewport();
        editor.setStatusMessage(std::string("gd (python)") + gdArrow +
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
        if(!constants::is_filetype<constants::no_pattern,
                                   constants::python_suffixes>(p.string()))
            continue;
        if(find_python_def_in_file(p.string(), symbol, defY, defX))
        {
            editor.pushJumpLocation();
            editor.openFile(p.string());
            *editor.cursorY = defY;
            *editor.cursorX = defX;
            applyViewport();
            editor.setStatusMessage(std::string("gd (python)") + gdArrow +
                                    p.string() + ":" +
                                    std::to_string(defY + 1));
            return true;
        }
    }

    editor.setStatusMessage("gd (python): not found");
    return true;
}
