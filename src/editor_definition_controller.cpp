#include "editor_definition_controller.h"

#include "ascii.h"
#include "asm_documentation.h"
#include "cpp_navigation_utilities.h"
#include "editor.h"
#include "stdlib_goto.h"
#include "text_utils.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
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

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(editor.isClangdLspEnabled() && editor.getFileType() == FileType::Cpp &&
       editor.lspClient)
    {
        editor.lspClient->didChange(editor.currentBuffer->filename,
                                    bufferText(), "cpp");
        int includeColumn = *editor.cursorX;
        const size_t includePos = currentLine.find(includePath);
        if(text_utils::is_found(includePos))
            includeColumn = static_cast<int>(includePos);
        auto loc = editor.lspClient->definition(
            editor.currentBuffer->filename, *editor.cursorY, includeColumn,
            currentLine);
        if(loc)
            return jumpToLocation(loc->path, loc->line, loc->character,
                                  "clangd");
    }
#endif

    std::string resolvedPath;
    if(isSystem)
    {
        resolvedPath =
            CppNavigationUtilities::resolveSystemInclude(includePath);
    }
    else
    {
        fs::path currentDir = ".";
        if(!editor.filename->empty())
        {
            fs::path currentPath(*editor.filename);
            if(currentPath.has_parent_path())
                currentDir = currentPath.parent_path();
        }

        std::error_code ec;
        std::vector<fs::path> candidates = {currentDir / includePath};
        if(!editor.projectRoot.empty())
        {
            fs::path root(editor.projectRoot);
            candidates.push_back(root / includePath);
            candidates.push_back(root / "include" / includePath);
            candidates.push_back(root / "src" / includePath);
        }

        for(const auto& candidate : candidates)
        {
            if(fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec))
            {
                resolvedPath = candidate.lexically_normal().string();
                break;
            }
            ec.clear();
        }
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

bool EditorDefinitionController::goToAsmDefinition()
{
#ifndef UVIM_ENABLE_ASM_DOCS
    editor.setStatusMessage("gd (asm): docs not compiled in");
    return true;
#else
    if(!editor.lines || !editor.cursorY || *editor.cursorY < 0 ||
       *editor.cursorY >= static_cast<int>(editor.lines->size()))
    {
        return false;
    }

    const std::string& line = (*editor.lines)[*editor.cursorY];
    std::optional<asm_documentation::Location> loc =
        asm_documentation::find(line, editor.cursorX ? *editor.cursorX : 0);
    if(!loc)
    {
        editor.setStatusMessage("gd (asm): instruction not found");
        return true;
    }

    editor.pushJumpLocation();
    editor.openFile(loc->path);
    *editor.cursorY = loc->line;
    *editor.cursorX = 0;
    clampCursor();
    applyViewport();
    const std::string gdArrow = ascii::utf8(ascii::RIGHT_ARROW_PADDED);
    editor.setStatusMessage("gd (asm " + loc->arch + ")" + gdArrow +
                            loc->mnemonic);
    return true;
#endif
}

namespace
{
std::string wrapTextLine(std::string_view line, int width)
{
    if(width <= 0 || text_utils::displayWidth(line) <= width)
        return std::string(line);

    std::string indent;
    size_t firstText = line.find_first_not_of(' ');
    if(text_utils::is_found(firstText))
        indent = std::string(line.substr(0, firstText));

    std::ostringstream out;
    std::string current = indent;
    size_t pos = firstText == std::string_view::npos ? 0 : firstText;
    bool first = true;

    while(pos < line.size())
    {
        while(pos < line.size() && text_utils::is_space(line[pos]))
            ++pos;
        size_t start = pos;
        while(pos < line.size() && !text_utils::is_space(line[pos]))
            ++pos;
        if(start == pos)
            break;

        std::string word(line.substr(start, pos - start));
        const int currentWidth = text_utils::displayWidth(current);
        const int wordWidth = text_utils::displayWidth(word);
        const int nextWidth = currentWidth + (current == indent ? 0 : 1) +
                              wordWidth;

        if(nextWidth > width && current != indent)
        {
            if(!first)
                out << '\n';
            out << current;
            current = indent + "  " + word;
            first = false;
        }
        else
        {
            if(current != indent)
                current.push_back(' ');
            current += word;
        }
    }

    if(!current.empty())
    {
        if(!first)
            out << '\n';
        out << current;
    }
    return out.str();
}

std::string wrapPopupText(std::string_view text, int width)
{
    std::ostringstream out;
    size_t start = 0;
    bool first = true;
    while(start <= text.size())
    {
        size_t end = text.find('\n', start);
        std::string_view line =
            end == std::string_view::npos
                ? text.substr(start)
                : text.substr(start, end - start);
        if(!first)
            out << '\n';
        out << wrapTextLine(line, width);
        first = false;
        if(end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return out.str();
}

std::string readAsmDocSection(const asm_documentation::Location& loc)
{
    std::ifstream in(loc.path);
    if(!in)
        return {};

    std::vector<std::string> lines;
    std::string line;
    while(std::getline(in, line))
        lines.push_back(std::move(line));
    if(lines.empty())
        return {};

    int start = std::clamp(loc.line, 0, (int)lines.size() - 1);
    int end = (int)lines.size();
    if(loc.line > 0)
    {
        for(int i = start + 1; i < (int)lines.size(); ++i)
        {
            if(lines[i].rfind("## ", 0) == 0)
            {
                end = i;
                break;
            }
        }
    }

    std::ostringstream out;
    for(int i = start; i < end; ++i)
    {
        if(i > start)
            out << '\n';
        out << lines[i];
    }
    return out.str();
}
} // namespace

void Editor::openAsmDocumentationPopupForCursor()
{
#ifndef UVIM_ENABLE_ASM_DOCS
    setStatusMessage("leader-ga: asm docs not compiled in");
    needsFullRedraw = true;
#else
    closeSymbolPopup();
    if(!currentBuffer || !lines || !cursorX || !cursorY)
        return;

    if(getFileType() != FileType::Asm)
    {
        setStatusMessage("leader-ga: assembly files only");
        needsFullRedraw = true;
        return;
    }
    if(*cursorY < 0 || *cursorY >= static_cast<int>(lines->size()))
        return;

    const std::string& line = (*lines)[*cursorY];
    std::optional<asm_documentation::Location> loc =
        asm_documentation::find(line, *cursorX);
    if(!loc)
    {
        setStatusMessage("leader-ga: instruction not found");
        needsFullRedraw = true;
        return;
    }

    std::string docs = readAsmDocSection(*loc);
    if(docs.empty())
    {
        setStatusMessage("leader-ga: documentation unavailable");
        needsFullRedraw = true;
        return;
    }

    const int popupWidth = std::max(48, std::min(96, screenCols - 8));
    symbolPopupText = wrapPopupText(docs, popupWidth);
    symbolPopupActive = true;
    symbolPopupModal = true;
    symbolPopupCursorX = *cursorX;
    symbolPopupCursorY = *cursorY;
    symbolPopupScroll = 0;
    setStatusMessage("leader-ga (asm " + loc->arch + "): " + loc->mnemonic);
    needsFullRedraw = true;
#endif
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

    const std::optional<FileType> fileType = editor.getFileType();
    if(fileType == FileType::Asm)
    {
        if(goToAsmDefinition())
            return;
    }

    std::string symbol = editor.getSymbolUnderCursor();
    if(symbol.empty())
    {
        editor.setStatusMessage("gd: no symbol");
        return;
    }

    if(goToStdSymbol(symbol))
        return;

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
