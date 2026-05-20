#include "editor_definition_controller.h"

#include "ascii.h"
#include "cpp_navigation_utilities.h"
#include "editor.h"
#include "stdlib_goto.h"
#include "text_utils.h"

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

EditorDefinitionController::EditorDefinitionController(Editor& editor)
    : editor(editor)
{
}

void EditorDefinitionController::applyViewport()
{
    if(editor.gdCenterScreen)
        editor.centerScreen();
    else
        editor.adjustViewport();
}

void EditorDefinitionController::clampCursor()
{
    if(!editor.lines || !editor.cursorY || !editor.cursorX)
        return;
    if(*editor.cursorY >= static_cast<int>(editor.lines->size()))
        *editor.cursorY = editor.lines->empty() ? 0 : editor.lines->size() - 1;
    if(*editor.cursorY >= 0 &&
       *editor.cursorX >
           static_cast<int>((*editor.lines)[*editor.cursorY].size()))
    {
        *editor.cursorX = (*editor.lines)[*editor.cursorY].size();
    }
}

std::string EditorDefinitionController::bufferText() const
{
    std::string text;
    if(!editor.lines)
        return text;
    text.reserve(editor.lines->size() * 80);
    for(size_t i = 0; i < editor.lines->size(); ++i)
    {
        text += (*editor.lines)[i];
        if(i + 1 < editor.lines->size())
            text.push_back('\n');
    }
    return text;
}

bool EditorDefinitionController::jumpToLocation(const std::string& path,
                                                int line, int character,
                                                std::string_view label)
{
    if(!editor.cursorY || !editor.cursorX)
        return false;

    const std::string gdArrow = ascii::utf8(ascii::RIGHT_ARROW_PADDED);
    editor.pushJumpLocation();
    editor.openFile(path);
    *editor.cursorY = line;
    *editor.cursorX = character;
    clampCursor();
    applyViewport();
    editor.setStatusMessage("gd (" + std::string(label) + ")" + gdArrow + path +
                            ":" + std::to_string(line + 1));
    return true;
}

bool EditorDefinitionController::goToInclude()
{
    if(!editor.lines || !editor.cursorY || !editor.filename ||
       *editor.cursorY < 0 ||
       *editor.cursorY >= static_cast<int>(editor.lines->size()))
    {
        return false;
    }

    const std::string& currentLine = (*editor.lines)[*editor.cursorY];
    auto [includePath, isSystem] =
        CppNavigationUtilities::extractIncludePath(currentLine);
    if(includePath.empty())
        return false;

    std::string resolvedPath;
    if(isSystem)
    {
        resolvedPath =
            CppNavigationUtilities::resolveSystemInclude(includePath);
    }
    else
    {
        std::string currentDir = ".";
        if(!editor.filename->empty())
        {
            size_t lastSlash = editor.filename->rfind('/');
            if(text_utils::is_found(lastSlash))
                currentDir = editor.filename->substr(0, lastSlash);
        }

        std::string tryPath = currentDir + "/" + includePath;
        std::error_code ec;
        if(fs::exists(tryPath, ec) && !ec)
            resolvedPath = tryPath;
    }

    if(resolvedPath.empty())
    {
        editor.setStatusMessage("gd: include file not found: " + includePath);
        return true;
    }

    const std::string gdArrow = ascii::utf8(ascii::RIGHT_ARROW_PADDED);
    editor.pushJumpLocation();
    editor.openFile(resolvedPath);

    std::string displayPath = resolvedPath;
    if(isSystem && resolvedPath.length() > 50)
    {
        displayPath = ".../" + std::string(text_utils::basename(resolvedPath));
    }
    editor.setStatusMessage(std::string("gd") + gdArrow + displayPath);
    return true;
}

bool EditorDefinitionController::goToStdSymbol(const std::string& symbol)
{
    if(!editor.lines || !editor.cursorY || !editor.cursorX ||
       !editor.filename || editor.isFileType(FileType::Mla))
    {
        return false;
    }

    bool isStdSymbol = false;
    if(*editor.cursorY >= 0 &&
       *editor.cursorY < static_cast<int>(editor.lines->size()))
    {
        const std::string& line = (*editor.lines)[*editor.cursorY];
        int x = *editor.cursorX;
        if(x >= 0 && x < static_cast<int>(line.size()) &&
           CppNavigationUtilities::isIdent(line[x]))
        {
            int l = x;
            while(l > 0 && CppNavigationUtilities::isIdent(line[l - 1]))
                --l;
            isStdSymbol = l >= 5 && line.compare(l - 5, 5, "std::") == 0;
        }
    }

    std::string headerName;
    if(isStdSymbol || editor.symbolPrefix.rfind("std::", 0) == 0)
        headerName = stdlib_goto::headerForSymbol(symbol);
    else if(isStdSymbol)
        headerName = symbol;

    if(headerName.empty())
        return false;

    std::string header =
        CppNavigationUtilities::resolveSystemInclude(headerName);
    if(header.empty())
        return false;

    const std::string gdArrow = ascii::utf8(ascii::RIGHT_ARROW_PADDED);
    editor.pushJumpLocation();
    editor.openFile(header);
    editor.setStatusMessage(std::string("gd") + gdArrow + "<sys>/" +
                            headerName);
    return true;
}

void EditorDefinitionController::goToDefinition()
{
    if(!editor.currentBuffer || !editor.lines || !editor.cursorY ||
       !editor.cursorX)
    {
        editor.setStatusMessage("gd: no buffer");
        return;
    }

    if(goToInclude())
        return;

    std::string symbol = editor.getSymbolUnderCursor();
    if(symbol.empty())
    {
        editor.setStatusMessage("gd: no symbol");
        return;
    }

    if(goToStdSymbol(symbol))
        return;

    const std::optional<FileType> fileType = editor.getFileType();
    if(fileType == FileType::Robot)
    {
        if(goToRobotDefinition())
            return;
    }
    else if(fileType == FileType::Python)
    {
        if(goToPythonDefinition(symbol))
            return;
    }
    else if(fileType == FileType::Html || fileType == FileType::Css ||
            fileType == FileType::Json || fileType == FileType::JavaScript ||
            fileType == FileType::TypeScript)
    {
        if(goToWebDefinition(*fileType, symbol))
            return;
    }
    else if(fileType == FileType::Mla)
    {
        if(goToMlangDefinition(symbol))
            return;
    }

    if(goToCppDefinition(symbol))
        return;

    editor.setStatusMessage("gd: '" + symbol + "' not found (curY=" +
                            std::to_string(*editor.cursorY) +
                            " curX=" + std::to_string(*editor.cursorX) + ")");
}

void Editor::goToDefinition()
{
    definitionController->goToDefinition();
}
