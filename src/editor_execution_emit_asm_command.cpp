#include "editor.h"
#include "editor_execution_command_helpers.h"
#include "editor_execution_commands.h"
#include "editor_path_utilities.h"
#include "editor_utils.h"
#include "file_browser_mode.h"
#include "gitignore.h"
#include "mode_state_machine.h"
#include "process_pipe.h"
#include "text_utils.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
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

namespace
{
std::string shellQuote(std::string_view value)
{
    std::string out = "'";
    for(char ch : value)
    {
        if(ch == '\'')
            out += "'\\''";
        else
            out.push_back(ch);
    }
    out += "'";
    return out;
}

std::vector<std::string> splitCompilerFlags(std::string_view flags)
{
    std::vector<std::string> result;
    std::string current;
    char quote = 0;
    bool escaping = false;

    for(char ch : flags)
    {
        if(escaping)
        {
            current.push_back(ch);
            escaping = false;
            continue;
        }
        if(ch == '\\')
        {
            escaping = true;
            continue;
        }
        if(quote)
        {
            if(ch == quote)
                quote = 0;
            else
                current.push_back(ch);
            continue;
        }
        if(ch == '\'' || ch == '"')
        {
            quote = ch;
            continue;
        }
        if(text_utils::is_space(ch))
        {
            if(!current.empty())
            {
                result.push_back(std::move(current));
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }

    if(!current.empty())
        result.push_back(std::move(current));
    return result;
}

bool isCSource(std::string_view path)
{
    return path.size() >= 2 &&
           text_utils::iequals_ascii(path.substr(path.size() - 2), ".c");
}

struct TopLevelType
{
    std::string kind;
    std::string name;
};

std::vector<TopLevelType>
collectTopLevelTypes(const std::vector<std::string>& lines, bool includeClass)
{
    std::vector<TopLevelType> types;
    int braceDepth = 0;

    auto pushUnique = [&](std::string kind, std::string name)
    {
        if(name.empty())
            return;
        const auto found =
            std::find_if(types.begin(), types.end(),
                         [&](const TopLevelType& type)
                         {
                             return type.kind == kind && type.name == name;
                         });
        if(found == types.end())
            types.push_back({std::move(kind), std::move(name)});
    };

    for(const std::string& line : lines)
    {
        std::string_view text(line);
        size_t comment = text.find("//");
        if(text_utils::is_found(comment))
            text = text.substr(0, comment);

        if(braceDepth == 0)
        {
            for(std::string_view keyword : {"struct", "class", "union"})
            {
                if(!includeClass && keyword == "class")
                    continue;
                size_t pos = text.find(keyword);
                if(text_utils::is_not_found(pos))
                    continue;
                const bool validStart =
                    pos == 0 || !(text_utils::is_alpha(text[pos - 1]) ||
                                  text_utils::is_digit(text[pos - 1]) ||
                                  text[pos - 1] == '_');
                size_t nameStart = pos + keyword.size();
                const bool validEnd = nameStart >= text.size() ||
                                      !(text_utils::is_alpha(text[nameStart]) ||
                                        text_utils::is_digit(text[nameStart]) ||
                                        text[nameStart] == '_');
                if(!validStart || !validEnd)
                    continue;

                while(nameStart < text.size() &&
                      text_utils::is_space(text[nameStart]))
                    ++nameStart;
                size_t nameEnd = nameStart;
                while(nameEnd < text.size() &&
                      (text_utils::is_alpha(text[nameEnd]) ||
                       text_utils::is_digit(text[nameEnd]) ||
                       text[nameEnd] == '_'))
                    ++nameEnd;
                size_t afterName = nameEnd;
                while(afterName < text.size() &&
                      text_utils::is_space(text[afterName]))
                    ++afterName;
                if(afterName < text.size() && text[afterName] == ';')
                    continue;
                if(nameEnd > nameStart)
                    pushUnique(std::string(keyword),
                               std::string(text.substr(nameStart,
                                                       nameEnd - nameStart)));
            }
        }

        for(char ch : text)
        {
            if(ch == '{')
                ++braceDepth;
            else if(ch == '}' && braceDepth > 0)
                --braceDepth;
        }
    }

    return types;
}

std::string cppAnchorForTypes(const std::vector<TopLevelType>& types)
{
    if(types.empty())
        return {};

    std::string anchor;
    anchor += "\nnamespace uvim_emit_asm_detail {\n";
    anchor += "template <typename T>\n";
    anchor += "auto touch(int) -> decltype(T{}, void())\n";
    anchor += "{\n";
    anchor += "    T value{};\n";
    anchor += "#if defined(__clang__) || defined(__GNUC__)\n";
    anchor += "    asm volatile(\"\" : : \"g\"(&value) : \"memory\");\n";
    anchor += "#endif\n";
    anchor += "}\n";
    anchor += "template <typename T>\n";
    anchor += "void touch(...) {}\n";
    anchor += "}\n";
    anchor +=
        "extern \"C\" __attribute__((used)) void uvim_emit_asm_anchor()\n";
    anchor += "{\n";
    for(const TopLevelType& type : types)
        anchor += "    uvim_emit_asm_detail::touch<" + type.name + ">(0);\n";
    anchor += "}\n";
    return anchor;
}

std::string cAnchorForTypes(const std::vector<TopLevelType>& types)
{
    if(types.empty())
        return {};

    std::string anchor;
    anchor += "\n__attribute__((used)) void uvim_emit_asm_anchor(void)\n";
    anchor += "{\n";
    for(const TopLevelType& type : types)
    {
        if(type.kind != "struct" && type.kind != "union")
            continue;
        anchor += "    " + type.kind + " " + type.name + " value_" +
                  type.name + " = {0};\n";
        anchor += "#if defined(__clang__) || defined(__GNUC__)\n";
        anchor += "    __asm__ volatile(\"\" : : \"g\"(&value_" + type.name +
                  ") : \"memory\");\n";
        anchor += "#endif\n";
    }
    anchor += "}\n";
    return anchor;
}

std::string bufferText(const std::vector<std::string>& lines)
{
    std::string text;
    for(size_t i = 0; i < lines.size(); ++i)
    {
        text += lines[i];
        if(i + 1 < lines.size())
            text += '\n';
    }
    return text;
}

std::vector<std::string> splitLines(std::string_view text)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while(start <= text.size())
    {
        size_t end = text.find('\n', start);
        if(text_utils::is_not_found(end))
            end = text.size();
        std::string line(text.substr(start, end - start));
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(std::move(line));
        if(end == text.size())
            break;
        start = end + 1;
    }
    if(lines.empty())
        lines.push_back("");
    return lines;
}

std::string displayNameForAsmBuffer(const std::string& sourcePath)
{
    if(sourcePath.empty())
        return "assembly.s";
    return sourcePath + ".s";
}
} // namespace

bool EmitAsmCommand::execute(Editor& editor,
                             const CommandRequest& request) const
{
    constexpr std::string_view command = "emitasm";
    if(request.trimmed != command &&
       request.trimmed.rfind(std::string(command) + " ", 0) != 0)
        return false;

    if(!editor.currentBuffer || !editor.lines || editor.lines->empty())
    {
        editor.setStatusMessage("emitasm: no buffer");
        return true;
    }
    if(!editor.isFileType<FileType::Cpp>())
    {
        editor.setStatusMessage("emitasm: C/C++ only");
        return true;
    }

    const std::string sourcePath = editor.currentBuffer->filename;
    const bool cSource = isCSource(sourcePath);

    std::error_code ec;
    fs::path tempDir = fs::temp_directory_path(ec);
    if(ec)
        tempDir = ".";
    const auto stamp = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    fs::path tempPath =
        tempDir / ("uvim_emit_asm_" + stamp + (cSource ? ".c" : ".cpp"));

    {
        std::ofstream out(tempPath);
        if(!out)
        {
            editor.setStatusMessage("emitasm: cannot create temp source");
            return true;
        }
        out << bufferText(*editor.lines) << '\n';
        if(cSource)
            out << cAnchorForTypes(collectTopLevelTypes(*editor.lines, false));
        else
            out << cppAnchorForTypes(collectTopLevelTypes(*editor.lines, true));
    }

    fs::path sourceDir = ".";
    if(!sourcePath.empty())
    {
        fs::path original(sourcePath);
        if(original.has_parent_path())
            sourceDir = original.parent_path();
    }

    std::vector<std::string> args = {
        cSource ? "clang" : "clang++",       "-x", cSource ? "c" : "c++",
        cSource ? "-std=c11" : "-std=c++20", "-w",
    };

    std::string flags;
    if(request.trimmed.size() > command.size())
        flags = std::string(trim_view(request.trimmed.substr(command.size())));
    for(std::string& flag : splitCompilerFlags(flags))
        args.push_back(std::move(flag));

    args.push_back("-S");
    args.push_back("-fverbose-asm");
    args.push_back("-I");
    args.push_back(sourceDir.string());
    args.push_back("-o");
    args.push_back("-");
    args.push_back(tempPath.string());

    std::string shellCommand;
    for(size_t i = 0; i < args.size(); ++i)
    {
        if(i > 0)
            shellCommand += ' ';
        shellCommand += shellQuote(args[i]);
    }
    shellCommand += " 2>&1";

    ProcessPipe pipe(shellCommand, "r");
    const std::string output = pipe ? pipe.readAll() : "";
    const int status = pipe.close();
    fs::remove(tempPath, ec);

    if(status != 0)
    {
        editor.setStatusMessage("emitasm: clang failed");
        return true;
    }

    const std::string asmName = displayNameForAsmBuffer(sourcePath);
    editor.createNewBuffer();
    if(editor.currentBuffer)
    {
        editor.currentBuffer->lines = splitLines(output);
        editor.currentBuffer->filename = asmName;
        editor.currentBuffer->dirty = false;
        editor.currentBuffer->fileTypeCacheValid = false;
        editor.currentBuffer->cursorX = 0;
        editor.currentBuffer->cursorY = 0;
        editor.currentBuffer->offsetX = 0;
        editor.currentBuffer->offsetY = 0;
        editor.updateCurrentBufferPointers();
    }
    editor.setStatusMessage("emitasm: wrote assembly buffer");
    editor.needsFullRedraw = true;
    return true;
}
} // namespace command::execution
