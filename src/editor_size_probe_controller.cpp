#include "editor.h"
#ifdef UVIM_ENABLE_STRUCT_SIZE_POPUP
#include "editor.h"

#include "cpp_navigation_utilities.h"
#include "cpp_size_probe_options.h"
#include "cpp_type_layout_probe.h"
#include "cursor_call.h"
#include "mlang_size_probe.h"
#include "process_pipe.h"
#include "symbol_popup_layout.h"
#include "symbol_popup_utilities.h"
#include "text_utils.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
namespace fs = std::filesystem;

struct SizeProbeResult
{
    std::optional<long long> size;
    std::string expression;
    std::string memberTypeExpression;
    int insertAfterLine = 0;
    std::string error;
};

struct MemberInfo
{
    std::string name;
    std::optional<long long> size;
    std::optional<long long> offset;
    std::vector<MemberInfo> children;
    int indent = 0;
    bool truncated = false;
    bool layoutComplete = false;
};

struct TypeBody
{
    std::string body;
    int closingLine = -1;
};

struct SourceDocument
{
    std::string path;
    std::vector<std::string> lines;
};

struct TypeDefinition
{
    const SourceDocument* document = nullptr;
    int line = -1;
};

struct ParsedMember
{
    std::string name;
    std::string typeName;
    std::string nestedTypeName;
};

struct ProbeRequest
{
    std::string expression;
    long long expectedValue = 0;
};

int findTypeDefinitionLine(
    const std::vector<std::string>& lines,
    const std::string& typeName);
std::optional<TypeDefinition> findTypeDefinition(
    const std::vector<SourceDocument>& documents,
    const std::string& typeName);
std::string trimCopy(std::string_view text);

bool isCSource(std::string_view path)
{
    return path.size() >= 2 &&
           text_utils::iequals_ascii(path.substr(path.size() - 2), ".c");
}

std::string bufferTextWithProbe(
    const std::vector<std::string>& lines,
    int insertAfterLine,
    std::string_view assertion)
{
    std::string text;
    for(int y = 0; y < static_cast<int>(lines.size()); ++y)
    {
        text += lines[y];
        text += '\n';
        if(y == insertAfterLine)
        {
            text += assertion;
            text += '\n';
        }
    }
    if(insertAfterLine >= static_cast<int>(lines.size()))
    {
        text += assertion;
        text += '\n';
    }
    return text;
}

std::vector<std::string> readLinesFromFile(const fs::path& path)
{
    std::ifstream in(path);
    std::vector<std::string> result;
    std::string line;
    while(std::getline(in, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        result.push_back(line);
    }
    return result;
}

std::vector<SourceDocument> collectSourceDocuments(
    const std::vector<std::string>& lines,
    const std::string& filePath)
{
    std::vector<SourceDocument> documents;
    documents.push_back({filePath, lines});

    std::vector<fs::path> pending;
    std::vector<std::string> seen;
    if(!filePath.empty())
        seen.push_back(fs::absolute(fs::path(filePath)).string());

    auto queueInclude = [&](const fs::path& includePath)
    {
        std::error_code ec;
        const fs::path absolute = fs::absolute(includePath, ec);
        if(ec || !fs::exists(absolute, ec) || fs::is_directory(absolute, ec))
            return;
        const std::string key = absolute.string();
        if(std::find(seen.begin(), seen.end(), key) != seen.end())
            return;
        seen.push_back(key);
        pending.push_back(absolute);
    };

    auto scanIncludes = [&](const std::vector<std::string>& sourceLines,
                            const fs::path& baseDir)
    {
        static const std::regex localIncludePattern(
            R"include(^\s*#\s*include\s*"([^"]+)")include");
        std::smatch match;
        for(const std::string& line : sourceLines)
        {
            if(!std::regex_search(line, match, localIncludePattern) ||
               match.size() < 2)
                continue;
            queueInclude(baseDir / match[1].str());
        }
    };

    fs::path baseDir = ".";
    if(!filePath.empty())
    {
        fs::path currentPath(filePath);
        if(currentPath.has_parent_path())
            baseDir = currentPath.parent_path();
    }
    scanIncludes(lines, baseDir);

    constexpr size_t maxDocuments = 16;
    for(size_t index = 0;
        index < pending.size() && documents.size() < maxDocuments;
        ++index)
    {
        const fs::path path = pending[index];
        std::vector<std::string> includeLines = readLinesFromFile(path);
        if(includeLines.empty())
            continue;
        documents.push_back({path.string(), includeLines});
        fs::path includeDir = path.parent_path();
        if(includeDir.empty())
            includeDir = ".";
        scanIncludes(documents.back().lines, includeDir);
    }

    return documents;
}

std::unordered_map<long long, long long> parseProbeResultsFromClangOutput(
    const std::string& output)
{
    std::unordered_map<long long, long long> results;
    std::unordered_set<long long> invalidRows;
    std::unordered_map<long long, std::pair<long long, long long>> candidates;
    static const std::regex diagnosticPattern(
        R"(^.*:([0-9]+):[0-9]+: (error|fatal error|note): (.*)$)");
    static const std::regex evalPattern(
        R"(expression evaluates to '([0-9]+) == (-?[0-9]+)')");
    std::istringstream lines(output);
    std::string line;
    std::smatch diagnostic, evaluation;
    while(std::getline(lines, line))
    {
        if(!std::regex_match(line, diagnostic, diagnosticPattern))
            continue;
        try
        {
            const long long row = std::stoll(diagnostic[1].str());
            const std::string kind = diagnostic[2].str();
            const std::string message = diagnostic[3].str();
            // Clang can recover an invalid member-function expression as a call
            // and still emit a bogus sizeof result. Only trust assertions whose
            // own source line has no other compilation errors.
            if(kind != "note" &&
               !message.starts_with("static assertion failed"))
                invalidRows.insert(row);
            else if(
                kind == "note" &&
                std::regex_search(message, evaluation, evalPattern))
                candidates[std::stoll(evaluation[2].str())] =
                    {row, std::stoll(evaluation[1].str())};
        }
        catch(...)
        {
        }
    }
    for(const auto& [expected, candidate] : candidates)
        if(!invalidRows.contains(candidate.first))
            results[expected] = candidate.second;

    return results;
}

std::vector<std::optional<long long>> runAssertionProbes(
    const std::vector<std::string>& lines,
    const std::string& filePath,
    const std::vector<ProbeRequest>& requests,
    int insertAfterLine,
    std::string* error = nullptr)
{
    std::vector<std::optional<long long>> values(requests.size());
    if(requests.empty())
        return values;

    const bool cSource = isCSource(filePath);
    std::string assertions;
    for(const ProbeRequest& request : requests)
    {
        const std::string expected = std::to_string(request.expectedValue);
        assertions += cSource ? "_Static_assert(" : "static_assert(";
        assertions += request.expression + " == " + expected +
                      ", \"uvim-size-probe\");\n";
    }

    std::error_code ec;
    fs::path tempDir = fs::temp_directory_path(ec);
    if(ec)
        tempDir = ".";

    const auto stamp = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    fs::path tempPath =
        tempDir / ("uvim_size_probe_" + stamp + (cSource ? ".c" : ".cpp"));

    {
        std::ofstream out(tempPath);
        if(!out)
            return values;
        out << bufferTextWithProbe(lines, insertAfterLine, assertions);
    }

    fs::path sourceDir = ".";
    if(!filePath.empty())
    {
        fs::path original(filePath);
        if(original.has_parent_path())
            sourceDir = original.parent_path();
    }

    const auto options = cppSizeProbeOptions(filePath);
    std::vector<std::string> args{
        cSource ? "clang" : "clang++",
        cSource ? "-std=c11" : "-std=c++20"};
    args.insert(args.end(), options.flags.begin(), options.flags.end());
    args.insert(
        args.end(),
        {"-fsyntax-only",
         "-ferror-limit=0",
         "-Wno-everything",
         "-fno-color-diagnostics",
         "-iquote",
         fs::absolute(sourceDir).string(),
         tempPath.string()});
    ProcessPipe pipe;
    const bool started = pipe.open(args, "r", true, options.directory);
    const std::string output = started ? pipe.readAll() : "";
    pipe.close();
    if(error)
    {
        static const std::regex diagnostic(
            R"((?:fatal error|error): ([^\n\r]+))");
        std::smatch match;
        if(!started)
            *error = "could not start clang";
        else if(std::regex_search(output, match, diagnostic))
            *error = match[1].str();
    }
    fs::remove(tempPath, ec);
    const auto parsed = parseProbeResultsFromClangOutput(output);
    for(size_t i = 0; i < requests.size(); ++i)
    {
        auto found = parsed.find(requests[i].expectedValue);
        if(found != parsed.end())
            values[i] = found->second;
    }
    return values;
}

std::vector<std::optional<long long>> runSizeProbes(
    const std::vector<std::string>& lines,
    const std::string& filePath,
    const std::vector<std::string>& expressions,
    int insertAfterLine,
    std::string* error = nullptr)
{
    std::vector<ProbeRequest> requests;
    requests.reserve(expressions.size());
    for(size_t i = 0; i < expressions.size(); ++i)
    {
        requests.push_back(
            {"sizeof(" + expressions[i] + ")", -static_cast<long long>(i + 1)});
    }
    return runAssertionProbes(
        lines,
        filePath,
        requests,
        insertAfterLine,
        error);
}

std::vector<std::string> sizeExpressionsForSymbol(std::string_view symbol)
{
    std::vector<std::string> expressions;
    expressions.emplace_back(symbol);
    expressions.emplace_back("struct " + std::string(symbol));
    expressions.emplace_back("class " + std::string(symbol));
    expressions.emplace_back("union " + std::string(symbol));
    return expressions;
}

std::string cursorExpressionAt(
    const std::vector<std::string>& lines,
    int cursorY,
    int cursorX)
{
    if(cursorY < 0 || cursorY >= static_cast<int>(lines.size()))
        return {};

    const std::string& line = lines[(size_t)cursorY];
    if(line.empty())
        return {};

    int x = std::clamp(cursorX, 0, static_cast<int>(line.size()) - 1);
    if(!CppNavigationUtilities::isIdent(line[(size_t)x]))
        return {};

    int identStart = x;
    while(identStart > 0 &&
          CppNavigationUtilities::isIdent(line[(size_t)identStart - 1]))
        --identStart;

    int left = identStart;
    while(left >= 2 && line[(size_t)left - 1] == ':' &&
          line[(size_t)left - 2] == ':')
    {
        left -= 2;
        while(left > 0 &&
              CppNavigationUtilities::isIdent(line[(size_t)left - 1]))
            --left;
    }
    // Preserve member access so sizeof sees the expression in its local scope.
    while(left > 0)
    {
        int previous = left;
        if(line[static_cast<size_t>(left - 1)] == '.')
            previous = left - 1;
        else if(
            left >= 2 && line[static_cast<size_t>(left - 2)] == '-' &&
            line[static_cast<size_t>(left - 1)] == '>')
            previous = left - 2;
        else
            break;
        int start = previous;
        while(start > 0 && (CppNavigationUtilities::isIdent(
                                line[static_cast<size_t>(start - 1)]) ||
                            line[static_cast<size_t>(start - 1)] == ':'))
            --start;
        if(start == previous)
            break;
        left = start;
    }

    int right = x + 1;
    while(right < static_cast<int>(line.size()) &&
          CppNavigationUtilities::isIdent(line[(size_t)right]))
        ++right;

    if(right < static_cast<int>(line.size()) && line[(size_t)right] == '<')
    {
        int depth = 0;
        int pos = right;
        while(pos < static_cast<int>(line.size()))
        {
            const char ch = line[(size_t)pos];
            if(ch == '<')
                ++depth;
            else if(ch == '>')
            {
                --depth;
                if(depth == 0)
                {
                    right = pos + 1;
                    break;
                }
            }
            ++pos;
        }
    }

    return trimCopy(
        std::string_view(line).substr(
            static_cast<size_t>(left),
            static_cast<size_t>(right - left)));
}

std::string stripTypeKeyword(std::string expression)
{
    for(std::string_view keyword : {"struct ", "class ", "union "})
    {
        if(expression.rfind(keyword, 0) == 0)
            return expression.substr(keyword.size());
    }
    return expression;
}

std::string methodReceiverAt(
    const std::vector<std::string>& lines,
    int cursorY,
    int cursorX,
    const std::string& expression)
{
    if(cursorY < 0 || cursorY >= static_cast<int>(lines.size()))
        return {};
    const auto& line = lines[cursorY];
    size_t end = static_cast<size_t>(std::max(0, cursorX));
    while(end < line.size() && CppNavigationUtilities::isIdent(line[end]))
        ++end;
    if(end < line.size() && line[end] == '<')
    {
        int depth = 0;
        do
        {
            if(line[end] == '<')
                ++depth;
            else if(line[end] == '>')
                --depth;
            ++end;
        } while(end < line.size() && depth > 0);
    }
    while(end < line.size() &&
          std::isspace(static_cast<unsigned char>(line[end])))
        ++end;
    if(end >= line.size() || line[end] != '(')
        return {};
    const auto dot = expression.rfind('.');
    const auto arrow = expression.rfind("->");
    if(arrow != std::string::npos && (dot == std::string::npos || arrow > dot))
        return "(*" + expression.substr(0, arrow) + ")";
    return dot == std::string::npos ? std::string{} : expression.substr(0, dot);
}

std::string typeNameFromExpression(std::string expression)
{
    expression = stripTypeKeyword(std::move(expression));
    size_t paren = expression.find('(');
    if(text_utils::is_found(paren))
        expression = expression.substr(0, paren);

    while(!expression.empty() &&
          (expression.back() == '*' || expression.back() == '&' ||
           std::isspace((unsigned char)expression.back())))
    {
        expression.pop_back();
    }

    size_t start = 0;
    while(start < expression.size() &&
          (std::isspace((unsigned char)expression[start]) ||
           expression[start] == '*'))
        ++start;
    expression = expression.substr(start);

    const std::string base = SymbolPopupUtilities::lastQualifier(expression);
    return base.empty() ? expression : base;
}

std::optional<std::string> findResolvableTypeName(
    const std::vector<SourceDocument>& documents,
    const std::vector<std::string>& candidates)
{
    for(const std::string& candidate : candidates)
    {
        std::string typeName = typeNameFromExpression(candidate);
        if(typeName.empty())
            continue;
        if(findTypeDefinition(documents, typeName))
            return typeName;
    }
    return std::nullopt;
}

std::optional<std::string> resolveLocalVariableType(
    const std::vector<std::string>& lines,
    const std::string& symbol,
    int cursorY,
    int cursorX)
{
    int y = -1;
    int x = 0;
    if(!CppNavigationUtilities::searchLocalDefinition(
           lines,
           symbol,
           cursorY,
           cursorX,
           y,
           x))
    {
        for(int row = std::min(cursorY, (int)lines.size() - 1); row >= 0; --row)
        {
            size_t pos = lines[row].find(symbol);
            if(text_utils::is_not_found(pos) ||
               text_utils::is_not_found(lines[row].find(';')))
                continue;
            const bool validStart =
                pos == 0 ||
                !CppNavigationUtilities::isIdent(lines[row][pos - 1]);
            const size_t after = pos + symbol.size();
            const bool validEnd =
                after >= lines[row].size() ||
                !CppNavigationUtilities::isIdent(lines[row][after]);
            if(!validStart || !validEnd)
                continue;
            if(after < lines[row].size() && lines[row][after] == '.')
                continue;
            if(after + 1 < lines[row].size() && lines[row][after] == '-' &&
               lines[row][after + 1] == '>')
                continue;
            if(pos > 0 && text_utils::is_found(lines[row].rfind('.', pos - 1)))
                continue;
            std::string prefix = trimCopy(
                std::string_view(lines[row])
                    .substr(0, static_cast<size_t>(pos)));
            if(prefix == "return" || prefix == "co_return" || prefix == "throw")
                continue;
            y = row;
            x = (int)pos;
            break;
        }
        if(y < 0)
            return std::nullopt;
    }

    std::string type =
        SymbolPopupUtilities::extractTypeBeforeName(lines[y], symbol);
    if(type.empty())
    {
        auto trim = [](std::string_view text)
        {
            while(!text.empty() && std::isspace((unsigned char)text.front()))
                text.remove_prefix(1);
            while(!text.empty() && std::isspace((unsigned char)text.back()))
                text.remove_suffix(1);
            return std::string(text);
        };

        size_t pos = lines[y].find(symbol);
        if(text_utils::is_found(pos))
        {
            std::string prefix =
                trim(std::string_view(lines[y]).substr(0, pos));
            size_t eq = prefix.find('=');
            if(text_utils::is_found(eq))
                prefix = prefix.substr(eq + 1);
            size_t lastSpace = prefix.find_last_of(" \t");
            if(text_utils::is_found(lastSpace))
                type = trim(prefix.substr(lastSpace + 1));
            else
                type = trim(prefix);
        }
    }
    if(type.empty() || type == "auto")
        return std::nullopt;
    return type;
}

void pushUniqueExpression(
    std::vector<std::string>& expressions,
    const std::string& expression)
{
    if(expression.empty())
        return;
    if(std::find(expressions.begin(), expressions.end(), expression) ==
       expressions.end())
        expressions.push_back(expression);
}

SizeProbeResult resolveSizeExpression(
    const std::vector<std::string>& lines,
    const std::string& filePath,
    const std::string& symbol,
    const std::string& cursorExpression,
    int cursorY,
    int cursorX)
{
    const int currentLine = std::clamp(cursorY, 0, (int)lines.size() - 1);
    SizeProbeResult result;
    const std::string receiver =
        methodReceiverAt(lines, cursorY, cursorX, cursorExpression);
    const bool dereferencedReceiver =
        receiver.starts_with("(*") && receiver.ends_with(')');
    const std::string lookupSymbol =
        dereferencedReceiver ? receiver.substr(2, receiver.size() - 3)
        : !receiver.empty()  ? receiver
                             : symbol;
    std::optional<std::string> localType =
        resolveLocalVariableType(lines, lookupSymbol, cursorY, cursorX);

    std::vector<std::string> expressions;
    pushUniqueExpression(expressions, cursorExpression);
    // A method has no object size. Keep data/function-pointer members first,
    // then inspect the receiver when sizeof(member) is invalid.
    pushUniqueExpression(expressions, receiver);
    for(const std::string& expression : sizeExpressionsForSymbol(symbol))
        pushUniqueExpression(expressions, expression);

    std::vector<std::string> currentLineExpressions = expressions;
    if(localType)
        pushUniqueExpression(currentLineExpressions, *localType);

    const std::vector<std::optional<long long>> currentLineSizes =
        runSizeProbes(
            lines,
            filePath,
            currentLineExpressions,
            currentLine,
            &result.error);
    for(size_t i = 0;
        i < currentLineExpressions.size() && i < currentLineSizes.size();
        ++i)
    {
        if(currentLineSizes[i])
        {
            result.size = currentLineSizes[i];
            result.expression = currentLineExpressions[i];
            if(localType && result.expression == lookupSymbol)
                result.memberTypeExpression = *localType;
            else if(
                localType && dereferencedReceiver &&
                result.expression == receiver)
            {
                result.memberTypeExpression = *localType;
                if(auto star = result.memberTypeExpression.rfind('*');
                   star != std::string::npos)
                    result.memberTypeExpression.replace(star, 1, " ");
                result.memberTypeExpression =
                    trimCopy(result.memberTypeExpression);
            }
            result.insertAfterLine = currentLine;
            return result;
        }
    }

    std::string eofError;
    const std::vector<std::optional<long long>> eofSizes = runSizeProbes(
        lines,
        filePath,
        expressions,
        (int)lines.size(),
        &eofError);
    if(result.error.empty())
        result.error = std::move(eofError);
    for(size_t i = 0; i < expressions.size() && i < eofSizes.size(); ++i)
    {
        if(eofSizes[i])
        {
            result.size = eofSizes[i];
            result.expression = expressions[i];
            result.insertAfterLine = (int)lines.size();
            return result;
        }
    }

    return result;
}

int findTypeDefinitionLine(
    const std::vector<std::string>& lines,
    const std::string& typeName)
{
    for(int y = 0; y < (int)lines.size(); ++y)
    {
        for(std::string_view keyword : {"struct ", "class ", "union "})
        {
            size_t pos = lines[y].find(std::string(keyword) + typeName);
            if(text_utils::is_not_found(pos))
                continue;
            const size_t after = pos + keyword.size() + typeName.size();
            if(after >= lines[y].size() ||
               !CppNavigationUtilities::isIdent(lines[y][after]))
                return y;
        }
    }
    return -1;
}

std::optional<TypeDefinition> findTypeDefinition(
    const std::vector<SourceDocument>& documents,
    const std::string& typeName)
{
    for(const SourceDocument& document : documents)
    {
        const int line = findTypeDefinitionLine(document.lines, typeName);
        if(line >= 0)
            return TypeDefinition{&document, line};
    }
    return std::nullopt;
}

TypeBody collectTypeBody(const std::vector<std::string>& lines, int startLine)
{
    TypeBody result;
    int depth = 0;
    bool seenOpen = false;

    for(int y = startLine; y < (int)lines.size(); ++y)
    {
        const std::string& line = lines[y];
        for(char ch : line)
        {
            if(ch == '{')
            {
                if(seenOpen && depth > 0)
                    result.body.push_back(ch);
                ++depth;
                seenOpen = true;
                continue;
            }
            if(ch == '}')
            {
                --depth;
                if(depth <= 0 && seenOpen)
                {
                    result.closingLine = y;
                    return result;
                }
                if(seenOpen && depth > 0)
                    result.body.push_back(ch);
                continue;
            }
            if(seenOpen && depth > 0)
                result.body.push_back(ch);
        }
        if(seenOpen && depth > 0)
            result.body.push_back('\n');
    }

    return result;
}

std::vector<std::string> splitTopLevel(std::string_view text, char delimiter)
{
    std::vector<std::string> parts;
    int angle = 0;
    int paren = 0;
    int bracket = 0;
    int brace = 0;
    size_t start = 0;

    for(size_t i = 0; i < text.size(); ++i)
    {
        const char ch = text[i];
        if(ch == '<')
            ++angle;
        else if(ch == '>' && angle > 0)
            --angle;
        else if(ch == '(')
            ++paren;
        else if(ch == ')' && paren > 0)
            --paren;
        else if(ch == '[')
            ++bracket;
        else if(ch == ']' && bracket > 0)
            --bracket;
        else if(ch == '{')
            ++brace;
        else if(ch == '}' && brace > 0)
            --brace;
        else if(
            ch == delimiter && angle == 0 && paren == 0 && bracket == 0 &&
            brace == 0)
        {
            parts.emplace_back(text.substr(start, i - start));
            start = i + 1;
        }
    }

    parts.emplace_back(text.substr(start));
    return parts;
}

std::string trimCopy(std::string_view text)
{
    while(!text.empty() && std::isspace((unsigned char)text.front()))
        text.remove_prefix(1);
    while(!text.empty() && std::isspace((unsigned char)text.back()))
        text.remove_suffix(1);
    return std::string(text);
}

std::optional<std::string> memberNameFromDeclarator(std::string declarator)
{
    size_t eq = declarator.find('=');
    if(text_utils::is_found(eq))
        declarator = declarator.substr(0, eq);
    for(size_t colon = 0; colon < declarator.size(); ++colon)
    {
        if(declarator[colon] != ':')
            continue;
        const bool scopeLeft = colon > 0 && declarator[colon - 1] == ':';
        const bool scopeRight =
            colon + 1 < declarator.size() && declarator[colon + 1] == ':';
        if(!scopeLeft && !scopeRight)
        {
            declarator = declarator.substr(0, colon);
            break;
        }
    }
    size_t bracket = declarator.find('[');
    if(text_utils::is_found(bracket))
        declarator = declarator.substr(0, bracket);

    int end = (int)declarator.size() - 1;
    while(end >= 0 && !CppNavigationUtilities::isIdent(declarator[(size_t)end]))
        --end;
    if(end < 0)
        return std::nullopt;

    int start = end;
    while(start >= 0 &&
          CppNavigationUtilities::isIdent(declarator[(size_t)start]))
        --start;
    std::string name =
        declarator.substr((size_t)start + 1, (size_t)(end - start));
    if(name.empty())
        return std::nullopt;
    return name;
}

std::optional<ParsedMember> parseNestedTypeDeclaration(
    std::string_view statement)
{
    for(std::string_view keyword : {"struct ", "class ", "union "})
    {
        size_t pos = statement.find(keyword);
        if(text_utils::is_not_found(pos))
            continue;
        if(pos > 0 && CppNavigationUtilities::isIdent(statement[pos - 1]))
            continue;

        size_t nameStart = pos + keyword.size();
        while(nameStart < statement.size() &&
              std::isspace((unsigned char)statement[nameStart]))
            ++nameStart;

        size_t nameEnd = nameStart;
        while(nameEnd < statement.size() &&
              CppNavigationUtilities::isIdent(statement[nameEnd]))
            ++nameEnd;
        if(nameEnd == nameStart)
            continue;

        size_t brace = statement.find('{', nameEnd);
        if(text_utils::is_not_found(brace))
            continue;

        ParsedMember parsed;
        parsed.name =
            trimCopy(statement.substr(nameStart, nameEnd - nameStart));
        parsed.nestedTypeName = parsed.name;
        return parsed;
    }
    return std::nullopt;
}

std::string typeNameFromMemberStatement(
    const std::string& statement,
    const std::string& memberName)
{
    std::string type =
        SymbolPopupUtilities::extractTypeBeforeName(statement, memberName);
    if(type.empty())
        return {};
    if(type.find('*') != std::string::npos ||
       type.find('&') != std::string::npos ||
       statement.find('[', statement.find(memberName) + memberName.size()) !=
           std::string::npos)
        return {};
    return typeNameFromExpression(type);
}

std::vector<ParsedMember> parseMembers(const std::string& body)
{
    std::vector<ParsedMember> members;
    for(std::string statement : splitTopLevel(body, ';'))
    {
        statement = trimCopy(statement);
        if(statement.empty() || statement == "public:" ||
           statement == "private:" || statement == "protected:")
            continue;
        if(statement.rfind("using ", 0) == 0 ||
           statement.rfind("typedef ", 0) == 0)
            continue;
        if(auto nestedType = parseNestedTypeDeclaration(statement))
        {
            members.push_back(std::move(*nestedType));
            continue;
        }
        if(text_utils::is_found(statement.find('(')))
        {
            const size_t closeBrace = statement.rfind('}');
            if(text_utils::is_not_found(closeBrace) ||
               closeBrace + 1 >= statement.size())
                continue;
            statement =
                trimCopy(std::string_view(statement).substr(closeBrace + 1));
            if(statement.empty() || text_utils::is_found(statement.find('(')))
                continue;
        }

        const auto declarators = splitTopLevel(statement, ',');
        for(size_t i = 0; i < declarators.size(); ++i)
        {
            std::string declarator = trimCopy(declarators[i]);
            if(auto name = memberNameFromDeclarator(std::move(declarator)))
            {
                ParsedMember parsed;
                parsed.name = *name;
                parsed.typeName = typeNameFromMemberStatement(statement, *name);
                members.push_back(std::move(parsed));
            }
        }
    }
    return members;
}

std::vector<MemberInfo> resolveMembersForType(
    const std::vector<SourceDocument>& documents,
    const std::vector<std::string>& lines,
    const std::string& filePath,
    const std::string& typeName,
    int insertAfterLine,
    int indent,
    int depth,
    bool* complete = nullptr);

void appendChildMembers(
    MemberInfo& parent,
    const std::vector<SourceDocument>& documents,
    const std::vector<std::string>& lines,
    const std::string& filePath,
    const std::string& typeName,
    int insertAfterLine,
    int depth)
{
    if(typeName.empty())
        return;

    constexpr int maxNestedStructDepth = 3;
    if(depth >= maxNestedStructDepth)
    {
        if(findTypeDefinition(documents, typeName))
        {
            MemberInfo marker;
            marker.name = "...";
            marker.indent = parent.indent + 1;
            marker.truncated = true;
            parent.children.push_back(std::move(marker));
        }
        return;
    }

    parent.children = resolveMembersForType(
        documents,
        lines,
        filePath,
        typeName,
        insertAfterLine,
        parent.indent + 1,
        depth + 1,
        &parent.layoutComplete);
}

std::vector<MemberInfo> resolveMembersForType(
    const std::vector<SourceDocument>& documents,
    const std::vector<std::string>& lines,
    const std::string& filePath,
    const std::string& typeName,
    int insertAfterLine,
    int indent,
    int depth,
    bool* complete)
{
    if(complete)
        *complete = false;
    std::vector<MemberInfo> members;
    const std::optional<TypeDefinition> definition =
        findTypeDefinition(documents, typeName);
    if(!definition || !definition->document)
        return members;

    const TypeBody typeBody =
        collectTypeBody(definition->document->lines, definition->line);
    const bool definitionInPrimary =
        !documents.empty() && definition->document == &documents.front();
    const bool probeInsideType =
        definitionInPrimary && typeBody.closingLine > definition->line;
    const int memberProbeLine =
        probeInsideType ? typeBody.closingLine - 1 : insertAfterLine;

    const std::vector<ParsedMember> parsedMembers = parseMembers(typeBody.body);
    std::vector<ProbeRequest> sizeRequests;
    std::vector<ProbeRequest> offsetRequests;
    std::vector<size_t> offsetMemberIndexes;
    sizeRequests.reserve(parsedMembers.size());
    offsetRequests.reserve(parsedMembers.size());
    offsetMemberIndexes.reserve(parsedMembers.size());

    for(size_t i = 0; i < parsedMembers.size(); ++i)
    {
        const ParsedMember& parsed = parsedMembers[i];
        const long long expected = -static_cast<long long>(i + 1);
        if(!parsed.nestedTypeName.empty())
        {
            sizeRequests.push_back(
                {"sizeof(" + parsed.nestedTypeName + ")", expected});
            continue;
        }

        const std::string memberExpression =
            probeInsideType ? parsed.name
                            : "(((" + typeName + "*)0)->" + parsed.name + ")";
        sizeRequests.push_back({"sizeof(" + memberExpression + ")", expected});

        offsetMemberIndexes.push_back(i);
        offsetRequests.push_back(
            {"__builtin_offsetof(" + typeName + ", " + parsed.name + ")",
             expected});
    }

    const std::vector<std::optional<long long>> memberSizes =
        runAssertionProbes(lines, filePath, sizeRequests, memberProbeLine);
    const std::vector<std::optional<long long>> memberOffsets =
        runAssertionProbes(lines, filePath, offsetRequests, insertAfterLine);

    std::vector<std::optional<long long>> offsetsByMember(parsedMembers.size());
    for(size_t i = 0;
        i < offsetMemberIndexes.size() && i < memberOffsets.size();
        ++i)
    {
        offsetsByMember[offsetMemberIndexes[i]] = memberOffsets[i];
    }

    bool allOffsetsKnown = !parsedMembers.empty();
    for(size_t i = 0; i < parsedMembers.size(); ++i)
    {
        const ParsedMember& parsed = parsedMembers[i];
        if(!parsed.nestedTypeName.empty())
        {
            MemberInfo info;
            info.name = parsed.nestedTypeName;
            info.indent = indent;
            if(i < memberSizes.size())
                info.size = memberSizes[i];
            appendChildMembers(
                info,
                documents,
                lines,
                filePath,
                parsed.nestedTypeName,
                insertAfterLine,
                depth);
            if(info.size || !info.children.empty())
                members.push_back(std::move(info));
            continue;
        }

        MemberInfo info;
        info.name = parsed.name;
        info.indent = indent;
        if(i < memberSizes.size())
            info.size = memberSizes[i];
        info.offset = offsetsByMember[i];
        allOffsetsKnown = allOffsetsKnown && info.size && info.offset;
        appendChildMembers(
            info,
            documents,
            lines,
            filePath,
            parsed.typeName,
            insertAfterLine,
            depth);
        if(info.size)
            members.push_back(std::move(info));
    }

    if(complete)
        *complete = allOffsetsKnown;
    return members;
}

std::vector<MemberInfo> resolveMembers(
    const std::vector<std::string>& lines,
    const std::string& filePath,
    const std::string& typeExpression,
    const std::string& symbol,
    int insertAfterLine,
    bool& complete)
{
    // A pointer's storage is the pointer itself, not its pointee's fields.
    if(typeExpression.find('*') != std::string::npos)
        return {};
    const std::vector<SourceDocument> documents =
        collectSourceDocuments(lines, filePath);
    const std::optional<std::string> resolvedTypeName =
        findResolvableTypeName(documents, {typeExpression, symbol});
    if(!resolvedTypeName)
        return {};

    return resolveMembersForType(
        documents,
        lines,
        filePath,
        *resolvedTypeName,
        insertAfterLine,
        0,
        0,
        &complete);
}

std::string buildSizePopupText(
    const std::string& expression,
    long long total,
    const std::vector<MemberInfo>& members,
    bool complete,
    std::vector<Editor::SizePopupEntry>& entries,
    bool mlang,
    bool functionReturn,
    const std::string& returnType)
{
    std::ostringstream out;
    if(functionReturn)
        out << "Return value of " << expression << ": " << total << " bytes";
    else
        out << (mlang ? "storage(" : "sizeof(") << expression << ") = " << total
            << " bytes";
    if(!returnType.empty())
        out << " | " << returnType;
    entries.push_back({0, expression, 0, total, false});
    int row = 0;
    std::function<void(
        const std::vector<MemberInfo>&,
        long long,
        std::optional<long long>,
        bool,
        const std::string&)>
        appendMembers = [&](const std::vector<MemberInfo>& current,
                            long long extent,
                            std::optional<long long> base,
                            bool known,
                            const std::string& prefix)
    {
        std::optional<long long> end;
        auto padding = [&](long long offset, long long size, int indent)
        {
            out << '\n'
                << std::string(indent * 2, ' ') << "pad: " << size << " bytes";
            entries.push_back(
                {++row,
                 prefix + "<padding>",
                 base ? std::optional<long long>(*base + offset) : std::nullopt,
                 size,
                 true});
        };
        for(const MemberInfo& member : current)
        {
            const std::string indent(member.indent * 2, ' ');
            if(member.truncated)
            {
                out << '\n' << indent << "...";
                ++row;
                continue;
            }
            if(known && end && member.offset && *member.offset > *end)
                padding(*end, *member.offset - *end, member.indent);
            out << '\n' << indent << member.name << ": ";
            if(member.size)
                out << *member.size << " bytes";
            else
                out << "?";
            const auto offset =
                base && member.offset
                    ? std::optional<long long>(*base + *member.offset)
                    : std::nullopt;
            entries.push_back(
                {++row, prefix + member.name, offset, member.size, false});
            if(!member.children.empty())
                appendMembers(
                    member.children,
                    member.size.value_or(0),
                    offset,
                    member.layoutComplete,
                    prefix + member.name + ".");
            if(member.offset && member.size)
                end = std::max(end.value_or(0), *member.offset + *member.size);
        }
        if(known && end && *end < extent)
            padding(
                *end,
                extent - *end,
                current.empty() ? 0 : current.back().indent);
    };
    appendMembers(members, total, 0, complete, "");
    return out.str();
}
} // namespace

void Editor::openSizePopupForCursor()
{
    closeSymbolPopup();

    if(!currentBuffer || !lines || !cursorX || !cursorY)
        return;

    if(!isFileType<FileType::Cpp>() && !isFileType<FileType::Mla>())
    {
        setStatusMessage("gs: C/C++ and Mlang only");
        needsFullRedraw = true;
        return;
    }

    const std::string symbol = getSymbolUnderCursor();
    if(symbol.empty())
    {
        setStatusMessage("gs: no symbol under cursor");
        needsFullRedraw = true;
        return;
    }

    if(lines->empty())
    {
        setStatusMessage("gs: empty buffer");
        needsFullRedraw = true;
        return;
    }

    const std::string cursorExpression =
        cursorExpressionAt(*lines, *cursorY, *cursorX);
    const auto call =
        cursorCallAt(*lines, *cursorY, *cursorX, cursorExpression);
    std::string expression, returnType;
    long long total = 0;
    bool complete = false;
    std::vector<MemberInfo> members;
    std::function<
        std::vector<MemberInfo>(const std::vector<DebugTypeMember>&, int)>
        convert = [&](const std::vector<DebugTypeMember>& fields, int indent)
    {
        std::vector<MemberInfo> result;
        for(const auto& field : fields)
        {
            MemberInfo member;
            member.name = field.name;
            member.size = (field.offsetBits % 8 + field.sizeBits + 7) / 8;
            member.offset = field.offsetBits / 8;
            member.indent = indent;
            if(field.sizeBits % 8 || field.offsetBits % 8)
                member.name += " (" + std::to_string(field.sizeBits) +
                               " bits at bit +" +
                               std::to_string(field.offsetBits) + ")";
            member.children = convert(field.children, indent + 1);
            member.layoutComplete =
                field.complete && std::all_of(
                                      field.children.begin(),
                                      field.children.end(),
                                      [](const auto& child)
                                      {
                                          return child.sizeBits % 8 == 0 &&
                                                 child.offsetBits % 8 == 0;
                                      });
            result.push_back(std::move(member));
        }
        return result;
    };
    if(call)
    {
        std::string error;
        auto layout = isFileType<FileType::Mla>() ? probeMlangSize(
                                                        *lines,
                                                        currentBuffer->filename,
                                                        call->callee,
                                                        *cursorY + 1,
                                                        error,
                                                        true,
                                                        *cursorX + 1)
                                                  : probeCppReturnLayout(
                                                        *lines,
                                                        currentBuffer->filename,
                                                        *call,
                                                        *cursorX,
                                                        error);
        if(!layout)
        {
            setStatusMessage(
                "gs: " +
                (error.empty()
                     ? "could not resolve the return type for " + call->callee
                     : error));
            needsFullRedraw = true;
            return;
        }
        if(layout->typeName == "void")
        {
            setStatusMessage(
                "gs: " + call->callee +
                " returns void (no return-value storage)");
            needsFullRedraw = true;
            return;
        }
        expression = call->expression;
        for(char& ch : expression)
            if(ch == '\n' || ch == '\r' || ch == '\t')
                ch = ' ';
        if(!isFileType<FileType::Mla>())
            returnType = layout->typeName;
        total = (layout->sizeBits + 7) / 8;
        members = convert(layout->members, 0);
        complete =
            layout->complete && std::all_of(
                                    layout->members.begin(),
                                    layout->members.end(),
                                    [](const auto& member)
                                    {
                                        return member.sizeBits % 8 == 0 &&
                                               member.offsetBits % 8 == 0;
                                    });
    }
    else if(isFileType<FileType::Mla>())
    {
        expression = cursorExpression.empty() ? symbol : cursorExpression;
        std::string error;
        auto layout = probeMlangSize(
            *lines,
            currentBuffer->filename,
            expression,
            *cursorY + 1,
            error);
        if(!layout)
        {
            setStatusMessage("gs: " + error);
            needsFullRedraw = true;
            return;
        }
        members = convert(layout->members, 0);
        total = (layout->sizeBits + 7) / 8;
        complete =
            layout->complete && std::all_of(
                                    layout->members.begin(),
                                    layout->members.end(),
                                    [](const auto& member)
                                    {
                                        return member.sizeBits % 8 == 0 &&
                                               member.offsetBits % 8 == 0;
                                    });
    }
    else
    {
        SizeProbeResult result = resolveSizeExpression(
            *lines,
            currentBuffer->filename,
            symbol,
            cursorExpression,
            *cursorY,
            *cursorX);
        if(!result.size)
        {
            setStatusMessage(
                "gs: clang could not resolve size for " + symbol +
                (result.error.empty() ? "" : ": " + result.error));
            needsFullRedraw = true;
            return;
        }
        expression = result.expression;
        total = *result.size;
        const std::string memberTypeExpression =
            result.memberTypeExpression.empty() ? result.expression
                                                : result.memberTypeExpression;
        members = resolveMembers(
            *lines,
            currentBuffer->filename,
            memberTypeExpression,
            symbol,
            result.insertAfterLine,
            complete);
        static const std::unordered_set<std::string> primitives =
            {"bool",
             "char",
             "signed char",
             "unsigned char",
             "short",
             "unsigned short",
             "int",
             "unsigned",
             "unsigned int",
             "long",
             "unsigned long",
             "long long",
             "unsigned long long",
             "float",
             "double",
             "long double",
             "wchar_t",
             "char8_t",
             "char16_t",
             "char32_t",
             "size_t",
             "std::size_t",
             "void"};
        if(members.empty() && !isCSource(currentBuffer->filename) &&
           !primitives.contains(memberTypeExpression) &&
           memberTypeExpression.find('*') == std::string::npos)
        {
            if(auto layout = probeCppTypeLayout(
                   *lines,
                   currentBuffer->filename,
                   expression,
                   *cursorY + 1,
                   memberTypeExpression);
               layout && layout->sizeBits == total * 8 &&
               !layout->members.empty())
            {
                members = convert(layout->members, 0);
                complete = layout->complete &&
                           std::all_of(
                               layout->members.begin(),
                               layout->members.end(),
                               [](const auto& member)
                               {
                                   return member.sizeBits % 8 == 0 &&
                                          member.offsetBits % 8 == 0;
                               });
            }
        }
    }
    symbolPopupText = buildSizePopupText(
        expression,
        total,
        members,
        complete,
        sizePopupEntries,
        isFileType<FileType::Mla>(),
        call.has_value(),
        returnType);
    sizePopupBaseText = symbolPopupText;
    sizePopupSelection = 0;
    symbolPopupActive = true;
    symbolPopupModal = true;
    symbolPopupCursorX = *cursorX;
    symbolPopupCursorY = *cursorY;
    symbolPopupScroll = 0;
    setStatusMessage(
        "Size: dim red = padding | Ctrl-j/k select | j/k scroll | q close");
    needsFullRedraw = true;
}

void Editor::browseSizePopup(int delta)
{
    if(sizePopupEntries.empty())
        return;
    sizePopupSelection = std::clamp(
        sizePopupSelection + delta,
        0,
        static_cast<int>(sizePopupEntries.size()) - 1);
    const auto& entry = sizePopupEntries[sizePopupSelection];
    std::ostringstream detail;
    detail << sizePopupBaseText << "\n\nSelected: " << entry.name;
    if(entry.size)
        detail << " | " << *entry.size << " bytes";
    if(entry.offset)
        detail << " | offset +0x" << std::hex << *entry.offset << std::dec;
    else
        detail << " | offset unavailable (type or incomplete layout)";
    detail << "\nByte map (offsets in hex; ## member, -- padding)";
    if(entry.offset && entry.size)
    {
        constexpr long long limit = 128;
        const long long count = std::min(*entry.size, limit);
        for(long long byte = 0; byte < count; ++byte)
        {
            if(byte % 8 == 0)
                detail << "\n+0x" << std::hex << (*entry.offset + byte)
                       << std::dec << ":";
            bool padding = entry.padding;
            for(const auto& range : sizePopupEntries)
                if(range.padding && range.offset && range.size &&
                   *entry.offset + byte >= *range.offset &&
                   *entry.offset + byte < *range.offset + *range.size)
                    padding = true;
            detail << (padding ? " --" : " ##");
        }
        if(*entry.size > limit)
            detail << "\n... first 128 bytes shown";
    }
    detail << "\nRaw hex: available with :ss in a paused debugger";
    symbolPopupText = detail.str();
    const auto layout = layoutSymbolPopup(*this);
    symbolPopupScroll =
        layout.detailRow < 0
            ? 0
            : std::clamp(
                  layout.detailRow + std::min(4, layout.visibleRows) -
                      layout.visibleRows,
                  0,
                  layout.maxScroll());
    needsFullRedraw = true;
}

#else
void Editor::browseSizePopup(int) {}

void Editor::openSizePopupForCursor()
{
    setStatusMessage("size probes disabled in this build");
}
#endif
