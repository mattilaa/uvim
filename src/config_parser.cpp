#include "config_parser.h"

#include "text_utils.h"

#include <cctype>
#include <sstream>

namespace editor::config
{
namespace
{
void trim(std::string& s)
{
    size_t start = 0;
    while(start < s.size() && std::isspace((unsigned char)s[start]))
        ++start;
    size_t end = s.size();
    while(end > start && std::isspace((unsigned char)s[end - 1]))
        --end;
    s = s.substr(start, end - start);
}

std::string strip_comment(std::string value)
{
    bool inString = false;
    char quote = '\0';
    bool escaped = false;
    for(size_t i = 0; i < value.size(); ++i)
    {
        char c = value[i];
        if(inString)
        {
            if(quote == '"' && !escaped && c == '\\')
            {
                escaped = true;
                continue;
            }
            if(!escaped && c == quote)
            {
                inString = false;
                quote = '\0';
            }
            escaped = false;
            continue;
        }
        if(c == '"' || c == '\'')
        {
            inString = true;
            quote = c;
            escaped = false;
            continue;
        }
        if(c == '#')
            return value.substr(0, i);
    }
    return value;
}

std::string unquote(std::string value)
{
    if(value.size() < 2)
        return value;
    const char quote = value.front();
    if((quote != '"' && quote != '\'') || value.back() != quote)
        return value;
    value = value.substr(1, value.size() - 2);
    if(quote != '"')
        return value;
    std::string out;
    out.reserve(value.size());
    for(size_t i = 0; i < value.size(); ++i)
    {
        if(value[i] != '\\' || i + 1 >= value.size())
        {
            out.push_back(value[i]);
            continue;
        }
        char next = value[++i];
        switch(next)
        {
        case 'n':
            out.push_back('\n');
            break;
        case 'r':
            out.push_back('\r');
            break;
        case 't':
            out.push_back('\t');
            break;
        case '"':
        case '\\':
            out.push_back(next);
            break;
        default:
            out.push_back(next);
            break;
        }
    }
    return out;
}
} // namespace

std::unordered_map<std::string, std::string>
parseTomlMap(const std::string& input)
{
    std::unordered_map<std::string, std::string> out;
    std::string section;

    std::istringstream stream(input);
    std::string line;
    while(std::getline(stream, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();

        line = strip_comment(line);
        trim(line);
        if(line.empty())
            continue;

        if(line.front() == '[' && line.back() == ']')
        {
            section = line.substr(1, line.size() - 2);
            trim(section);
            continue;
        }

        size_t equals = line.find('=');
        if(text_utils::is_not_found(equals))
            continue;

        std::string key = line.substr(0, equals);
        std::string value = line.substr(equals + 1);
        trim(key);
        trim(value);
        value = unquote(value);

        std::string full = section;
        if(!full.empty() && !key.empty())
            full += '.';
        full += key;

        if(!full.empty())
            out[full] = value;
    }

    return out;
}

} // namespace editor::config
