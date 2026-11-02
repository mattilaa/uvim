#include "debug_type_layout.h"

#include <cctype>
#include <filesystem>
#include <functional>
#include <regex>
#include <sstream>
#include <unordered_map>

namespace
{
using Metadata = std::unordered_map<long long, std::string>;

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
    const auto& pattern = key == "name"       ? namePattern
                          : key == "filename" ? filenamePattern
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

std::optional<DebugTypeLayout> parseDebugTypeMetadata(
    const std::string& ir,
    const std::string& expression,
    int cursorLine)
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
            node(id).find("DW_TAG_const_type") != std::string::npos))
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
        const bool local = text.find("!DILocalVariable(") != std::string::npos;
        const bool global =
            text.find("!DIGlobalVariable(") != std::string::npos;
        if((!local && !global) || name(text) != root || !inPrimary(text))
            continue;
        const auto declarationLine = number(text, "line").value_or(0);
        if(declarationLine > cursorLine || declarationLine < selectedLine)
            continue;
        if(local &&
           functionScope(number(text, "scope").value_or(-1)) != currentFunction)
            continue;
        if(local && !visibleScope(number(text, "scope").value_or(-1)))
            continue;
        if(auto type = number(text, "type"))
        {
            selected = *type;
            selectedLine = declarationLine;
        }
    }
    if(!selected)
        for(const auto& [id, text] : nodes)
            if((text.find("!DICompositeType(") != std::string::npos ||
                text.find("!DIBasicType(") != std::string::npos ||
                text.find("DW_TAG_typedef") != std::string::npos) &&
               name(text) == expression)
            {
                selected = id;
                break;
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
    if(dot != std::string::npos)
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
    int budget = 256;
    std::function<std::optional<DebugTypeLayout>(long long, int)> layout =
        [&](long long type, int depth) -> std::optional<DebugTypeLayout>
    {
        type = unwrap(type, 0);
        auto size = number(node(type), "size");
        if(!size)
            return {};
        DebugTypeLayout result{*size, {}, true};
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
    return layout(*selected, 0);
}
