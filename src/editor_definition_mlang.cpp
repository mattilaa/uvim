#include "editor_definition_controller.h"

#include "ascii.h"
#include "cpp_navigation_utilities.h"
#include "editor.h"
#include "editor_utils.h"
#include "mlang_utilities.h"
#include "syntax_highlighter.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif

#include <cctype>
#include <vector>

using editor::helper::ascii_lower;

namespace
{
bool is_mlang_symbol(char ch)
{
    unsigned char u = static_cast<unsigned char>(ch);
    return std::isalnum(u) || ch == '_';
}

std::vector<int> mlang_lsp_query_columns(const std::string& line, int cursorX)
{
    std::vector<int> columns;
    auto push = [&](int x)
    {
        if(x < 0 || x >= static_cast<int>(line.size()))
            return;
        for(int existing : columns)
            if(existing == x)
                return;
        columns.push_back(x);
    };

    int lspX = cursorX;
    if(line.empty())
    {
        push(lspX);
        return columns;
    }

    if(lspX >= static_cast<int>(line.size()))
        lspX = static_cast<int>(line.size()) - 1;
    if(lspX < 0)
        lspX = 0;

    if(is_mlang_symbol(line[lspX]))
    {
        int start = lspX;
        int end = lspX;
        while(start > 0 && is_mlang_symbol(line[start - 1]))
            --start;
        while(end + 1 < static_cast<int>(line.size()) &&
              is_mlang_symbol(line[end + 1]))
            ++end;
        int p = end + 1;
        while(p < static_cast<int>(line.size()) &&
              std::isspace(static_cast<unsigned char>(line[p])))
            ++p;
        int sepPos = p;
        bool hasArrow = p + 1 < static_cast<int>(line.size()) &&
                        line[p] == '-' && line[p + 1] == '>';
        bool hasDot =
            !hasArrow && p < static_cast<int>(line.size()) && line[p] == '.';
        if(hasArrow)
            p += 2;
        else if(hasDot)
            ++p;
        while(p < static_cast<int>(line.size()) &&
              std::isspace(static_cast<unsigned char>(line[p])))
            ++p;
        if((hasArrow || hasDot) && cursorX >= sepPos &&
           p < static_cast<int>(line.size()) && is_mlang_symbol(line[p]))
            lspX = p;
    }

    if(!is_mlang_symbol(line[lspX]))
    {
        if(line[lspX] == '.' || line[lspX] == ':')
        {
            int right = lspX + 1;
            while(right < static_cast<int>(line.size()) &&
                  !is_mlang_symbol(line[right]))
                ++right;
            if(right < static_cast<int>(line.size()) &&
               is_mlang_symbol(line[right]))
                lspX = right;
        }
        int left = lspX - 1;
        while(left >= 0 && !is_mlang_symbol(line[left]))
            --left;
        int right = lspX + 1;
        while(right < static_cast<int>(line.size()) &&
              !is_mlang_symbol(line[right]))
            ++right;
        if(left >= 0 && is_mlang_symbol(line[left]))
            lspX = left;
        else if(right < static_cast<int>(line.size()) &&
                is_mlang_symbol(line[right]))
            lspX = right;
    }

    push(lspX);
    push(cursorX);

    if(is_mlang_symbol(line[lspX]))
    {
        int start = lspX;
        int end = lspX;
        while(start > 0 && is_mlang_symbol(line[start - 1]))
            --start;
        while(end + 1 < static_cast<int>(line.size()) &&
              is_mlang_symbol(line[end + 1]))
            ++end;
        push(start);
        push(end);

        int p = end + 1;
        while(p < static_cast<int>(line.size()) &&
              std::isspace(static_cast<unsigned char>(line[p])))
            ++p;
        int sepPos = p;
        bool sawMemberSep = false;
        if(p + 1 < static_cast<int>(line.size()) && line[p] == '-' &&
           line[p + 1] == '>')
        {
            sawMemberSep = true;
            p += 2;
        }
        else if(p < static_cast<int>(line.size()) && line[p] == '.')
        {
            sawMemberSep = true;
            ++p;
        }
        while(p < static_cast<int>(line.size()) &&
              std::isspace(static_cast<unsigned char>(line[p])))
            ++p;
        if(sawMemberSep && cursorX > end && cursorX >= sepPos &&
           p < static_cast<int>(line.size()) && is_mlang_symbol(line[p]))
        {
            push(p);
            int rhsEnd = p;
            while(rhsEnd + 1 < static_cast<int>(line.size()) &&
                  is_mlang_symbol(line[rhsEnd + 1]))
                ++rhsEnd;
            push(rhsEnd);
        }
    }

    return columns;
}
} // namespace

bool EditorDefinitionController::goToMlangDefinition(const std::string& symbol)
{
    const std::string gdArrow = ascii::utf8(ascii::RIGHT_ARROW_PADDED);

    if(*editor.cursorY >= 0 &&
       *editor.cursorY < static_cast<int>(editor.lines->size()))
    {
        const std::string& line = (*editor.lines)[*editor.cursorY];
        std::string modulePath;
        if(MlangUtilities::moduleDeclUnderCursor(line, *editor.cursorX,
                                                 modulePath))
        {
            std::string moduleFile;
            if(MlangUtilities::resolveModuleFile(
                   modulePath, editor.currentBuffer->filename, moduleFile))
            {
                editor.pushJumpLocation();
                editor.openFile(moduleFile);
                *editor.cursorY = 0;
                *editor.cursorX = 0;
                applyViewport();
                editor.setStatusMessage(std::string("gd (mlang mod)") +
                                        gdArrow + moduleFile);
                return true;
            }
        }
    }

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(editor.isMlangLspEnabled() && editor.mlangLspClient)
    {
        std::vector<int> queryXs;
        if(*editor.cursorY >= 0 &&
           *editor.cursorY < static_cast<int>(editor.lines->size()))
        {
            queryXs = mlang_lsp_query_columns((*editor.lines)[*editor.cursorY],
                                              *editor.cursorX);
        }
        if(queryXs.empty())
            queryXs.push_back(*editor.cursorX);

        editor.mlangLspClient->didChange(editor.currentBuffer->filename,
                                         bufferText(), "mlang");
        for(int queryX : queryXs)
        {
            auto loc = editor.mlangLspClient->definition(
                editor.currentBuffer->filename, *editor.cursorY, queryX);
            if(loc)
                return jumpToLocation(loc->path, loc->line, loc->character,
                                      "mlang");
        }
    }
#endif

    if(editor.syntaxHighlighter)
        editor.syntaxHighlighter->ensureMlangTokensLoaded();
    if(editor.mlangTokenCache)
    {
        std::string key = editor.mlangTokenCache->caseInsensitive
                              ? ascii_lower(symbol)
                              : symbol;
        auto jumpBuiltin = [&](const auto& hit, std::string_view label)
        {
            editor.pushJumpLocation();
            editor.openFile(hit.path);
            *editor.cursorY = hit.line;
            *editor.cursorX = 0;
            clampCursor();
            applyViewport();
            editor.setStatusMessage("gd (" + std::string(label) + ")" +
                                    gdArrow + hit.path + ":" +
                                    std::to_string(hit.line + 1));
            return true;
        };

        auto typeIt = editor.mlangTokenCache->builtinTypes.find(key);
        if(typeIt != editor.mlangTokenCache->builtinTypes.end())
            return jumpBuiltin(typeIt->second, "mlang builtin");

        std::string macroKey = key;
        if(!macroKey.empty() && macroKey.back() == '!')
            macroKey.pop_back();
        auto macroIt = editor.mlangTokenCache->builtinMacros.find(macroKey);
        if(macroIt != editor.mlangTokenCache->builtinMacros.end())
            return jumpBuiltin(macroIt->second, "mlang macro");

        auto attrIt = editor.mlangTokenCache->builtinAttributes.find(key);
        if(attrIt != editor.mlangTokenCache->builtinAttributes.end())
            return jumpBuiltin(attrIt->second, "mlang attribute");

        auto fnIt = editor.mlangTokenCache->builtinFunctions.find(key);
        if(fnIt != editor.mlangTokenCache->builtinFunctions.end())
            return jumpBuiltin(fnIt->second, "mlang fn");
    }

    std::string builtinPath;
    int builtinLine = 0;
    if(MlangUtilities::findBuiltinType(symbol, builtinPath, builtinLine))
        return jumpToLocation(builtinPath, builtinLine, 0, "mlang builtin");

    std::string macroPath;
    int macroLine = 0;
    std::string macroSym = symbol;
    if(!macroSym.empty() && macroSym.back() == '!')
        macroSym.pop_back();
    if(MlangUtilities::findBuiltinMacro(macroSym, macroPath, macroLine))
        return jumpToLocation(macroPath, macroLine, 0, "mlang macro");

    std::string attrPath;
    int attrLine = 0;
    if(MlangUtilities::findBuiltinAttribute(symbol, attrPath, attrLine))
        return jumpToLocation(attrPath, attrLine, 0, "mlang attribute");

    std::string fnPath;
    int fnLine = 0;
    if(MlangUtilities::findBuiltinFunction(symbol, fnPath, fnLine,
                                           editor.currentBuffer->filename))
        return jumpToLocation(fnPath, fnLine, 0, "mlang fn");

    int defY = -1;
    int defX = 0;
    if(MlangUtilities::findTopLevelDefInLines(*editor.lines, symbol, defY,
                                              defX))
    {
        editor.pushJumpLocation();
        *editor.cursorY = defY;
        *editor.cursorX = defX;
        applyViewport();
        editor.setStatusMessage(std::string("gd (mlang local)") + gdArrow +
                                *editor.filename + ":" +
                                std::to_string(defY + 1));
        return true;
    }

    if(CppNavigationUtilities::searchLocalDefinition(
           *editor.lines, symbol, *editor.cursorY, *editor.cursorX, defY, defX))
    {
        if(defY != *editor.cursorY || defX != *editor.cursorX)
        {
            editor.pushJumpLocation();
            *editor.cursorY = defY;
            *editor.cursorX = defX;
            applyViewport();
            editor.setStatusMessage(std::string("gd (mlang local)") + gdArrow +
                                    *editor.filename + ":" +
                                    std::to_string(defY + 1));
            return true;
        }
    }

    if(CppNavigationUtilities::searchMemberDefinition(*editor.lines, symbol,
                                                      defY, defX))
    {
        editor.pushJumpLocation();
        *editor.cursorY = defY;
        *editor.cursorX = defX;
        applyViewport();
        editor.setStatusMessage(std::string("gd (mlang member)") + gdArrow +
                                *editor.filename + ":" +
                                std::to_string(defY + 1));
        return true;
    }

    editor.setStatusMessage("gd (mlang): not found");
    return true;
}
