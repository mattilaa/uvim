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
#include <unordered_map>
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

struct TopLevelFunction
{
    std::string name;
    int count = 0;
};

struct AssemblyFunctionBlock
{
    std::string symbol;
    std::string label;
    std::vector<std::string> lines;
    bool anchor = false;
};

bool containsKeyword(std::string_view text, std::string_view keyword)
{
    size_t pos = 0;
    while(text_utils::is_found(pos = text.find(keyword, pos)))
    {
        const bool validStart =
            pos == 0 ||
            !(text_utils::is_alpha(text[pos - 1]) ||
              text_utils::is_digit(text[pos - 1]) || text[pos - 1] == '_');
        const size_t end = pos + keyword.size();
        const bool validEnd =
            end >= text.size() ||
            !(text_utils::is_alpha(text[end]) ||
              text_utils::is_digit(text[end]) || text[end] == '_');
        if(validStart && validEnd)
            return true;
        pos = end;
    }
    return false;
}

std::vector<TopLevelType>
collectTopLevelTypes(const std::vector<std::string>& lines, bool includeClass)
{
    std::vector<TopLevelType> types;
    int braceDepth = 0;

    auto pushUnique = [&](std::string kind, std::string name)
    {
        if(name.empty())
            return;
        const auto found = std::find_if(
            types.begin(), types.end(), [&](const TopLevelType& type)
            { return type.kind == kind && type.name == name; });
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

std::vector<TopLevelFunction>
collectTopLevelFunctions(const std::vector<std::string>& lines)
{
    std::vector<TopLevelFunction> functions;
    int braceDepth = 0;
    std::string pending;

    auto noteFunction = [&](std::string name)
    {
        if(name.empty() || name == "uvim_emit_asm_anchor" ||
           name == "uvim_emit_asm_function_anchor")
            return;
        auto it = std::find_if(functions.begin(), functions.end(),
                               [&](const TopLevelFunction& function)
                               { return function.name == name; });
        if(it == functions.end())
            functions.push_back({std::move(name), 1});
        else
            ++it->count;
    };

    auto trim = [](std::string_view value)
    {
        while(!value.empty() && text_utils::is_space(value.front()))
            value.remove_prefix(1);
        while(!value.empty() && text_utils::is_space(value.back()))
            value.remove_suffix(1);
        return value;
    };

    for(const std::string& line : lines)
    {
        std::string_view text(line);
        size_t comment = text.find("//");
        if(text_utils::is_found(comment))
            text = text.substr(0, comment);

        if(braceDepth == 0)
        {
            std::string_view trimmed = trim(text);
            if(!trimmed.empty() && trimmed[0] != '#')
            {
                if(!pending.empty())
                    pending.push_back(' ');
                pending.append(trimmed.data(), trimmed.size());
            }

            const size_t openBrace = pending.find('{');
            if(text_utils::is_found(openBrace))
            {
                std::string_view signature =
                    trim(std::string_view(pending).substr(0, openBrace));
                const size_t closeParen = signature.rfind(')');
                const size_t openParen = signature.rfind('(', closeParen);
                const bool skippedContext =
                    containsKeyword(signature, "struct") ||
                    containsKeyword(signature, "class") ||
                    containsKeyword(signature, "union") ||
                    containsKeyword(signature, "enum") ||
                    containsKeyword(signature, "namespace") ||
                    containsKeyword(signature, "if") ||
                    containsKeyword(signature, "else") ||
                    containsKeyword(signature, "for") ||
                    containsKeyword(signature, "while") ||
                    containsKeyword(signature, "switch") ||
                    containsKeyword(signature, "catch");

                if(!skippedContext && text_utils::is_found(openParen) &&
                   text_utils::is_found(closeParen) && openParen < closeParen)
                {
                    size_t nameEnd = openParen;
                    while(nameEnd > 0 &&
                          text_utils::is_space(signature[nameEnd - 1]))
                        --nameEnd;
                    size_t nameStart = nameEnd;
                    while(nameStart > 0 &&
                          (text_utils::is_alpha(signature[nameStart - 1]) ||
                           text_utils::is_digit(signature[nameStart - 1]) ||
                           signature[nameStart - 1] == '_'))
                        --nameStart;
                    if(nameEnd > nameStart)
                        noteFunction(std::string(
                            signature.substr(nameStart, nameEnd - nameStart)));
                }
                pending.clear();
            }
            else if(text_utils::is_found(pending.find(';')))
            {
                pending.clear();
            }
        }

        for(char ch : text)
        {
            if(ch == '{')
                ++braceDepth;
            else if(ch == '}' && braceDepth > 0)
                --braceDepth;
        }
        if(braceDepth == 0 && text_utils::is_found(text.find('}')))
            pending.clear();
    }

    functions.erase(std::remove_if(functions.begin(), functions.end(),
                                   [](const TopLevelFunction& function)
                                   { return function.count != 1; }),
                    functions.end());
    return functions;
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

std::string
cppAnchorForFunctions(const std::vector<TopLevelFunction>& functions)
{
    if(functions.empty())
        return {};

    std::string anchor;
    anchor += "\nnamespace uvim_emit_asm_detail {\n";
    anchor += "template <typename F>\n";
    anchor += "void touch_function(F function)\n";
    anchor += "{\n";
    anchor += "#if defined(__clang__) || defined(__GNUC__)\n";
    anchor += "    asm volatile(\"\" : : \"g\"(function) : \"memory\");\n";
    anchor += "#else\n";
    anchor += "    (void)function;\n";
    anchor += "#endif\n";
    anchor += "}\n";
    anchor += "}\n";
    anchor += "extern \"C\" __attribute__((used)) void "
              "uvim_emit_asm_function_anchor()\n";
    anchor += "{\n";
    for(const TopLevelFunction& function : functions)
        anchor += "    uvim_emit_asm_detail::touch_function(&" + function.name +
                  ");\n";
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
        anchor += "    " + type.kind + " " + type.name + " value_" + type.name +
                  " = {0};\n";
        anchor += "#if defined(__clang__) || defined(__GNUC__)\n";
        anchor += "    __asm__ volatile(\"\" : : \"g\"(&value_" + type.name +
                  ") : \"memory\");\n";
        anchor += "#endif\n";
    }
    anchor += "}\n";
    return anchor;
}

std::string cAnchorForFunctions(const std::vector<TopLevelFunction>& functions)
{
    if(functions.empty())
        return {};

    std::string anchor;
    anchor += "\n__attribute__((used)) void "
              "uvim_emit_asm_function_anchor(void)\n";
    anchor += "{\n";
    for(const TopLevelFunction& function : functions)
    {
        anchor += "#if defined(__clang__) || defined(__GNUC__)\n";
        anchor += "    __asm__ volatile(\"\" : : \"g\"(&" + function.name +
                  ") : \"memory\");\n";
        anchor += "#else\n";
        anchor += "    (void)&" + function.name + ";\n";
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

std::string trimCopy(std::string_view value)
{
    while(!value.empty() && text_utils::is_space(value.front()))
        value.remove_prefix(1);
    while(!value.empty() && text_utils::is_space(value.back()))
        value.remove_suffix(1);
    return std::string(value);
}

bool isLabelLine(std::string_view trimmed)
{
    if(trimmed.empty() || trimmed.front() == '.' || trimmed.front() == ';' ||
       trimmed.front() == '#')
        return false;

    const size_t colon = trimmed.find(':');
    if(text_utils::is_not_found(colon))
        return false;

    for(size_t i = 0; i < colon; ++i)
    {
        const char ch = trimmed[i];
        if(!(text_utils::is_alpha(ch) || text_utils::is_digit(ch) ||
             ch == '_' || ch == '.' || ch == '$'))
            return false;
    }
    return true;
}

std::string symbolFromLabel(std::string_view trimmed)
{
    const size_t colon = trimmed.find(':');
    if(text_utils::is_not_found(colon))
        return {};
    return std::string(trimmed.substr(0, colon));
}

std::string stripFunctionPrefix(std::string symbol)
{
    while(symbol.size() > 1 && symbol.front() == '_' && symbol[1] != 'Z')
        symbol.erase(symbol.begin());
    return symbol;
}

std::string demangleSymbol(std::string symbol,
                           std::unordered_map<std::string, std::string>& cache)
{
    if(symbol.empty())
        return symbol;

    if(auto it = cache.find(symbol); it != cache.end())
        return it->second;

    std::string display = stripFunctionPrefix(symbol);
    if(text_utils::is_found(display.find("_Z")) ||
       text_utils::is_found(symbol.find("_Z")))
    {
        ProcessPipe pipe({"c++filt", symbol});
        if(pipe)
        {
            std::string demangled = pipe.readLine();
            pipe.close();
            if(!demangled.empty() && demangled != symbol)
                display = std::move(demangled);
        }
    }

    cache.emplace(symbol, display);
    return display;
}

std::string stripAssemblyComment(std::string_view line)
{
    size_t end = line.size();
    const size_t semicolon = line.find(';');
    if(text_utils::is_found(semicolon))
        end = std::min(end, semicolon);
    const size_t slashComment = line.find("//");
    if(text_utils::is_found(slashComment))
        end = std::min(end, slashComment);
    return trimCopy(line.substr(0, end));
}

bool shouldSkipAsmLine(std::string_view trimmed)
{
    if(trimmed.empty())
        return true;
    if(trimmed.front() == ';' || trimmed.rfind("//", 0) == 0 ||
       trimmed.rfind("# %bb", 0) == 0)
        return true;
    if(trimmed.front() != '.')
        return false;

    return !isLabelLine(trimmed);
}

std::string extractBeginFunctionSymbol(std::string_view line)
{
    constexpr std::string_view marker = "-- Begin function ";
    const size_t pos = line.find(marker);
    if(text_utils::is_not_found(pos))
        return {};

    std::string_view symbol = line.substr(pos + marker.size());
    while(!symbol.empty() && text_utils::is_space(symbol.front()))
        symbol.remove_prefix(1);
    size_t end = 0;
    while(end < symbol.size() && !text_utils::is_space(symbol[end]))
        ++end;
    return std::string(symbol.substr(0, end));
}

std::string compactAssemblyOutput(std::string_view output)
{
    std::vector<AssemblyFunctionBlock> blocks;
    std::unordered_map<std::string, std::string> demangleCache;

    AssemblyFunctionBlock current;
    bool inFunction = false;
    bool waitingForLabel = false;
    std::string pendingSymbol;

    auto finishBlock = [&]()
    {
        if(!current.symbol.empty() || !current.lines.empty())
            blocks.push_back(std::move(current));
        current = {};
        inFunction = false;
        waitingForLabel = false;
        pendingSymbol.clear();
    };

    for(const std::string& rawLine : splitLines(output))
    {
        if(text_utils::is_found(rawLine.find("-- End function")))
        {
            finishBlock();
            continue;
        }

        const std::string beginSymbol = extractBeginFunctionSymbol(rawLine);
        if(!beginSymbol.empty())
        {
            if(inFunction)
                finishBlock();
            inFunction = true;
            waitingForLabel = true;
            pendingSymbol = beginSymbol;
            current.symbol = beginSymbol;
            current.anchor =
                text_utils::is_found(beginSymbol.find("uvim_emit_asm_"));
            continue;
        }

        std::string trimmed = trimCopy(rawLine);
        if(!inFunction && isLabelLine(trimmed))
        {
            inFunction = true;
            waitingForLabel = true;
        }

        if(!inFunction)
            continue;

        if(isLabelLine(trimmed))
        {
            const std::string symbol = symbolFromLabel(trimmed);
            if(waitingForLabel)
            {
                if(current.symbol.empty())
                    current.symbol =
                        !pendingSymbol.empty() ? pendingSymbol : symbol;
                current.anchor =
                    text_utils::is_found(current.symbol.find("uvim_emit_asm_"));
                current.label = demangleSymbol(current.symbol, demangleCache);
                current.lines.push_back(current.label + ":");
                waitingForLabel = false;
            }
            else if(symbol.rfind("Ltmp", 0) != 0 &&
                    symbol.rfind("Lfunc_end", 0) != 0)
            {
                current.lines.push_back(symbol + ":");
            }
            continue;
        }

        if(shouldSkipAsmLine(trimmed))
            continue;

        trimmed = stripAssemblyComment(trimmed);
        if(trimmed.empty())
            continue;
        current.lines.push_back("    " + trimmed);
    }

    if(inFunction)
        finishBlock();

    const bool hasRealFunction = std::any_of(
        blocks.begin(), blocks.end(), [](const AssemblyFunctionBlock& block)
        { return !block.anchor && !block.lines.empty(); });

    std::string compact;
    for(const AssemblyFunctionBlock& block : blocks)
    {
        if(block.lines.empty())
            continue;
        if(hasRealFunction && block.anchor)
            continue;
        if(!compact.empty())
            compact += '\n';
        for(const std::string& line : block.lines)
        {
            compact += line;
            compact += '\n';
        }
    }

    return compact.empty() ? std::string(output) : compact;
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
        {
            out << cAnchorForTypes(collectTopLevelTypes(*editor.lines, false));
            out << cAnchorForFunctions(collectTopLevelFunctions(*editor.lines));
        }
        else
        {
            out << cppAnchorForTypes(collectTopLevelTypes(*editor.lines, true));
            out << cppAnchorForFunctions(
                collectTopLevelFunctions(*editor.lines));
        }
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
    bool rawOutput = false;
    for(std::string& flag : splitCompilerFlags(flags))
    {
        if(flag == "--raw")
        {
            rawOutput = true;
            continue;
        }
        args.push_back(std::move(flag));
    }

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
    const std::string displayOutput =
        rawOutput ? output : compactAssemblyOutput(output);
    editor.createNewBuffer();
    if(editor.currentBuffer)
    {
        editor.currentBuffer->lines = splitLines(displayOutput);
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
