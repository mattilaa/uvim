#include "cursor_call.h"

#include <cctype>

std::optional<CursorCall> cursorCallAt(
    const std::vector<std::string>& lines,
    int row,
    int column,
    const std::string& callee)
{
    if(callee.empty() || row < 0 || row >= static_cast<int>(lines.size()) ||
       column < 0)
        return {};
    const auto& source = lines[row];
    const auto start = source.rfind(callee, static_cast<size_t>(column));
    if(start == std::string::npos ||
       start + callee.size() <= static_cast<size_t>(column))
        return {};
    bool blockComment = false, opened = false, escaped = false;
    char quote = 0;
    std::string rawEnd;
    int depth = 0;
    for(int y = row; y < static_cast<int>(lines.size()); ++y)
    {
        const auto& line = lines[y];
        for(size_t x = y == row ? start + callee.size() : 0; x < line.size();
            ++x)
        {
            const char ch = line[x];
            const char next = x + 1 < line.size() ? line[x + 1] : 0;
            if(!rawEnd.empty())
            {
                if(line.compare(x, rawEnd.size(), rawEnd) == 0)
                {
                    x += rawEnd.size() - 1;
                    rawEnd.clear();
                }
                continue;
            }
            if(blockComment)
            {
                if(ch == '*' && next == '/')
                {
                    blockComment = false;
                    ++x;
                }
                continue;
            }
            if(quote)
            {
                if(escaped)
                    escaped = false;
                else if(ch == '\\')
                    escaped = true;
                else if(ch == quote)
                    quote = 0;
                continue;
            }
            if(ch == '/' && next == '/')
                break;
            if(ch == '/' && next == '*')
            {
                blockComment = true;
                ++x;
                continue;
            }
            if(!opened)
            {
                if(std::isspace(static_cast<unsigned char>(ch)))
                    continue;
                if(ch != '(')
                    return {};
                opened = true;
            }
            if(ch == 'R' && next == '"')
            {
                const auto open = line.find('(', x + 2);
                if(open != std::string::npos && open - x <= 18)
                {
                    rawEnd = ")" + line.substr(x + 2, open - x - 2) + "\"";
                    x = open;
                    continue;
                }
            }
            if(ch == '\'' && x > 0 &&
               std::isalnum(static_cast<unsigned char>(next)))
            {
                size_t token = x;
                while(token > 0 &&
                      std::isalnum(static_cast<unsigned char>(line[token - 1])))
                    --token;
                if(token < x &&
                   std::isdigit(static_cast<unsigned char>(line[token])))
                    continue;
            }
            if(ch == '\'' || ch == '"')
                quote = ch;
            else if(ch == '(')
                ++depth;
            else if(ch == ')' && --depth == 0)
            {
                CursorCall result{
                    callee,
                    {},
                    row,
                    static_cast<int>(start),
                    y,
                    static_cast<int>(x + 1)};
                for(int part = row; part <= y; ++part)
                {
                    if(part != row)
                        result.expression += '\n';
                    const size_t begin = part == row ? start : 0;
                    const size_t end = part == y ? x + 1 : lines[part].size();
                    result.expression += lines[part].substr(begin, end - begin);
                }
                return result;
            }
        }
        if(quote && escaped)
            escaped = false;
    }
    return {};
}
