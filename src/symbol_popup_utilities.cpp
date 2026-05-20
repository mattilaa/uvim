#include "symbol_popup_utilities.h"
#include "cpp_navigation_utilities.h"
#include "editor_utils.h"
#include "text_utils.h"

#include <algorithm>
#include <cctype>
#include <fstream>

using editor::helper::trim_ascii_ws;
using editor::helper::trim_view;

std::string SymbolPopupUtilities::collectSignatureLine(
    const std::vector<std::string>& lines, int startY, int maxLines)
{
    if(startY < 0 || startY >= (int)lines.size())
        return "";
    std::string out = trim_ascii_ws(lines[startY]);
    if(!text_utils::contains(out, '('))
        return out;
    if(text_utils::contains(out, ')') || text_utils::contains(out, '{'))
        return out;

    for(int i = 1; i <= maxLines && startY + i < (int)lines.size(); ++i)
    {
        std::string chunk = trim_ascii_ws(lines[startY + i]);
        if(chunk.empty())
            continue;
        out += " " + chunk;
        if(text_utils::contains(chunk, ')') ||
           text_utils::contains(chunk, '{') || text_utils::contains(chunk, ';'))
        {
            break;
        }
    }
    return out;
}

std::string
SymbolPopupUtilities::extractInitializerTypeCandidate(std::string_view rhs)
{
    rhs = trim_view(rhs);
    if(rhs.empty())
        return "";
    if(!rhs.empty() && rhs.back() == ';')
        rhs.remove_suffix(1);
    rhs = trim_view(rhs);
    if(rhs.empty())
        return "";

    int depth = 0;
    std::string token;
    token.reserve(rhs.size());
    for(size_t i = 0; i < rhs.size(); ++i)
    {
        char c = rhs[i];
        if(c == '<')
            depth++;
        else if(c == '>')
            depth = std::max(0, depth - 1);
        if(depth == 0 && (c == '(' || c == '{' || c == ';'))
            break;
        token.push_back(c);
    }
    return trim_ascii_ws(token);
}

bool SymbolPopupUtilities::isControlStatement(std::string_view line)
{
    line = trim_view(line);
    auto starts = [&](std::string_view kw)
    {
        if(!line.starts_with(kw))
            return false;
        if(line.size() == kw.size())
            return true;
        char next = line[kw.size()];
        return text_utils::is_space(next) || next == '(';
    };
    return starts("if") || starts("for") || starts("while") ||
           starts("switch") || starts("return") || starts("throw") ||
           starts("catch") || starts("else");
}

bool SymbolPopupUtilities::findDeclarationInLines(
    const std::vector<std::string>& lines, const std::string& symbol, int& outY,
    int& outX)
{
    if(symbol.empty())
        return false;
    for(int y = 0; y < (int)lines.size(); ++y)
    {
        const std::string& line = lines[y];
        if(!text_utils::contains(line, symbol))
            continue;
        if(isControlStatement(line))
            continue;

        auto matches = text_utils::find_cursor(line, symbol);
        size_t pos = 0;
        while(matches.next(pos))
        {
            bool leftOk = true;
            if(pos > 0)
            {
                char prev = line[pos - 1];
                if(CppNavigationUtilities::isIdent(prev) || prev == '.' ||
                   prev == '>' || prev == '*')
                {
                    leftOk = false;
                }
                else if(prev == ':' && (pos < 2 || line[pos - 2] != ':'))
                {
                    leftOk = false;
                }
            }
            if(!leftOk)
            {
                continue;
            }
            size_t after = pos + symbol.size();
            if(after < line.size() &&
               CppNavigationUtilities::isIdent(line[after]))
            {
                continue;
            }
            while(after < line.size() &&
                  std::isspace((unsigned char)line[after]))
            {
                ++after;
            }
            if(after >= line.size() || line[after] != '(')
            {
                continue;
            }
            outY = y;
            outX = (int)pos;
            return true;
        }
    }
    return false;
}

bool SymbolPopupUtilities::loadFileLines(const std::string& path,
                                         std::vector<std::string>& out)
{
    std::ifstream in(path);
    if(!in.is_open())
        return false;
    out.clear();
    std::string line;
    while(std::getline(in, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        out.push_back(line);
    }
    if(out.empty())
        out.push_back("");
    return true;
}

std::string SymbolPopupUtilities::lastQualifier(std::string_view text)
{
    std::string s(text);
    while(s.size() >= 2 && s.substr(s.size() - 2) == "::")
        s.resize(s.size() - 2);
    size_t pos = s.rfind("::");
    if(text_utils::is_found(pos))
        s = s.substr(pos + 2);
    return s;
}

std::string SymbolPopupUtilities::extractTypeBeforeName(const std::string& line,
                                                        const std::string& name)
{
    if(name.empty())
        return "";
    size_t pos = line.find(name);
    while(text_utils::is_found(pos))
    {
        bool leftOk =
            (pos == 0) || !CppNavigationUtilities::isIdent(line[pos - 1]);
        size_t end = pos + name.size();
        bool rightOk =
            (end >= line.size()) || !CppNavigationUtilities::isIdent(line[end]);
        if(leftOk && rightOk)
            break;
        pos = line.find(name, pos + name.size());
    }
    if(text_utils::is_not_found(pos))
        return "";

    int i = (int)pos - 1;
    auto is_skip = [](char c)
    { return c == ' ' || c == '\t' || c == '*' || c == '&'; };
    while(i >= 0 && is_skip(line[i]))
        --i;

    auto skip_template = [&](int& idx)
    {
        if(idx < 0 || line[idx] != '>')
            return;
        int depth = 0;
        while(idx >= 0)
        {
            char c = line[idx];
            if(c == '>')
                depth++;
            else if(c == '<')
            {
                depth--;
                if(depth == 0)
                {
                    --idx;
                    return;
                }
            }
            --idx;
        }
    };

    std::string qualifiers[] = {"const",    "volatile",  "mutable",
                                "static",   "constexpr", "inline",
                                "typename", "class",     "struct"};

    while(i >= 0)
    {
        while(i >= 0 && is_skip(line[i]))
            --i;
        skip_template(i);
        while(i >= 0 && is_skip(line[i]))
            --i;
        if(i < 0)
            break;

        int end = i;
        while(i >= 0 &&
              (CppNavigationUtilities::isIdent(line[i]) || line[i] == ':'))
            --i;
        if(end < 0 || end < i + 1)
            break;
        std::string token = line.substr((size_t)i + 1, (size_t)(end - i));
        bool isQualifier = false;
        for(const auto& q : qualifiers)
        {
            if(token == q)
            {
                isQualifier = true;
                break;
            }
        }
        if(isQualifier)
            continue;
        return token;
    }
    return "";
}
