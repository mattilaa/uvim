#include "editor_definition_controller.h"

#include "ascii.h"
#include "cpp_navigation_utilities.h"
#include "editor.h"
#include "editor_utils.h"
#include "text_utils.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif

#include <fstream>
#include <unordered_map>

using editor::helper::collect_js_ts_imports;
using editor::helper::css_import_path_under_cursor;
using editor::helper::extract_html_stylesheets;
using editor::helper::extract_js_ts_module_specifier;
using editor::helper::find_css_selector_in_file;
using editor::helper::find_js_ts_def_in_file;
using editor::helper::find_ts_member_in_type;
using editor::helper::find_ts_type_definition;
using editor::helper::find_ts_type_for_identifier;
using editor::helper::html_path_under_cursor;
using editor::helper::infer_ts_type_from_array_method_line;
using editor::helper::resolve_js_ts_module;
using editor::helper::resolve_js_ts_module_path;
using editor::helper::trim_view;

#ifdef UVIM_ENABLE_CLANGD_LSP
bool EditorDefinitionController::goToGenericLspDefinition(
    LspClient* client, const char* languageId, std::string_view label)
{
    if(!client)
        return false;
    client->didChange(editor.currentBuffer->filename, bufferText(), languageId);
    auto loc = client->definition(editor.currentBuffer->filename,
                                  *editor.cursorY, *editor.cursorX);
    if(!loc)
        return false;
    return jumpToLocation(loc->path, loc->line, loc->character, label);
}
#endif

namespace
{
bool cursorInAttributeValue(std::string_view line, int cursorX,
                            std::string_view attr)
{
    std::string_view trimmed = trim_view(line);
    size_t pos = trimmed.find(attr);
    if(text_utils::is_not_found(pos))
        return false;
    size_t eq = trimmed.find('=', pos + attr.size());
    if(text_utils::is_not_found(eq))
        return false;
    size_t q = trimmed.find_first_of("\"'", eq + 1);
    if(text_utils::is_not_found(q))
        return false;
    char quote = trimmed[q];
    size_t end = trimmed.find(quote, q + 1);
    if(text_utils::is_not_found(end) || end <= q + 1)
        return false;
    int startX = static_cast<int>(q + 1 + (trimmed.data() - line.data()));
    int endX = static_cast<int>(end + (trimmed.data() - line.data()));
    return cursorX >= startX && cursorX <= endX;
}
} // namespace

bool EditorDefinitionController::goToWebDefinition(FileType fileType,
                                                   const std::string& symbol)
{
    const std::string gdArrow = ascii::utf8(ascii::RIGHT_ARROW_PADDED);

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(fileType == FileType::Html && editor.isHtmlLspEnabled() &&
       goToGenericLspDefinition(editor.htmlLspClient.get(), "html", "html"))
        return true;
    if(fileType == FileType::Css && editor.isCssLspEnabled() &&
       goToGenericLspDefinition(editor.cssLspClient.get(), "css", "css"))
        return true;
    if(fileType == FileType::Json && editor.isJsonLspEnabled() &&
       goToGenericLspDefinition(editor.jsonLspClient.get(), "json", "json"))
        return true;
    if((fileType == FileType::JavaScript || fileType == FileType::TypeScript) &&
       editor.isTsLspEnabled())
    {
        const char* lang =
            fileType == FileType::TypeScript ? "typescript" : "javascript";
        if(goToGenericLspDefinition(editor.tsLspClient.get(), lang, "ts"))
            return true;
    }
#endif

    std::string_view lineView;
    if(*editor.cursorY >= 0 &&
       *editor.cursorY < static_cast<int>(editor.lines->size()))
        lineView = (*editor.lines)[*editor.cursorY];

    if(fileType == FileType::JavaScript || fileType == FileType::TypeScript)
    {
        if(!lineView.empty() && *editor.cursorX >= 0 && !symbol.empty())
        {
            int pos = *editor.cursorX;
            if(pos >= static_cast<int>(lineView.size()))
                pos = static_cast<int>(lineView.size()) - 1;
            if(pos >= 0 && !CppNavigationUtilities::isIdent(lineView[pos]) &&
               pos > 0 && CppNavigationUtilities::isIdent(lineView[pos - 1]))
                --pos;
            if(pos >= 0 && CppNavigationUtilities::isIdent(lineView[pos]))
            {
                int symStart = pos;
                int symEnd = pos;
                while(symStart > 0 &&
                      CppNavigationUtilities::isIdent(lineView[symStart - 1]))
                    --symStart;
                while(symEnd + 1 < static_cast<int>(lineView.size()) &&
                      CppNavigationUtilities::isIdent(lineView[symEnd + 1]))
                    ++symEnd;

                int before = symStart - 1;
                while(before >= 0 && text_utils::is_space(lineView[before]))
                    --before;
                if(before >= 0 && lineView[before] == '.')
                {
                    std::string_view member =
                        lineView.substr(symStart, symEnd - symStart + 1);
                    int baseEnd = before - 1;
                    while(baseEnd >= 0 &&
                          text_utils::is_space(lineView[baseEnd]))
                        --baseEnd;
                    int baseStart = baseEnd;
                    while(baseStart >= 0 &&
                          CppNavigationUtilities::isIdent(lineView[baseStart]))
                        --baseStart;
                    ++baseStart;
                    if(baseStart <= baseEnd)
                    {
                        std::string_view base =
                            lineView.substr(baseStart, baseEnd - baseStart + 1);
                        std::string typeName = find_ts_type_for_identifier(
                            *editor.lines, base, *editor.cursorY);
                        if(typeName.empty())
                            typeName = infer_ts_type_from_array_method_line(
                                lineView, base, *editor.lines, *editor.cursorY);
                        if(!typeName.empty())
                        {
                            int typeY = -1;
                            int typeX = 0;
                            if(find_ts_type_definition(*editor.lines, typeName,
                                                       typeY, typeX))
                            {
                                int memberY = -1;
                                int memberX = 0;
                                if(find_ts_member_in_type(*editor.lines, typeY,
                                                          member, memberY,
                                                          memberX))
                                {
                                    editor.pushJumpLocation();
                                    *editor.cursorY = memberY;
                                    *editor.cursorX = memberX;
                                    applyViewport();
                                    editor.setStatusMessage(
                                        std::string("gd (ts member)") +
                                        gdArrow + *editor.filename + ":" +
                                        std::to_string(memberY + 1));
                                    return true;
                                }
                            }

                            std::unordered_map<std::string, std::string>
                                imports;
                            collect_js_ts_imports(*editor.lines, imports);
                            auto itType = imports.find(typeName);
                            if(itType != imports.end())
                            {
                                std::string resolved = resolve_js_ts_module(
                                    editor.currentBuffer->filename,
                                    itType->second);
                                std::ifstream in(resolved);
                                if(in.is_open())
                                {
                                    std::vector<std::string> fileLines;
                                    std::string fileLine;
                                    while(std::getline(in, fileLine))
                                    {
                                        if(!fileLine.empty() &&
                                           fileLine.back() == '\r')
                                            fileLine.pop_back();
                                        fileLines.push_back(fileLine);
                                    }
                                    int defY = -1;
                                    int defX = 0;
                                    if(find_ts_type_definition(
                                           fileLines, typeName, defY, defX))
                                    {
                                        int memberY = -1;
                                        int memberX = 0;
                                        if(find_ts_member_in_type(
                                               fileLines, defY, member, memberY,
                                               memberX))
                                        {
                                            editor.pushJumpLocation();
                                            editor.openFile(resolved);
                                            *editor.cursorY = memberY;
                                            *editor.cursorX = memberX;
                                            applyViewport();
                                            editor.setStatusMessage(
                                                std::string("gd (ts member)") +
                                                gdArrow + resolved + ":" +
                                                std::to_string(memberY + 1));
                                            return true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        std::string_view module;
        if(!lineView.empty() &&
           extract_js_ts_module_specifier(lineView, module))
        {
            int x = *editor.cursorX;
            size_t q = lineView.find_first_of("\"'");
            if(text_utils::is_found(q))
            {
                char quote = lineView[q];
                size_t end = lineView.find(quote, q + 1);
                if(text_utils::is_found(end) && x >= static_cast<int>(q) + 1 &&
                   x <= static_cast<int>(end))
                {
                    std::string resolved = resolve_js_ts_module(
                        editor.currentBuffer->filename, module);
                    if(!resolved.empty())
                    {
                        editor.pushJumpLocation();
                        editor.openFile(resolved);
                        applyViewport();
                        editor.setStatusMessage(
                            std::string("gd (js/ts import)") + gdArrow +
                            resolved);
                        return true;
                    }
                }
            }
        }

        int defY = -1;
        int defX = 0;
        if(find_js_ts_def_in_file(editor.currentBuffer->filename, symbol, defY,
                                  defX))
        {
            *editor.cursorY = defY;
            *editor.cursorX = defX;
            applyViewport();
            editor.setStatusMessage(std::string("gd (js/ts)") + gdArrow +
                                    *editor.filename + ":" +
                                    std::to_string(defY + 1));
            return true;
        }

        std::unordered_map<std::string, std::string> imports;
        collect_js_ts_imports(*editor.lines, imports);
        auto it = imports.find(symbol);
        if(it != imports.end())
        {
            std::string resolved = resolve_js_ts_module(
                editor.currentBuffer->filename, it->second);
            if(!resolved.empty())
            {
                int defFileY = -1;
                int defFileX = 0;
                bool found = find_js_ts_def_in_file(resolved, symbol, defFileY,
                                                    defFileX);
                editor.pushJumpLocation();
                editor.openFile(resolved);
                if(found)
                {
                    *editor.cursorY = defFileY;
                    *editor.cursorX = defFileX;
                    applyViewport();
                    editor.setStatusMessage(std::string("gd (js/ts)") +
                                            gdArrow + resolved + ":" +
                                            std::to_string(defFileY + 1));
                }
                else
                {
                    applyViewport();
                    editor.setStatusMessage(std::string("gd (js/ts import)") +
                                            gdArrow + resolved);
                }
                return true;
            }
        }
    }

    if(fileType == FileType::Html)
    {
        std::string_view htmlPath;
        if(!lineView.empty() &&
           html_path_under_cursor(lineView, *editor.cursorX, htmlPath))
        {
            std::string htmlPathStr(htmlPath);
            if(!htmlPathStr.empty() && htmlPathStr.front() != '.' &&
               htmlPathStr.front() != '/')
                htmlPathStr = "./" + htmlPathStr;
            std::string resolved = resolve_js_ts_module_path(
                editor.currentBuffer->filename, htmlPathStr);
            if(!resolved.empty())
            {
                editor.pushJumpLocation();
                editor.openFile(resolved);
                applyViewport();
                editor.setStatusMessage(std::string("gd (html link)") +
                                        gdArrow + resolved);
                return true;
            }
        }

        bool inClass =
            !lineView.empty() &&
            cursorInAttributeValue(lineView, *editor.cursorX, "class");
        bool inId = !lineView.empty() &&
                    cursorInAttributeValue(lineView, *editor.cursorX, "id");
        if(!symbol.empty() && (inClass || inId))
        {
            auto sheets = extract_html_stylesheets(*editor.lines);
            for(const auto& sheet : sheets)
            {
                std::string resolved = resolve_js_ts_module_path(
                    editor.currentBuffer->filename, sheet);
                if(resolved.empty())
                    continue;
                std::string selector = inId ? "#" + symbol : "." + symbol;
                int defY = -1;
                int defX = 0;
                if(find_css_selector_in_file(resolved, selector, defY, defX))
                {
                    editor.pushJumpLocation();
                    editor.openFile(resolved);
                    *editor.cursorY = defY;
                    *editor.cursorX = defX;
                    applyViewport();
                    editor.setStatusMessage(std::string("gd (html css)") +
                                            gdArrow + resolved + ":" +
                                            std::to_string(defY + 1));
                    return true;
                }
            }
        }
    }

    if(fileType == FileType::Css)
    {
        std::string_view cssPath;
        if(!lineView.empty() &&
           css_import_path_under_cursor(lineView, *editor.cursorX, cssPath))
        {
            std::string cssPathStr(cssPath);
            if(!cssPathStr.empty() && cssPathStr.front() != '.' &&
               cssPathStr.front() != '/')
                cssPathStr = "./" + cssPathStr;
            std::string resolved = resolve_js_ts_module_path(
                editor.currentBuffer->filename, cssPathStr);
            if(!resolved.empty())
            {
                editor.pushJumpLocation();
                editor.openFile(resolved);
                applyViewport();
                editor.setStatusMessage(std::string("gd (css import)") +
                                        gdArrow + resolved);
                return true;
            }
        }
    }

    return false;
}
