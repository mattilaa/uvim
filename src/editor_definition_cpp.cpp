#include "editor_definition_controller.h"

#include "ascii.h"
#include "cpp_navigation_utilities.h"
#include "editor.h"
#include "editor_utils.h"
#include "text_utils.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif

bool EditorDefinitionController::goToCppDefinition(const std::string& symbol)
{
    const std::string gdArrow = ascii::utf8(ascii::RIGHT_ARROW_PADDED);

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(editor.isClangdLspEnabled() && editor.getFileType() == FileType::Cpp &&
       editor.lspClient)
    {
        const bool clangdIndexing = editor.lspClient->indexingInProgress();
        if(!clangdIndexing)
        {
            size_t currentHash = editor::helper::hash_lines(*editor.lines);
            if(!editor.currentBuffer->lspHashValid ||
               editor.currentBuffer->lspSyncNeeded || *editor.dirty ||
               currentHash != editor.currentBuffer->lspContentHash)
            {
                editor.lspClient->didChange(editor.currentBuffer->filename,
                                            bufferText(), "cpp");
                editor.currentBuffer->lspContentHash = currentHash;
                editor.currentBuffer->lspHashValid = true;
                editor.currentBuffer->lspSyncNeeded = false;
            }

            std::string_view lineText;
            if(*editor.cursorY >= 0 &&
               *editor.cursorY < static_cast<int>(editor.lines->size()))
            {
                lineText = (*editor.lines)[*editor.cursorY];
            }
            auto loc = editor.lspClient->definition(
                editor.currentBuffer->filename, *editor.cursorY,
                *editor.cursorX, lineText);
            if(loc)
            {
                editor.pushJumpLocation();
                editor.openFile(loc->path);
                *editor.cursorY = loc->line;
                *editor.cursorX = loc->character;
                clampCursor();
                applyViewport();

                std::string displayPath = loc->path;
                bool isSystemHeader = loc->path.find("/usr/") == 0 ||
                                      loc->path.find("/opt/") == 0 ||
                                      loc->path.find("/Library/") == 0 ||
                                      loc->path.find("/Applications/") == 0;
                if(isSystemHeader)
                {
                    displayPath = "<sys>/" +
                                  std::string(text_utils::basename(loc->path));
                }
                editor.setStatusMessage(std::string("gd (clangd)") + gdArrow +
                                        displayPath + ":" +
                                        std::to_string(loc->line + 1));
                return true;
            }
        }
    }
#endif

    editor.pushJumpLocation();

    int y = 0;
    int x = 0;
    std::string current = editor.currentBuffer->filename;
    std::string alternate = editor.findAlternateFile(current);

    if(CppNavigationUtilities::searchLocalDefinition(
           *editor.lines, symbol, *editor.cursorY, *editor.cursorX, y, x))
    {
        if(y != *editor.cursorY || x != *editor.cursorX)
        {
            *editor.cursorY = y;
            *editor.cursorX = x;
            applyViewport();
            editor.setStatusMessage(std::string("gd") + gdArrow + "local '" +
                                    symbol + "' at " + std::to_string(y + 1) +
                                    ":" + std::to_string(x + 1));
            return true;
        }
    }

    if(CppNavigationUtilities::searchMemberDefinition(*editor.lines, symbol, y,
                                                      x))
    {
        if(y != *editor.cursorY || x != *editor.cursorX)
        {
            *editor.cursorY = y;
            *editor.cursorX = x;
            applyViewport();
            editor.setStatusMessage(std::string("gd") + gdArrow + "member '" +
                                    symbol + "' at " + std::to_string(y + 1) +
                                    ":" + std::to_string(x + 1));
            return true;
        }
    }

    if(!alternate.empty())
    {
        editor.openFile(alternate);

        if(editor.searchDefinitionInBuffer(editor.currentBuffer, symbol, y, x))
        {
            *editor.cursorY = y;
            *editor.cursorX = x;
            applyViewport();
            editor.setStatusMessage(std::string("gd") + gdArrow + alternate);
            return true;
        }

        editor.openFile(current);
    }

    if(editor.searchDefinitionInBuffer(editor.currentBuffer, symbol, y, x))
    {
        *editor.cursorY = y;
        *editor.cursorX = x;
        applyViewport();
        editor.setStatusMessage("gd (same file)");
        return true;
    }

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(editor.isClangdLspEnabled() && editor.getFileType() == FileType::Cpp &&
       editor.lspClient && editor.lspClient->indexingInProgress())
    {
        std::string detail = editor.lspClient->indexingStatus();
        if(detail.empty())
            detail = "in progress";
        editor.setStatusMessage("gd: clangd indexing (" + detail +
                                "), try again");
        return true;
    }
#endif

    return false;
}
