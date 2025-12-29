#include "editor.h"
#include "terminal.h"
#include <cctype>
#include <cstring>
#include <filesystem>
#include <unordered_set>

#include "text_utils.h"
#include "constants.h"
#include "cpp_constants.h"

template <class Arr>
inline bool contains_sorted(const Arr& arr, std::string_view s)
{
    return std::ranges::binary_search(arr, s);
}

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

std::vector<Token> Editor::tokenizeLine(const std::string& line, bool& inBlockComment)
{
    std::vector<Token> tokens;
    std::string_view sv{line};

    const int len = static_cast<int>(sv.size());
    int i = 0;

    while (i < len)
    {
        while (i < len && text_utils::is_space(sv[i])) ++i;
        if (i >= len) break;

        // In block comment: consume until "*/" or EOL
        if (inBlockComment)
        {
            const int start = i;
            while (i < len && !(i < len - 1 && sv[i] == '*' && sv[i + 1] == '/'))
                ++i;

            if (i < len - 1 && sv[i] == '*' && sv[i + 1] == '/')
            {
                i += 2;
                inBlockComment = false;
            }

            tokens.push_back({TOKEN_COMMENT, start, i - start});
            continue;
        }

        // Preprocessor line (only if first non-space is '#')
        if (i == 0 && sv[i] == '#')
        {
            tokens.push_back({TOKEN_PREPROCESSOR, i, len - i});
            break;
        }

        // Line comment
        if (i < len - 1 && sv[i] == '/' && sv[i + 1] == '/')
        {
            tokens.push_back({TOKEN_COMMENT, i, len - i});
            break;
        }

        // Block comment start
        if (i < len - 1 && sv[i] == '/' && sv[i + 1] == '*')
        {
            const int start = i;
            i += 2;

            while (i < len - 1 && !(sv[i] == '*' && sv[i + 1] == '/'))
                ++i;

            if (i < len - 1 && sv[i] == '*' && sv[i + 1] == '/')
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

        // String literal
        if (sv[i] == '"')
        {
            const int start = i++;
            while (i < len && sv[i] != '"')
            {
                if (sv[i] == '\\' && i + 1 < len) i += 2;
                else ++i;
            }
            if (i < len) ++i;

            tokens.push_back({TOKEN_STRING, start, i - start});
            continue;
        }

        // Character literal
        if (sv[i] == '\'')
        {
            const int start = i++;
            while (i < len && sv[i] != '\'')
            {
                if (sv[i] == '\\' && i + 1 < len) i += 2;
                else ++i;
            }
            if (i < len) ++i;

            tokens.push_back({TOKEN_CHAR, start, i - start});
            continue;
        }

        // Number
        if (text_utils::is_digit(sv[i]) || (sv[i] == '.' && i + 1 < len && text_utils::is_digit(sv[i + 1])))
        {
            const int start = i;
            bool hasHex = false;

            if (sv[i] == '0' && i + 1 < len && (sv[i + 1] == 'x' || sv[i + 1] == 'X'))
            {
                hasHex = true;
                i += 2;
            }

            while (i < len &&
                   (text_utils::is_digit(sv[i]) ||
                    (hasHex && text_utils::is_xdigit(sv[i])) ||
                    sv[i] == '.' || sv[i] == 'e' || sv[i] == 'E' ||
                    sv[i] == 'f' || sv[i] == 'F' || sv[i] == 'u' || sv[i] == 'U' ||
                    sv[i] == 'l' || sv[i] == 'L'))
            {
                ++i;
            }

            tokens.push_back({TOKEN_NUMBER, start, i - start});
            continue;
        }

        // Identifier / keyword / type / function
        if (text_utils::is_alpha(sv[i]) || sv[i] == '_')
        {
            const int start = i;
            while (i < len && (text_utils::is_alnum(sv[i]) || sv[i] == '_' || sv[i] == ':'))
                ++i;

            const std::string_view word = sv.substr(start, i - start);

            int j = i;
            while (j < len && text_utils::is_space(sv[j])) ++j;

            TokenType type = TOKEN_NORMAL;

            if (j < len && sv[j] == '(')
            {
                type = TOKEN_FUNCTION;
            }
            else if (cpp_constants::is_keyword(word))
            {
                type = TOKEN_KEYWORD;
            }
            else if (cpp_constants::is_type(word))
            {
                type = TOKEN_TYPE;
            }
            else if (!tokens.empty())
            {
                const Token& prev = tokens.back();
                const std::string_view prevWord = sv.substr(prev.start, prev.length);

                if (prevWord == "struct" || prevWord == "class" || prevWord == "enum" ||
                    prevWord == ":" || prevWord == "->")
                {
                    type = TOKEN_TYPE;
                }
            }

            tokens.push_back({type, start, i - start});
            continue;
        }

        // Operators / punctuation
        if (cpp_constants::is_operator_char(sv[i]))
        {
            const int start = i++;
            if (i < len)
            {
                const char a = sv[i - 1], b = sv[i];
                const bool two =
                    (a == '+' && b == '+') || (a == '-' && b == '-') ||
                    (a == '&' && b == '&') || (a == '|' && b == '|') ||
                    (a == '=' && b == '=') || (a == '!' && b == '=') ||
                    (a == '<' && b == '=') || (a == '>' && b == '=') ||
                    (a == '<' && b == '<') || (a == '>' && b == '>') ||
                    (a == '-' && b == '>') || (a == ':' && b == ':') ||
                    (a == '.' && b == '.') ||
                    (a == '+' && b == '=') || (a == '-' && b == '=') ||
                    (a == '*' && b == '=') || (a == '/' && b == '=') ||
                    (a == '%' && b == '=');

                if (two) ++i;
            }

            tokens.push_back({TOKEN_OPERATOR, start, i - start});
            continue;
        }

        // Fallback: single char
        tokens.push_back({TOKEN_NORMAL, i, 1});
        ++i;
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
