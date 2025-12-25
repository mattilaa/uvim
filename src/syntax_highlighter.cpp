#include "syntax_highlighter.h"
#include "terminal.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>

SyntaxHighlighter::SyntaxHighlighter(EditorContext& ctx) : ctx(ctx) {}

bool SyntaxHighlighter::isCppFile() const
{
    if(ctx.filename->empty())
        return false;

    size_t dotPos = ctx.filename->find_last_of('.');
    if(dotPos == std::string::npos)
    {
        const std::string& path = *ctx.filename;
        if(path.find("/c++/") != std::string::npos ||
           path.find("/bits/") != std::string::npos ||
           path.find("/ext/") != std::string::npos ||
           path.find("/__") != std::string::npos)
        {
            return true;
        }
        return false;
    }

    std::string ext = ctx.filename->substr(dotPos);
    return (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".h" ||
            ext == ".hpp" || ext == ".hxx" || ext == ".c" || ext == ".C");
}

bool SyntaxHighlighter::isMlaFile() const
{
    if(ctx.filename->empty())
        return false;

    size_t dotPos = ctx.filename->find_last_of('.');
    if(dotPos == std::string::npos)
        return false;

    std::string ext = ctx.filename->substr(dotPos);
    return ext == ".mla";
}

bool SyntaxHighlighter::isKeyword(const std::string& word) const
{
    static const std::unordered_set<std::string> keywords = {"alignas",
                                                             "alignof",
                                                             "and",
                                                             "and_eq",
                                                             "asm",
                                                             "auto",
                                                             "bitand",
                                                             "bitor",
                                                             "break",
                                                             "case",
                                                             "catch",
                                                             "class",
                                                             "compl",
                                                             "concept",
                                                             "const",
                                                             "consteval",
                                                             "constexpr",
                                                             "constinit",
                                                             "const_cast",
                                                             "continue",
                                                             "co_await",
                                                             "co_return",
                                                             "co_yield",
                                                             "decltype",
                                                             "default",
                                                             "delete",
                                                             "do",
                                                             "dynamic_cast",
                                                             "else",
                                                             "enum",
                                                             "explicit",
                                                             "export",
                                                             "extern",
                                                             "false",
                                                             "for",
                                                             "friend",
                                                             "goto",
                                                             "if",
                                                             "inline",
                                                             "mutable",
                                                             "namespace",
                                                             "new",
                                                             "noexcept",
                                                             "not",
                                                             "not_eq",
                                                             "nullptr",
                                                             "operator",
                                                             "or",
                                                             "or_eq",
                                                             "private",
                                                             "protected",
                                                             "public",
                                                             "register",
                                                             "reinterpret_cast",
                                                             "requires",
                                                             "return",
                                                             "sizeof",
                                                             "static",
                                                             "static_assert",
                                                             "static_cast",
                                                             "struct",
                                                             "switch",
                                                             "template",
                                                             "this",
                                                             "thread_local",
                                                             "throw",
                                                             "true",
                                                             "try",
                                                             "typedef",
                                                             "typeid",
                                                             "typename",
                                                             "union",
                                                             "using",
                                                             "virtual",
                                                             "volatile",
                                                             "while",
                                                             "xor",
                                                             "xor_eq",
                                                             "override",
                                                             "final"};
    return keywords.find(word) != keywords.end();
}

bool SyntaxHighlighter::isType(const std::string& word) const
{
    static const std::unordered_set<std::string> types = {
        "bool",      "char",     "char8_t",      "char16_t",   "char32_t",
        "double",    "float",    "int",          "long",       "short",
        "signed",    "unsigned", "void",         "wchar_t",    "int8_t",
        "int16_t",   "int32_t",  "int64_t",      "uint8_t",    "uint16_t",
        "uint32_t",  "uint64_t", "size_t",       "ptrdiff_t",  "intptr_t",
        "uintptr_t", "string",   "vector",       "map",        "set",
        "list",      "deque",    "array",        "pair",       "tuple",
        "optional",  "variant",  "unique_ptr",   "shared_ptr", "weak_ptr",
        "function",  "thread",   "mutex",        "atomic",     "istream",
        "ostream",   "fstream",  "stringstream", "FILE",       "nullptr_t"};
    return types.find(word) != types.end();
}

bool SyntaxHighlighter::isMlaKeyword(const std::string& word) const
{
    static const std::unordered_set<std::string> keywords = {
        "fn",    "let",    "mut",    "if",       "else",   "while", "for",
        "in",    "return", "break",  "continue", "struct", "enum",  "impl",
        "pub",   "mod",    "use",    "as",       "match",  "true",  "false",
        "self",  "Self",   "const",  "static",   "type",   "where", "loop",
        "ref",   "move",   "extern", "crate",    "super",  "trait", "dyn",
        "async", "await",  "unsafe"};
    return keywords.find(word) != keywords.end();
}

bool SyntaxHighlighter::isMlaType(const std::string& word) const
{
    static const std::unordered_set<std::string> types = {
        "i8",   "i16",  "i32", "i64",    "i128",    "isize",  "u8",
        "u16",  "u32",  "u64", "u128",   "usize",   "f32",    "f64",
        "bool", "char", "str", "String", "Vec",     "Option", "Result",
        "Box",  "Rc",   "Arc", "Cell",   "RefCell", "Mutex"};
    return types.find(word) != types.end();
}

std::string SyntaxHighlighter::getColorCode(TokenType type) const
{
    switch(type)
    {
    case TokenType::KEYWORD:
        return Terminal::FG_MAGENTA;
    case TokenType::TYPE:
        return Terminal::FG_CYAN;
    case TokenType::STRING:
        return Terminal::FG_GREEN;
    case TokenType::NUMBER:
        return Terminal::FG_YELLOW;
    case TokenType::COMMENT:
        return Terminal::FG_BRIGHT_BLACK;
    case TokenType::PREPROCESSOR:
        return Terminal::FG_BLUE;
    case TokenType::FUNCTION:
        return Terminal::FG_YELLOW;
    case TokenType::OPERATOR:
        return Terminal::FG_WHITE;
    case TokenType::BRACKET:
        return Terminal::FG_WHITE;
    case TokenType::IDENTIFIER:
    case TokenType::NORMAL:
    default:
        return Terminal::FG_WHITE;
    }
}

std::vector<Token>
SyntaxHighlighter::tokenizeLine(const std::string& line) const
{
    std::vector<Token> tokens;

    if(!isCppFile() && !isMlaFile())
    {
        // No syntax highlighting
        if(!line.empty())
        {
            tokens.push_back({TokenType::NORMAL, 0, (int)line.length()});
        }
        return tokens;
    }

    bool isMla = isMlaFile();
    int i = 0;
    int len = line.length();

    while(i < len)
    {
        // Skip whitespace
        if(std::isspace(static_cast<unsigned char>(line[i])))
        {
            int start = i;
            while(i < len && std::isspace(static_cast<unsigned char>(line[i])))
            {
                i++;
            }
            tokens.push_back({TokenType::NORMAL, start, i - start});
            continue;
        }

        // Preprocessor (C/C++)
        if(!isMla && line[i] == '#')
        {
            tokens.push_back({TokenType::PREPROCESSOR, i, len - i});
            break;
        }

        // Single-line comment
        if(i + 1 < len && line[i] == '/' && line[i + 1] == '/')
        {
            tokens.push_back({TokenType::COMMENT, i, len - i});
            break;
        }

        // Multi-line comment start (simplified - doesn't track across lines)
        if(i + 1 < len && line[i] == '/' && line[i + 1] == '*')
        {
            int start = i;
            i += 2;
            while(i + 1 < len && !(line[i] == '*' && line[i + 1] == '/'))
            {
                i++;
            }
            if(i + 1 < len)
            {
                i += 2;
            }
            tokens.push_back({TokenType::COMMENT, start, i - start});
            continue;
        }

        // String literal
        if(line[i] == '"' || line[i] == '\'')
        {
            char quote = line[i];
            int start = i;
            i++;
            while(i < len && line[i] != quote)
            {
                if(line[i] == '\\' && i + 1 < len)
                {
                    i += 2;
                }
                else
                {
                    i++;
                }
            }
            if(i < len)
            {
                i++; // Include closing quote
            }
            tokens.push_back({TokenType::STRING, start, i - start});
            continue;
        }

        // Raw string literal (C++11)
        if(!isMla && i + 1 < len && line[i] == 'R' && line[i + 1] == '"')
        {
            int start = i;
            i += 2;
            // Find delimiter
            std::string delim;
            while(i < len && line[i] != '(')
            {
                delim += line[i];
                i++;
            }
            if(i < len)
                i++; // Skip '('

            // Find closing )delimiter"
            std::string closing = ")" + delim + "\"";
            size_t closePos = line.find(closing, i);
            if(closePos != std::string::npos)
            {
                i = closePos + closing.length();
            }
            else
            {
                i = len;
            }
            tokens.push_back({TokenType::STRING, start, i - start});
            continue;
        }

        // Number
        if(std::isdigit(static_cast<unsigned char>(line[i])) ||
           (line[i] == '.' && i + 1 < len &&
            std::isdigit(static_cast<unsigned char>(line[i + 1]))))
        {
            int start = i;

            // Hex
            if(line[i] == '0' && i + 1 < len &&
               (line[i + 1] == 'x' || line[i + 1] == 'X'))
            {
                i += 2;
                while(i < len &&
                      std::isxdigit(static_cast<unsigned char>(line[i])))
                {
                    i++;
                }
            }
            // Binary
            else if(line[i] == '0' && i + 1 < len &&
                    (line[i + 1] == 'b' || line[i + 1] == 'B'))
            {
                i += 2;
                while(i < len && (line[i] == '0' || line[i] == '1'))
                {
                    i++;
                }
            }
            // Decimal/float
            else
            {
                while(i < len &&
                      (std::isdigit(static_cast<unsigned char>(line[i])) ||
                       line[i] == '.' || line[i] == 'e' || line[i] == 'E' ||
                       line[i] == '+' || line[i] == '-' || line[i] == '\''))
                {
                    i++;
                }
            }

            // Suffix (u, l, f, etc.)
            while(i < len &&
                  (line[i] == 'u' || line[i] == 'U' || line[i] == 'l' ||
                   line[i] == 'L' || line[i] == 'f' || line[i] == 'F'))
            {
                i++;
            }

            tokens.push_back({TokenType::NUMBER, start, i - start});
            continue;
        }

        // Identifier or keyword
        if(std::isalpha(static_cast<unsigned char>(line[i])) || line[i] == '_')
        {
            int start = i;
            while(i < len &&
                  (std::isalnum(static_cast<unsigned char>(line[i])) ||
                   line[i] == '_'))
            {
                i++;
            }

            std::string word = line.substr(start, i - start);
            TokenType type = TokenType::IDENTIFIER;

            if(isMla)
            {
                if(isMlaKeyword(word))
                    type = TokenType::KEYWORD;
                else if(isMlaType(word))
                    type = TokenType::TYPE;
            }
            else
            {
                if(isKeyword(word))
                    type = TokenType::KEYWORD;
                else if(isType(word))
                    type = TokenType::TYPE;
            }

            // Check if it's a function call
            if(type == TokenType::IDENTIFIER)
            {
                int j = i;
                while(j < len &&
                      std::isspace(static_cast<unsigned char>(line[j])))
                {
                    j++;
                }
                if(j < len && line[j] == '(')
                {
                    type = TokenType::FUNCTION;
                }
            }

            tokens.push_back({type, start, i - start});
            continue;
        }

        // Operators and brackets
        if(std::strchr("+-*/%=<>!&|^~?:", line[i]))
        {
            int start = i;
            // Handle multi-char operators
            if(i + 1 < len)
            {
                std::string op2 = line.substr(i, 2);
                if(op2 == "==" || op2 == "!=" || op2 == "<=" || op2 == ">=" ||
                   op2 == "&&" || op2 == "||" || op2 == "++" || op2 == "--" ||
                   op2 == "+=" || op2 == "-=" || op2 == "*=" || op2 == "/=" ||
                   op2 == "%=" || op2 == "&=" || op2 == "|=" || op2 == "^=" ||
                   op2 == "<<" || op2 == ">>" || op2 == "->" || op2 == "::")
                {
                    i += 2;
                    // Check for <<= or >>=
                    if(i < len && line[i] == '=' &&
                       (op2 == "<<" || op2 == ">>"))
                    {
                        i++;
                    }
                    tokens.push_back({TokenType::OPERATOR, start, i - start});
                    continue;
                }
            }
            i++;
            tokens.push_back({TokenType::OPERATOR, start, 1});
            continue;
        }

        // Brackets
        if(std::strchr("()[]{}", line[i]))
        {
            tokens.push_back({TokenType::BRACKET, i, 1});
            i++;
            continue;
        }

        // Other characters
        tokens.push_back({TokenType::NORMAL, i, 1});
        i++;
    }

    return tokens;
}

void SyntaxHighlighter::renderLineWithSyntax(
    std::string& output, const std::string& line, int lineNum, int startCol,
    int maxCols, bool inSelection, int selStartCol, int selEndCol,
    bool inSearchMatch, int searchStartCol, int searchEndCol)
{
    std::vector<Token> tokens = tokenizeLine(line);

    int col = 0;
    int outputCols = 0;

    for(const auto& token : tokens)
    {
        std::string color = getColorCode(token.type);

        for(int j = 0; j < token.length && outputCols < maxCols; j++)
        {
            int charCol = token.start + j;

            if(charCol < startCol)
            {
                col++;
                continue;
            }

            char c = line[charCol];

            // Check for selection highlight
            bool isSelected =
                inSelection && charCol >= selStartCol && charCol <= selEndCol;

            // Check for search match highlight
            bool isSearchHit = inSearchMatch && charCol >= searchStartCol &&
                               charCol <= searchEndCol;

            if(isSelected)
            {
                output += Terminal::STYLE_SELECTION;
            }
            else if(isSearchHit)
            {
                output += Terminal::STYLE_SEARCH_MATCH;
            }
            else
            {
                output += color;
            }

            output += c;
            output += Terminal::ESC_RESET_ALL;

            outputCols++;
            col++;
        }
    }

    // Fill remaining space if line is shorter
    while(outputCols < maxCols)
    {
        bool isSelected = inSelection && col >= selStartCol && col <= selEndCol;
        if(isSelected)
        {
            output += Terminal::STYLE_SELECTION;
            output += ' ';
            output += Terminal::ESC_RESET_ALL;
        }
        else
        {
            output += ' ';
        }
        outputCols++;
        col++;
    }
}
