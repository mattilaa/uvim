#include "debug_type_layout.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <limits>
#include <regex>
#include <sstream>
#include <unordered_map>

namespace
{
using Metadata = std::unordered_map<long long, std::string>;
enum class Query
{
    Value,
    FunctionPointerReturn,
    ScopedFunctionPointerReturn,
    CallReturn
};

std::optional<long long> number(const std::string& node, const std::string& key)
{
    const std::regex pattern("\\b" + key + ": !?([0-9]+)");
    std::smatch match;
    if(!std::regex_search(node, match, pattern))
        return {};
    try
    {
        return std::stoll(match[1].str());
    }
    catch(...)
    {
        return {};
    }
}

std::string stringField(const std::string& node, const std::string& key)
{
    static const std::regex namePattern(R"field(\bname: "([^"]*)")field");
    static const std::regex filenamePattern(
        R"field(\bfilename: "([^"]*)")field");
    static const std::regex directoryPattern(
        R"field(\bdirectory: "([^"]*)")field");
    static const std::regex linkagePattern(
        R"field(\blinkageName: "([^"]*)")field");
    const auto& pattern = key == "linkageName" ? linkagePattern
                          : key == "name"      ? namePattern
                          : key == "filename"  ? filenamePattern
                                               : directoryPattern;
    std::smatch match;
    if(!std::regex_search(node, match, pattern))
        return {};
    std::string result = match[1].str();
    // LLVM metadata strings encode special UTF-8 bytes as \XX.
    for(size_t i = 0; i + 2 < result.size(); ++i)
        if(result[i] == '\\' &&
           std::isxdigit(static_cast<unsigned char>(result[i + 1])) &&
           std::isxdigit(static_cast<unsigned char>(result[i + 2])))
            result.replace(
                i,
                3,
                1,
                static_cast<char>(
                    std::stoi(result.substr(i + 1, 2), nullptr, 16)));
    return result;
}

std::string name(const std::string& node)
{
    return stringField(node, "name");
}

} // namespace

static std::optional<DebugTypeLayout> parseLayout(
    const std::string& ir,
    const std::string& expression,
    int cursorLine,
    Query query,
    int cursorColumn)
{
    Metadata nodes;
    std::istringstream input(ir);
    std::string line;
    static const std::regex definition(R"(^!([0-9]+) = (.*)$)");
    std::smatch match;
    while(std::getline(input, line))
        if(std::regex_match(line, match, definition))
            nodes.emplace(std::stoll(match[1].str()), match[2].str());
    auto node = [&](long long id) -> const std::string&
    {
        static const std::string empty;
        auto it = nodes.find(id);
        return it == nodes.end() ? empty : it->second;
    };
    std::function<long long(long long, int)> unwrap =
        [&](long long id, int depth)
    {
        if(depth < 16 &&
           (node(id).find("DW_TAG_typedef") != std::string::npos ||
            node(id).find("DW_TAG_const_type") != std::string::npos ||
            node(id).find("DW_TAG_volatile_type") != std::string::npos ||
            node(id).find("DW_TAG_reference_type") != std::string::npos ||
            node(id).find("DW_TAG_rvalue_reference_type") != std::string::npos))
            if(auto base = number(node(id), "baseType"))
                return unwrap(*base, depth + 1);
        return id;
    };
    std::optional<long long> primaryFile;
    for(const auto& [id, text] : nodes)
        if(text.find("!DICompileUnit(") != std::string::npos)
            primaryFile = number(text, "file");
    std::unordered_map<long long, std::filesystem::path> filePaths;
    auto filePath = [&](long long id)
    {
        namespace fs = std::filesystem;
        if(auto it = filePaths.find(id); it != filePaths.end())
            return it->second;
        fs::path filename(stringField(node(id), "filename"));
        auto path =
            (filename.is_absolute()
                 ? filename
                 : fs::path(stringField(node(id), "directory")) / filename)
                .lexically_normal();
        filePaths.emplace(id, path);
        return path;
    };
    auto inPrimary = [&](const std::string& text)
    {
        const auto file = number(text, "file");
        return !primaryFile || file == primaryFile ||
               (file && filePath(*file) == filePath(*primaryFile));
    };
    long long currentFunction = -1, currentFunctionLine = -1;
    for(const auto& [id, text] : nodes)
        if(text.find("!DISubprogram(") != std::string::npos && inPrimary(text))
            if(auto start = number(text, "line");
               start && *start <= cursorLine && *start > currentFunctionLine)
            {
                currentFunction = id;
                currentFunctionLine = *start;
            }
    auto functionScope = [&](long long scope)
    {
        for(int depth = 0; depth < 32; ++depth)
        {
            if(node(scope).find("!DISubprogram(") != std::string::npos)
                return scope;
            auto parent = number(node(scope), "scope");
            if(!parent)
                break;
            scope = *parent;
        }
        return -1LL;
    };
    std::optional<long long> cursorScope;
    for(const auto& [id, text] : nodes)
        if(text.find("!DILocation(") != std::string::npos &&
           number(text, "line") == cursorLine)
            if(auto scope = number(text, "scope");
               scope && functionScope(*scope) == currentFunction)
                cursorScope = scope;
    auto visibleScope = [&](long long declarationScope)
    {
        if(!cursorScope)
            return true;
        long long scope = *cursorScope;
        for(int depth = 0; depth < 32; ++depth)
        {
            if(scope == declarationScope)
                return true;
            auto parent = number(node(scope), "scope");
            if(!parent)
                break;
            scope = *parent;
        }
        return false;
    };
    // Dot access traverses the metadata for the actual member type.
    const auto dot = expression.find('.');
    const std::string root = expression.substr(0, dot);
    std::optional<long long> selected;
    long long selectedLine = -1;
    for(const auto& [id, text] : nodes)
    {
        if(query == Query::CallReturn)
            continue;
        const bool local = text.find("!DILocalVariable(") != std::string::npos;
        const bool global =
            text.find("!DIGlobalVariable(") != std::string::npos;
        if((!local && !global) || name(text) != root || !inPrimary(text))
            continue;
        const auto declarationLine = number(text, "line").value_or(0);
        if(query != Query::FunctionPointerReturn &&
           (declarationLine > cursorLine || declarationLine < selectedLine))
            continue;
        if(local && query != Query::FunctionPointerReturn &&
           functionScope(number(text, "scope").value_or(-1)) != currentFunction)
            continue;
        if(local && query != Query::FunctionPointerReturn &&
           !visibleScope(number(text, "scope").value_or(-1)))
            continue;
        if(auto type = number(text, "type"))
        {
            selected = *type;
            selectedLine = declarationLine;
        }
    }
    if(!selected && query == Query::Value)
        for(const auto& [id, text] : nodes)
            if((text.find("!DICompositeType(") != std::string::npos ||
                text.find("!DIBasicType(") != std::string::npos ||
                text.find("DW_TAG_typedef") != std::string::npos) &&
               name(text) == expression)
            {
                selected = id;
                break;
            }
    bool functionType = false;
    if(!selected && query == Query::CallReturn)
    {
        std::unordered_map<std::string, long long> functions;
        std::string wanted = expression;
        if(auto pos = wanted.find_last_of(".:"); pos != std::string::npos)
            wanted = wanted.substr(pos + 1);
        if(auto pos = wanted.find('<'); pos != std::string::npos)
            wanted.resize(pos);
        for(const auto& [id, text] : nodes)
            if(text.find("!DISubprogram(") != std::string::npos)
            {
                auto linkage = stringField(text, "linkageName");
                functions[linkage.empty() ? name(text) : linkage] = id;
            }
        static const std::regex callPattern(
            R"call(\b(?:call|invoke)\b[^@\n]*@(?:"([^"]+)"|([^ (]+))\()call");
        static const std::regex locationPattern(R"(!dbg !([0-9]+))");
        std::vector<std::string> instructions;
        std::istringstream code(ir);
        while(std::getline(code, line))
            instructions.push_back(line);
        long long best = std::numeric_limits<long long>::max();
        bool ambiguous = false;
        auto returnKey = [&](long long type)
        {
            const auto& list = node(number(node(type), "types").value_or(-1));
            return list.substr(0, list.find_first_of(",}"));
        };
        std::smatch call, location;
        for(size_t i = 0; i < instructions.size(); ++i)
        {
            if(instructions[i].find("call ") == std::string::npos &&
               instructions[i].find("invoke ") == std::string::npos)
                continue;
            std::string instruction = instructions[i];
            if(instruction.find("invoke ") != std::string::npos &&
               instruction.find("!dbg") == std::string::npos &&
               i + 1 < instructions.size())
                instruction += instructions[i + 1];
            if(!std::regex_search(instruction, call, callPattern) ||
               !std::regex_search(instruction, location, locationPattern))
                continue;
            const auto linkage =
                call[1].matched ? call[1].str() : call[2].str();
            if(linkage.starts_with("llvm.") ||
               linkage.find(wanted) == std::string::npos)
                continue;
            const auto& at = node(std::stoll(location[1].str()));
            if(number(at, "line") != cursorLine ||
               !inPrimary(
                   node(functionScope(number(at, "scope").value_or(-1)))))
                continue;
            const long long score =
                cursorColumn > 0
                    ? std::llabs(
                          number(at, "column").value_or(0) - cursorColumn)
                    : 0;
            if(auto it = functions.find(linkage);
               it != functions.end() && score <= best)
                if(auto type = number(node(it->second), "type"))
                {
                    if(score == best && selected &&
                       returnKey(*selected) != returnKey(*type))
                        ambiguous = true;
                    if(score < best)
                    {
                        selected = type;
                        functionType = true;
                        best = score;
                        ambiguous = false;
                    }
                }
        }
        if(ambiguous)
            return {};
        // A function definition under the cursor has no call instruction.
        if(!selected)
            for(const auto& [id, text] : nodes)
                if(text.find("!DISubprogram(") != std::string::npos &&
                   name(text) == wanted && inPrimary(text) &&
                   number(text, "line") == cursorLine)
                    if(auto type = number(text, "type"))
                    {
                        selected = type;
                        functionType = true;
                        break;
                    }
    }
    if(!selected)
        return {};
    auto fields = [&](long long type)
    {
        std::vector<long long> result;
        auto elements = number(node(unwrap(type, 0)), "elements");
        if(!elements)
            return result;
        static const std::regex reference(R"(!([0-9]+))");
        const auto& list = node(*elements);
        for(std::sregex_iterator it(list.begin(), list.end(), reference), end;
            it != end;
            ++it)
            result.push_back(std::stoll((*it)[1].str()));
        return result;
    };
    if(dot != std::string::npos && query != Query::CallReturn)
    {
        size_t start = dot + 1;
        while(start < expression.size())
        {
            const auto end = expression.find('.', start);
            const auto memberName = expression.substr(start, end - start);
            std::optional<long long> memberType;
            for(long long field : fields(*selected))
                if(name(node(field)) == memberName)
                    memberType = number(node(field), "baseType");
            if(!memberType)
                return {};
            selected = memberType;
            if(end == std::string::npos)
                break;
            start = end + 1;
        }
    }
    if(query != Query::Value)
    {
        long long type = unwrap(*selected, 0);
        if(!functionType)
        {
            if(node(type).find("DW_TAG_pointer_type") == std::string::npos &&
               node(type).find("DW_TAG_ptr_to_member_type") ==
                   std::string::npos)
                return {};
            auto base = number(node(type), "baseType");
            if(!base)
                return {};
            type = unwrap(*base, 0);
        }
        if(node(type).find("!DISubroutineType(") == std::string::npos)
            return {};
        auto types = number(node(type), "types");
        if(!types)
            return {};
        const auto& list = node(*types);
        if(list.starts_with("!{null"))
            return DebugTypeLayout{0, {}, true, "void"};
        static const std::regex returnType(R"(^!\{!([0-9]+))");
        std::smatch result;
        if(!std::regex_search(list, result, returnType))
            return {};
        selected = std::stoll(result[1].str());
    }
    std::function<std::string(long long, int)> typeName =
        [&](long long id, int depth) -> std::string
    {
        if(depth > 24)
            return "?";
        const auto& text = node(id);
        const auto base = number(text, "baseType");
        if(base)
        {
            if(text.find("DW_TAG_pointer_type") != std::string::npos)
                return typeName(*base, depth + 1) + "*";
            if(text.find("DW_TAG_rvalue_reference_type") != std::string::npos)
                return typeName(*base, depth + 1) + "&&";
            if(text.find("DW_TAG_reference_type") != std::string::npos)
                return typeName(*base, depth + 1) + "&";
            if(text.find("DW_TAG_const_type") != std::string::npos)
                return typeName(*base, depth + 1) + " const";
            if(text.find("DW_TAG_volatile_type") != std::string::npos)
                return typeName(*base, depth + 1) + " volatile";
            if(text.find("DW_TAG_typedef") != std::string::npos &&
               node(unwrap(*base, 0)).find("!DIBasicType(") !=
                   std::string::npos)
                return typeName(*base, depth + 1);
        }
        if(!base && text.find("DW_TAG_pointer_type") != std::string::npos)
            return "void*";
        std::string result = name(text);
        auto scope = number(text, "scope");
        for(int level = 0; scope && level < 16; ++level)
        {
            const auto& parent = node(*scope);
            if(parent.find("!DINamespace(") != std::string::npos &&
               parent.find("exportSymbols: true") == std::string::npos &&
               !name(parent).empty())
                result = name(parent) + "::" + result;
            scope = number(parent, "scope");
        }
        return result.empty()
                   ? text.find("!DISubroutineType(") != std::string::npos
                         ? "function"
                         : "<anonymous>"
                   : result;
    };
    int budget = 256;
    std::function<std::optional<DebugTypeLayout>(long long, int)> layout =
        [&](long long type, int depth) -> std::optional<DebugTypeLayout>
    {
        type = unwrap(type, 0);
        auto size = number(node(type), "size");
        if(!size)
            return {};
        DebugTypeLayout result{*size, {}, true, {}};
        for(long long field : fields(type))
        {
            const auto& text = node(field);
            if(text.find("DW_TAG_member") == std::string::npos)
            {
                if(text.find("DW_TAG_inheritance") != std::string::npos)
                    result.complete = false;
                continue;
            }
            if(depth >= 8 || --budget < 0)
            {
                result.complete = false;
                continue;
            }
            auto bits = number(text, "size");
            auto base = number(text, "baseType");
            const auto offset = number(text, "offset").value_or(0);
            if(!bits || !base || offset > *size || *bits > *size - offset)
            {
                result.complete = false;
                continue;
            }
            DebugTypeMember member{
                name(text).empty() ? "<anonymous>" : name(text),
                *bits,
                offset,
                {},
                false};
            if(auto child = layout(*base, depth + 1))
            {
                member.children = std::move(child->members);
                member.complete = child->complete;
            }
            result.members.push_back(std::move(member));
        }
        return result;
    };
    auto result = layout(*selected, 0);
    if(result)
        result->typeName = typeName(*selected, 0);
    return result;
}

std::optional<DebugTypeLayout> parseDebugTypeMetadata(
    const std::string& ir,
    const std::string& expression,
    int cursorLine)
{
    return parseLayout(ir, expression, cursorLine, Query::Value, 0);
}

std::optional<DebugTypeLayout> parseDebugFunctionPointerReturn(
    const std::string& ir,
    const std::string& variable)
{
    return parseLayout(ir, variable, 0, Query::FunctionPointerReturn, 0);
}

std::optional<DebugTypeLayout> parseDebugCallReturn(
    const std::string& ir,
    const std::string& callee,
    int cursorLine,
    int cursorColumn)
{
    if(auto result =
           parseLayout(ir, callee, cursorLine, Query::CallReturn, cursorColumn))
        return result;
    return parseLayout(
        ir,
        callee,
        cursorLine,
        Query::ScopedFunctionPointerReturn,
        cursorColumn);
}
