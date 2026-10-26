#include "structural_navigation.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <unordered_set>

namespace structural_navigation
{
namespace
{
struct Token
{
    std::string text;
    Position position;
    bool identifier = false;
    int mate = -1;
    int parent = -1;
    std::string spelling{};
};

struct Function
{
    int name;
    int body;
    int end; // exclusive; includes the closing brace in the function's scope
    bool exactStart = false;
};

bool identStart(char c)
{
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

bool ident(char c)
{
    return identStart(c) || std::isdigit(static_cast<unsigned char>(c));
}

bool keyword(std::string_view word)
{
    static const std::unordered_set<std::string_view> words =
        {"if",           "else",
         "elif",         "for",
         "foreach",      "while",
         "do",           "switch",
         "case",         "catch",
         "try",          "finally",
         "return",       "throw",
         "yield",        "co_return",
         "sizeof",       "alignof",
         "decltype",     "typeof",
         "typeid",       "noexcept",
         "requires",     "static_cast",
         "dynamic_cast", "reinterpret_cast",
         "const_cast",   "new",
         "delete",       "assert",
         "with",         "match",
         "except",       "def",
         "class",        "struct",
         "namespace",    "function",
         "fn",           "func",
         "import",       "from",
         "using",        "in",
         "is",           "and",
         "or",           "not",
         "await",        "async",
         "lambda",       "true",
         "false",        "None",
         "null",         "nullptr",
         "break",        "continue",
         "pass",         "raise",
         "del",          "global",
         "nonlocal",     "this",
         "self",         "base",
         "super",        "const",
         "let",          "var"};
    return words.count(word) != 0;
}

std::vector<Token> tokenize(
    const std::vector<std::string>& lines,
    FileType type)
{
    std::string source;
    for(const auto& line : lines)
        source += line + '\n';
    std::vector<Token> tokens;
    int row = 0;
    int column = 0;
    size_t i = 0;
    auto advance = [&]()
    {
        if(source[i++] == '\n')
        {
            ++row;
            column = 0;
        }
        else
            ++column;
    };
    auto starts = [&](std::string_view text)
    { return source.compare(i, text.size(), text) == 0; };
    const bool python = type == FileType::Python;
    while(i < source.size())
    {
        if(std::isspace(static_cast<unsigned char>(source[i])))
        {
            advance();
            continue;
        }
        if((python && source[i] == '#') || (!python && starts("//")) ||
           (type == FileType::Cpp && source[i] == '#' &&
            lines[row].find_first_not_of(" \t") == static_cast<size_t>(column)))
        {
            do
            {
                while(i < source.size() && source[i] != '\n')
                    advance();
                // C/C++ preprocessor continuations are not executable code.
                if(type != FileType::Cpp || i == 0 || source[i - 1] != '\\')
                    break;
                advance();
            } while(i < source.size());
            continue;
        }
        if(!python && starts("/*"))
        {
            advance();
            advance();
            while(i < source.size() && !starts("*/"))
                advance();
            if(i < source.size())
            {
                advance();
                advance();
            }
            continue;
        }
        const Position position{row, column};
        const size_t literalStart = i;
        // JavaScript regex literals are opaque, just like quoted strings.
        const bool javascript =
            type == FileType::JavaScript || type == FileType::TypeScript;
        if(javascript && source[i] == '/' &&
           (tokens.empty() || tokens.back().text == "=" ||
            tokens.back().text == "(" || tokens.back().text == "," ||
            tokens.back().text == ":" || tokens.back().text == "return" ||
            tokens.back().text == "=>" || tokens.back().text == "[" ||
            tokens.back().text == "!"))
        {
            advance();
            bool characterClass = false;
            while(i < source.size() && source[i] != '\n')
            {
                const char c = source[i];
                advance();
                if(c == '\\' && i < source.size())
                    advance();
                else if(c == '[')
                    characterClass = true;
                else if(c == ']')
                    characterClass = false;
                else if(c == '/' && !characterClass)
                    break;
            }
            while(i < source.size() && ident(source[i]))
                advance();
            tokens.push_back({"literal", position});
            tokens.back().spelling =
                source.substr(literalStart, i - literalStart);
            continue;
        }
        // C++ raw literals can contain quotes, comments and arbitrary braces.
        if(type == FileType::Cpp && starts("R\""))
        {
            const size_t opening = source.find('(', i + 2);
            if(opening != std::string::npos && opening - i <= 18)
            {
                const std::string ending =
                    ")" + source.substr(i + 2, opening - i - 2) + "\"";
                const size_t closing = source.find(ending, opening + 1);
                const size_t end = closing == std::string::npos
                                       ? source.size()
                                       : closing + ending.size();
                while(i < end)
                    advance();
                tokens.push_back({"literal", position});
                tokens.back().spelling =
                    source.substr(literalStart, i - literalStart);
                continue;
            }
        }
        const bool verbatim = type == FileType::CSharp && starts("@\"");
        if(source[i] == '\'' || source[i] == '"' ||
           ((type == FileType::JavaScript || type == FileType::TypeScript) &&
            source[i] == '`') ||
           verbatim)
        {
            if(verbatim)
                advance();
            const char quote = source[i];
            const bool triple = (python || type == FileType::CSharp) &&
                                starts(std::string(3, quote));
            const std::string ending(triple ? 3 : 1, quote);
            for(size_t n = 0; n < ending.size(); ++n)
                advance();
            while(i < source.size())
            {
                if(verbatim && starts("\"\""))
                {
                    advance();
                    advance();
                }
                else if(starts(ending))
                {
                    for(size_t n = 0; n < ending.size(); ++n)
                        advance();
                    break;
                }
                else if(!verbatim && source[i] == '\\')
                {
                    advance();
                    if(i < source.size())
                        advance();
                }
                else
                    advance();
            }
            tokens.push_back({"literal", position});
            tokens.back().spelling =
                source.substr(literalStart, i - literalStart);
            continue;
        }
        const size_t begin = i;
        const bool identifier = identStart(source[i]);
        advance();
        if(identifier)
        {
            while(i < source.size() && ident(source[i]))
                advance();
        }
        else if(i < source.size())
        {
            const std::string_view pair(source.data() + begin, 2);
            if(pair == "=>" || pair == "->" || pair == "::" || pair == ":=" ||
               pair == "==" || pair == "!=" || pair == "<=" || pair == ">=" ||
               pair == "+=" || pair == "-=" || pair == "*=" || pair == "/=" ||
               pair == "&&" || pair == "||" || pair == "++" || pair == "--")
                advance();
        }
        tokens.push_back(
            {source.substr(begin, i - begin), position, identifier});
    }
    std::vector<int> stack;
    for(int j = 0; j < static_cast<int>(tokens.size()); ++j)
    {
        tokens[j].parent = stack.empty() ? -1 : stack.back();
        const auto& text = tokens[j].text;
        if(text == "(" || text == "[" || text == "{")
            stack.push_back(j);
        else if(
            !stack.empty() &&
            ((text == ")" && tokens[stack.back()].text == "(") ||
             (text == "]" && tokens[stack.back()].text == "[") ||
             (text == "}" && tokens[stack.back()].text == "{")))
        {
            const int opening = stack.back();
            stack.pop_back();
            tokens[opening].mate = j;
            tokens[j].mate = opening;
        }
    }
    return tokens;
}

int callableName(const std::vector<Token>& tokens, int opening)
{
    int name = opening - 1;
    if(name >= 0 && tokens[name].text == ">")
    {
        int depth = 1;
        while(--name >= 0)
        {
            if(tokens[name].text == ">")
                ++depth;
            else if(tokens[name].text == "<" && --depth == 0)
            {
                --name;
                break;
            }
        }
    }
    return name;
}

int indent(const std::string& line)
{
    int width = 0;
    for(char c : line)
    {
        if(c == ' ')
            ++width;
        else if(c == '\t')
            width += 8 - width % 8;
        else
            break;
    }
    return width;
}

std::vector<Function> functions(
    const std::vector<Token>& tokens,
    const std::vector<std::string>& lines,
    FileType type)
{
    std::vector<Function> result;
    const int size = static_cast<int>(tokens.size());
    if(type == FileType::Python)
    {
        for(int i = 0; i + 2 < size; ++i)
        {
            if(tokens[i].text != "def" || !tokens[i + 1].identifier ||
               tokens[i + 2].text != "(" || tokens[i + 2].mate < 0)
                continue;
            int colon = tokens[i + 2].mate + 1;
            while(colon < size && tokens[colon].text != ":")
            {
                if(tokens[colon].mate > colon)
                    colon = tokens[colon].mate;
                ++colon;
            }
            if(colon == size)
                continue;
            int end = colon + 1;
            const int signatureIndent = indent(lines[tokens[i].position.line]);
            const bool inlineBody =
                end < size &&
                tokens[end].position.line == tokens[colon].position.line;
            while(end < size)
            {
                if(tokens[end].position.line > tokens[colon].position.line &&
                   (inlineBody || indent(lines[tokens[end].position.line]) <=
                                      signatureIndent))
                    break;
                if(tokens[end].mate > end)
                    end = tokens[end].mate;
                ++end;
            }
            result.push_back({i + 1, colon + 1, end});
        }
        return result;
    }

    std::unordered_set<int> claimedBodies;
    for(int i = 0; i < size; ++i)
    {
        int name = -1;
        int body = -1;
        bool exactStart = false;
        const int candidate = callableName(tokens, i);
        if(tokens[i].text == "(" && candidate >= 0 && tokens[i].mate >= 0 &&
           tokens[candidate].identifier &&
           (!keyword(tokens[candidate].text) ||
            tokens[candidate].text == "function"))
        {
            name = candidate;
            int after = tokens[i].mate + 1;
            // Qualifiers, trailing return types, and constructor initializers.
            while(after < size && tokens[after].text != "{")
            {
                const auto& text = tokens[after].text;
                if(text == ";" || text == "=" || text == "=>" || text == "}" ||
                   text == ")" || text == "," || text == ".")
                    break;
                if(text == ":" &&
                   (type == FileType::Cpp || type == FileType::CSharp))
                {
                    // Constructor initializer lists precede the actual body.
                    ++after;
                    while(after < size && tokens[after].identifier)
                    {
                        while(after < size && tokens[after].text != "(" &&
                              tokens[after].text != "{")
                            ++after;
                        if(after == size || tokens[after].mate < 0)
                            break;
                        after = tokens[after].mate + 1;
                        if(after < size && tokens[after].text == ",")
                            ++after;
                        else
                            break;
                    }
                    continue;
                }
                if(tokens[after].mate > after)
                    after = tokens[after].mate;
                ++after;
            }
            if(after < size && tokens[after].text == "{")
                body = after;
        }
        // C++ lambdas form their own scope, including capture-only lambdas.
        if(type == FileType::Cpp && tokens[i].text == "[" && tokens[i].mate > i)
        {
            int after = tokens[i].mate + 1;
            if(after < size && tokens[after].text == "(" &&
               tokens[after].mate > after)
                after = tokens[after].mate + 1;
            while(after < size && (tokens[after].text == "mutable" ||
                                   tokens[after].text == "noexcept"))
                ++after;
            if(after < size && tokens[after].text == "{")
            {
                name = i;
                body = after;
                exactStart = true;
            }
        }
        // Named block-bodied arrow functions: const greet = (name) => { ... }.
        if(tokens[i].text == "=>" && i > 1 && i + 1 < size &&
           tokens[i + 1].text == "{")
        {
            int before = i - 1;
            if(tokens[before].text == ")" && tokens[before].mate >= 0)
                before = tokens[before].mate;
            if(before >= 2 && tokens[before - 1].text == "=" &&
               tokens[before - 2].identifier)
            {
                name = before - 2;
                body = i + 1;
                exactStart = true;
            }
            else
            {
                name = before;
                body = i + 1;
                exactStart = true;
            }
        }
        if(body >= 0 && claimedBodies.insert(body).second)
        {
            const int end =
                tokens[body].mate < 0 ? size : tokens[body].mate + 1;
            result.push_back({name, body + 1, end, exactStart});
        }
    }
    std::sort(
        result.begin(),
        result.end(),
        [](const Function& a, const Function& b) { return a.name < b.name; });
    return result;
}

bool localDeclaration(const std::vector<Token>& tokens, int i, FileType type)
{
    const auto& token = tokens[i];
    if(!token.identifier || keyword(token.text) ||
       i + 1 >= static_cast<int>(tokens.size()))
        return false;
    const auto& next = tokens[i + 1].text;
    const std::string_view previous =
        i ? tokens[i - 1].text : std::string_view{};
    if(previous == "." || previous == "->" || previous == "::")
        return false;
    if(type == FileType::Python)
    {
        // Bare assignment/annotation targets, not object attributes or indices.
        if(next == ":=" || previous == "for" || previous == "as")
            return true;
        if(token.parent >= 0)
            return false; // keyword arguments and dictionary keys aren't locals
        if(next == "=" ||
           (next == ":" &&
            (i == 0 || tokens[i - 1].position.line < token.position.line ||
             previous == ";" || previous == ":")))
            return true;
        if(next == ",")
        {
            int after = i + 1;
            while(after < static_cast<int>(tokens.size()) &&
                  tokens[after].position.line == token.position.line &&
                  (tokens[after].text == "," || tokens[after].identifier))
                ++after;
            return after < static_cast<int>(tokens.size()) &&
                   tokens[after].text == "=";
        }
        return false;
    }
    if(previous == "let" || previous == "var" ||
       (previous == "const" &&
        (type == FileType::JavaScript || type == FileType::TypeScript ||
         next == "=" || next == ":")))
        return true;
    if(type == FileType::JavaScript || type == FileType::TypeScript)
        return false;
    if(next != "=" && next != ";" && next != "," && next != "[" &&
       next != ":" && next != "{")
        return false;
    if(i > 0 && tokens[i - 1].identifier && !keyword(previous))
        return true;
    if(previous == ">" || previous == "]" || previous == "?" ||
       previous == "*" || previous == "&")
    {
        // Require a type before pointer/reference/nullable/array modifiers.
        int before = i - 2;
        while(before >= 0 &&
              (tokens[before].text == "*" || tokens[before].text == "&"))
            --before;
        return before >= 0 && (tokens[before].identifier || previous == ">" ||
                               previous == "]");
    }
    return false;
}
} // namespace

bool supports(FileType type)
{
    return type == FileType::Cpp || type == FileType::Python ||
           type == FileType::Mla || type == FileType::JavaScript ||
           type == FileType::TypeScript || type == FileType::CSharp;
}

std::vector<FunctionSymbol> functionSymbols(
    const std::vector<std::string>& lines,
    FileType type)
{
    if(!supports(type))
        return {};
    const auto tokens = tokenize(lines, type);
    const auto outline = functions(tokens, lines, type);
    std::vector<FunctionSymbol> symbols;
    for(const auto& function : outline)
    {
        int begin = function.name;
        if(type == FileType::Python)
        {
            --begin; // def
            if(begin > 0 && tokens[begin - 1].text == "async")
                --begin;
        }
        else if(!function.exactStart || tokens[begin].identifier)
        {
            while(begin > 0)
            {
                const auto& previous = tokens[begin - 1].text;
                if(previous == ";" || previous == "{" || previous == "}")
                    break;
                if(previous == ":" && begin > 1 &&
                   (tokens[begin - 2].text == "public" ||
                    tokens[begin - 2].text == "private" ||
                    tokens[begin - 2].text == "protected"))
                    break;
                --begin;
            }
        }
        int end = function.body - 1; // Exclude the opening body brace.
        if(type == FileType::Python)
            ++end; // Keep Python's signature colon.
        std::string signature;
        for(int i = begin; i < end; ++i)
        {
            const auto& token = tokens[i];
            // Constructor initializers are not part of the signature.
            if((type == FileType::Cpp || type == FileType::CSharp) &&
               token.text == ":")
                break;
            const std::string& spelling =
                token.spelling.empty() ? token.text : token.spelling;
            if(i > begin)
            {
                const auto& previous = tokens[i - 1];
                const auto& previousSpelling = previous.spelling.empty()
                                                   ? previous.text
                                                   : previous.spelling;
                const bool gap =
                    token.position.line != previous.position.line ||
                    token.position.column >
                        previous.position.column +
                            static_cast<int>(previousSpelling.size());
                if(gap && previous.text != "(" && previous.text != "[" &&
                   token.text != ")" && token.text != "]")
                    signature += ' ';
            }
            for(unsigned char c : spelling)
            {
                if(c < 32 || c == 127)
                {
                    if(signature.empty() || signature.back() != ' ')
                        signature += ' ';
                }
                else
                    signature += static_cast<char>(c);
            }
        }
        while(!signature.empty() && signature.back() == ' ')
            signature.pop_back();
        symbols.push_back(
            {tokens[function.name].position, std::move(signature)});
    }
    return symbols;
}

std::vector<Position> targets(
    const std::vector<std::string>& lines,
    FileType type,
    Target target,
    Position cursor)
{
    if(!supports(type))
        return {};
    const auto tokens = tokenize(lines, type);
    const auto outline = functions(tokens, lines, type);
    std::vector<Position> result;
    if(target == Target::Function)
    {
        for(const auto& function : outline)
            result.push_back(tokens[function.name].position);
    }
    else
    {
        const Function* scope = nullptr;
        for(const auto& function : outline)
        {
            Position start = tokens[function.name].position;
            if(!function.exactStart)
                start.column = 0;
            const Position end =
                function.end < static_cast<int>(tokens.size())
                    ? tokens[function.end].position
                    : Position{static_cast<int>(lines.size()), 0};
            if(!(cursor < start) && cursor < end &&
               (!scope || function.body > scope->body))
                scope = &function;
        }
        if(!scope)
            return {};
        for(int i = scope->body; i < scope->end; ++i)
        {
            bool nested = false;
            for(const auto& function : outline)
            {
                if(function.body > scope->body && i >= function.name &&
                   i < function.end)
                {
                    nested = true;
                    break;
                }
            }
            if(nested)
                continue;
            const auto& token = tokens[i];
            bool call = token.identifier && !keyword(token.text) &&
                        i + 1 < scope->end && tokens[i + 1].text == "(";
            if(token.identifier && !keyword(token.text) && i + 1 < scope->end &&
               tokens[i + 1].text == "<")
            {
                int depth = 0;
                for(int j = i + 1; j < scope->end; ++j)
                {
                    if(tokens[j].text == "<")
                        ++depth;
                    else if(tokens[j].text == ">" && --depth == 0)
                    {
                        call = j + 1 < scope->end && tokens[j + 1].text == "(";
                        break;
                    }
                    else if(tokens[j].text == ";" || tokens[j].text == "{")
                        break;
                }
            }
            if(call || localDeclaration(tokens, i, type))
                result.push_back(token.position);
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
} // namespace structural_navigation
