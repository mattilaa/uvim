#include "editor.h"
#include "terminal.h"
#include <cctype>
#include <cstring>
#include <filesystem>
#include <unordered_set>

#include "text_utils.h"
#include "constants.h"

bool Editor::isCppFile() const
{
    if(!filename || filename->empty())
        return false;

    // Work with views to avoid copies
    std::string_view pathSv{*filename};

    // Use filesystem for extension logic
    std::filesystem::path p{*filename};

    // If there is no extension, keep your "stdlib header path" heuristics
    if(!p.has_extension())
    {
        return text_utils::contains(pathSv, "/c++/") ||
               text_utils::contains(pathSv, "/bits/") ||
               text_utils::contains(pathSv, "/ext/") ||
               text_utils::contains(pathSv, "/__");
    }

    // Compare extensions (including the dot) without allocating a new string
    // Note: path.extension() returns a path; .string() allocates. Use native
    // string view if possible: We'll just take a small allocation-free route by
    // comparing on the filename view.
    auto dot = pathSv.find_last_of('.');
    if(dot == std::string_view::npos)
        return false;
    std::string_view ext = pathSv.substr(dot); // includes '.'

    return std::any_of(constants::CPP_FILE_EXTENSIONS.begin(),
                       constants::CPP_FILE_EXTENSIONS.end(),
                       [&](std::string_view e)
                       { return text_utils::iequals_ascii(ext, e); });
}

bool Editor::isMlaFile() const
{
    if(filename->empty())
        return false;

    size_t dotPos = filename->find_last_of('.');
    if(dotPos == std::string::npos)
        return false;

    std::string ext = filename->substr(dotPos);
    return (ext == ".mla");
}

std::string Editor::getColorCode(TokenType type) const
{
    switch(type)
    {
    case TOKEN_KEYWORD:
        return Terminal::FG_MAGENTA;
    case TOKEN_TYPE:
        return Terminal::FG_CYAN;
    case TOKEN_STRING:
        return Terminal::FG_GREEN;
    case TOKEN_CHAR:
        return Terminal::FG_GREEN;
    case TOKEN_COMMENT:
        return Terminal::FG_BRIGHT_BLACK;
    case TOKEN_PREPROCESSOR:
        return Terminal::FG_YELLOW;
    case TOKEN_NUMBER:
        return Terminal::FG_RED;
    case TOKEN_OPERATOR:
        return Terminal::FG_BRIGHT_YELLOW;
    case TOKEN_FUNCTION:
        return Terminal::FG_BRIGHT_BLUE;
    default:
        return Terminal::FG_DEFAULT;
    }
}

std::vector<Token> Editor::tokenizeLine(const std::string& line,
                                        bool& inBlockComment)
{
    std::vector<Token> tokens;

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
                                                             "reflexpr",
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
                                                             "synchronized",
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
                                                             "final",
                                                             "fn",
                                                             "pub",
                                                             "impl",
                                                             "let",
                                                             "var",
                                                             "mod",
                                                             "use",
                                                             "in"};

    static const std::unordered_set<std::string> types = {
        "bool",
        "char",
        "char8_t",
        "char16_t",
        "char32_t",
        "double",
        "float",
        "int",
        "long",
        "short",
        "signed",
        "unsigned",
        "void",
        "wchar_t",
        "size_t",
        "ptrdiff_t",
        "nullptr_t",
        "int8_t",
        "int16_t",
        "int32_t",
        "int64_t",
        "uint8_t",
        "uint16_t",
        "uint32_t",
        "uint64_t",
        "intptr_t",
        "uintptr_t",
        "intmax_t",
        "uintmax_t",
        "std::vector",
        "std::list",
        "std::deque",
        "std::array",
        "std::forward_list",
        "std::map",
        "std::set",
        "std::multimap",
        "std::multiset",
        "std::unordered_map",
        "std::unordered_set",
        "std::unordered_multimap",
        "std::unordered_multiset",
        "std::stack",
        "std::queue",
        "std::priority_queue",
        "std::pair",
        "std::tuple",
        "std::string",
        "std::wstring",
        "std::u8string",
        "std::u16string",
        "std::u32string",
        "std::string_view",
        "std::wstring_view",
        "std::u8string_view",
        "std::u16string_view",
        "std::u32string_view",
        "std::unique_ptr",
        "std::shared_ptr",
        "std::weak_ptr",
        "std::auto_ptr",
        "std::function",
        "std::bind",
        "std::reference_wrapper",
        "std::optional",
        "std::variant",
        "std::any",
        "std::expected",
        "std::bitset",
        "std::complex",
        "std::iostream",
        "std::istream",
        "std::ostream",
        "std::stringstream",
        "std::istringstream",
        "std::ostringstream",
        "std::fstream",
        "std::ifstream",
        "std::ofstream",
        "std::cout",
        "std::cin",
        "std::cerr",
        "std::clog",
        "std::iterator",
        "std::reverse_iterator",
        "std::move_iterator",
        "std::back_insert_iterator",
        "std::front_insert_iterator",
        "std::insert_iterator",
        "std::thread",
        "std::mutex",
        "std::recursive_mutex",
        "std::timed_mutex",
        "std::lock_guard",
        "std::unique_lock",
        "std::shared_lock",
        "std::condition_variable",
        "std::condition_variable_any",
        "std::future",
        "std::promise",
        "std::packaged_task",
        "std::async",
        "std::atomic",
        "std::atomic_bool",
        "std::atomic_int",
        "std::chrono::duration",
        "std::chrono::time_point",
        "std::chrono::system_clock",
        "std::chrono::steady_clock",
        "std::chrono::high_resolution_clock",
        "std::chrono::seconds",
        "std::chrono::milliseconds",
        "std::chrono::microseconds",
        "std::chrono::nanoseconds",
        "std::mt19937",
        "std::mt19937_64",
        "std::random_device",
        "std::uniform_int_distribution",
        "std::uniform_real_distribution",
        "std::normal_distribution",
        "std::bernoulli_distribution",
        "std::discrete_distribution",
        "std::poisson_distribution",
        "std::exception",
        "std::runtime_error",
        "std::logic_error",
        "std::invalid_argument",
        "std::out_of_range",
        "std::overflow_error",
        "std::is_same",
        "std::is_integral",
        "std::is_floating_point",
        "std::is_pointer",
        "std::is_reference",
        "std::is_const",
        "std::enable_if",
        "std::conditional",
        "std::decay",
        "std::remove_reference",
        "std::remove_const",
        "std::remove_pointer",
        "std::less",
        "std::greater",
        "std::equal_to",
        "std::not_equal_to",
        "std::plus",
        "std::minus",
        "std::multiplies",
        "std::divides",
        "std::numeric_limits",
        "std::accumulate",
        "std::partial_sum",
        "std::initializer_list",
        "std::type_info",
        "std::bad_alloc",
        "std::nothrow_t",
        "std::align_val_t",
        "std::byte",
        "i8",
        "i16",
        "i32",
        "i64",
        "u8",
        "u16",
        "u32",
        "u64",
        "print",
        "println",
        "eprint",
        "eprintln",
        "string",
        "list",
        "map",
        "tuple"};

    int i = 0;
    int len = line.length();

    while(i < len)
    {
        while(i < len && std::isspace(line[i]))
            i++;

        if(i >= len)
            break;

        if(inBlockComment)
        {
            int start = i;
            while(i < len &&
                  !(i < len - 1 && line[i] == '*' && line[i + 1] == '/'))
                i++;

            if(i < len - 1 && line[i] == '*' && line[i + 1] == '/')
            {
                i += 2;
                inBlockComment = false;
            }

            tokens.push_back({TOKEN_COMMENT, start, i - start});
            continue;
        }

        if(i == 0 && line[i] == '#')
        {
            tokens.push_back({TOKEN_PREPROCESSOR, i, len - i});
            break;
        }

        if(i < len - 1 && line[i] == '/' && line[i + 1] == '/')
        {
            tokens.push_back({TOKEN_COMMENT, i, len - i});
            break;
        }

        if(i < len - 1 && line[i] == '/' && line[i + 1] == '*')
        {
            int start = i;
            i += 2;
            while(i < len - 1 && !(line[i] == '*' && line[i + 1] == '/'))
                i++;

            if(i < len - 1 && line[i] == '*' && line[i + 1] == '/')
            {
                i += 2;
            }
            else
            {
                inBlockComment = true;
                i = len;
            }

            tokens.push_back({TOKEN_COMMENT, start, i - start});
            continue;
        }

        if(line[i] == '"')
        {
            int start = i;
            i++;
            while(i < len && line[i] != '"')
            {
                if(line[i] == '\\' && i + 1 < len)
                    i += 2;
                else
                    i++;
            }
            if(i < len)
                i++;
            tokens.push_back({TOKEN_STRING, start, i - start});
            continue;
        }

        if(line[i] == '\'')
        {
            int start = i;
            i++;
            while(i < len && line[i] != '\'')
            {
                if(line[i] == '\\' && i + 1 < len)
                    i += 2;
                else
                    i++;
            }
            if(i < len)
                i++;
            tokens.push_back({TOKEN_CHAR, start, i - start});
            continue;
        }

        if(std::isdigit(line[i]) ||
           (line[i] == '.' && i + 1 < len && std::isdigit(line[i + 1])))
        {
            int start = i;
            bool hasHex = false;

            if(line[i] == '0' && i + 1 < len &&
               (line[i + 1] == 'x' || line[i + 1] == 'X'))
            {
                hasHex = true;
                i += 2;
            }

            while(i < len &&
                  (std::isdigit(line[i]) ||
                   (hasHex && std::isxdigit(line[i])) || line[i] == '.' ||
                   line[i] == 'e' || line[i] == 'E' || line[i] == 'f' ||
                   line[i] == 'F' || line[i] == 'u' || line[i] == 'U' ||
                   line[i] == 'l' || line[i] == 'L'))
            {
                i++;
            }

            tokens.push_back({TOKEN_NUMBER, start, i - start});
            continue;
        }

        if(std::isalpha(line[i]) || line[i] == '_')
        {
            int start = i;
            while(i < len &&
                  (std::isalnum(line[i]) || line[i] == '_' || line[i] == ':'))
                i++;

            std::string word = line.substr(start, i - start);

            int j = i;
            while(j < len && std::isspace(line[j]))
                j++;

            TokenType type = TOKEN_NORMAL;
            if(j < len && line[j] == '(')
            {
                type = TOKEN_FUNCTION;
            }
            else if(keywords.find(word) != keywords.end())
            {
                type = TOKEN_KEYWORD;
            }
            else if(types.find(word) != types.end())
            {
                type = TOKEN_TYPE;
            }
            else
            {
                if(!tokens.empty())
                {
                    const Token& prevToken = tokens.back();
                    std::string prevWord =
                        line.substr(prevToken.start, prevToken.length);
                    if(prevWord == "struct" || prevWord == "class" ||
                       prevWord == "enum" || prevWord == ":" ||
                       prevWord == "->")
                    {
                        type = TOKEN_TYPE;
                    }
                }
            }

            tokens.push_back({type, start, i - start});
            continue;
        }

        if(std::strchr("+-*/%=<>!&|^~?:.,;()[]{}\\", line[i]))
        {
            int start = i;
            i++;

            if(i < len && std::strchr("=<>+-&|*/%:.", line[i - 1]))
            {
                if((line[i - 1] == '+' && line[i] == '+') ||
                   (line[i - 1] == '-' && line[i] == '-') ||
                   (line[i - 1] == '&' && line[i] == '&') ||
                   (line[i - 1] == '|' && line[i] == '|') ||
                   (line[i - 1] == '=' && line[i] == '=') ||
                   (line[i - 1] == '!' && line[i] == '=') ||
                   (line[i - 1] == '<' && line[i] == '=') ||
                   (line[i - 1] == '>' && line[i] == '=') ||
                   (line[i - 1] == '<' && line[i] == '<') ||
                   (line[i - 1] == '>' && line[i] == '>') ||
                   (line[i - 1] == '-' && line[i] == '>') ||
                   (line[i - 1] == ':' && line[i] == ':') ||
                   (line[i - 1] == '.' && line[i] == '.') ||
                   (line[i - 1] == '+' && line[i] == '=') ||
                   (line[i - 1] == '-' && line[i] == '=') ||
                   (line[i - 1] == '*' && line[i] == '=') ||
                   (line[i - 1] == '/' && line[i] == '=') ||
                   (line[i - 1] == '%' && line[i] == '='))
                {
                    i++;
                }
            }

            tokens.push_back({TOKEN_OPERATOR, start, i - start});
            continue;
        }

        tokens.push_back({TOKEN_NORMAL, i, 1});
        i++;
    }

    return tokens;
}

void Editor::renderLineWithSyntax(std::string& output, const std::string& line,
                                  int start, int len, int fileRow)
{
    static bool inBlockComment = false;

    if(fileRow == 0)
        inBlockComment = false;

    bool blockCommentState = inBlockComment;
    std::vector<Token> tokens = tokenizeLine(line, blockCommentState);

    std::vector<TokenType> charColors(len, TOKEN_NORMAL);

    for(const auto& token : tokens)
    {
        int tokenEnd = token.start + token.length;
        for(int pos = token.start; pos < tokenEnd; pos++)
        {
            int visiblePos = pos - start;
            if(visiblePos >= 0 && visiblePos < len)
            {
                charColors[visiblePos] = token.type;
            }
        }
    }

    TokenType currentColor = TOKEN_NORMAL;
    for(int x = 0; x < len; x++)
    {
        int col = x + start;

        bool highlighted = false;
        if(isInSelection(fileRow, col) || isInVisualBlock(fileRow, col))
        {
            output += Terminal::STYLE_SELECTION;
            highlighted = true;
        }
        else if(isInSearchMatch(fileRow, col))
        {
            output += Terminal::STYLE_SEARCH_MATCH;
            highlighted = true;
        }

        if(!highlighted && charColors[x] != currentColor)
        {
            currentColor = charColors[x];
            output += getColorCode(currentColor);
        }

        output += line[col];

        if(highlighted)
        {
            output += Terminal::ESC_RESET_ALL;
            currentColor = TOKEN_NORMAL;
        }
    }

    if(currentColor != TOKEN_NORMAL)
    {
        output += Terminal::FG_DEFAULT;
    }

    inBlockComment = blockCommentState;
}
