#include "cpp_navigation_utilities.h"
#include "text_utils.h"

#include <cctype>
#include <filesystem>

bool CppNavigationUtilities::isIdent(char c)
{
    return std::isalnum((unsigned char)c) || c == '_';
}

int CppNavigationUtilities::findLocalDeclaration(const std::string& line,
                                                 const std::string& symbol)
{
    size_t firstNonSpace = line.find_first_not_of(" \t");
    if(text_utils::is_found(firstNonSpace) &&
       line.substr(firstNonSpace, 2) == "//")
        return -1;

    size_t commentPos = line.find("//");
    std::string effectiveLine =
        (text_utils::is_found(commentPos)) ? line.substr(0, commentPos) : line;

    auto matches = text_utils::find_cursor(effectiveLine, symbol);
    size_t pos = 0;
    while(matches.next(pos))
    {
        bool validStart = (pos == 0 || !isIdent(effectiveLine[pos - 1]));
        bool validEnd = (pos + symbol.length() >= effectiveLine.length() ||
                         !isIdent(effectiveLine[pos + symbol.length()]));

        if(!validStart || !validEnd)
        {
            matches.resume_at(pos + 1);
            continue;
        }

        size_t afterSymbol = pos + symbol.length();
        while(afterSymbol < effectiveLine.length() &&
              std::isspace((unsigned char)effectiveLine[afterSymbol]))
            afterSymbol++;

        int beforeSymbol = pos - 1;
        while(beforeSymbol >= 0 &&
              std::isspace((unsigned char)effectiveLine[beforeSymbol]))
            beforeSymbol--;

        if(afterSymbol < effectiveLine.length())
        {
            char nextChar = effectiveLine[afterSymbol];
            if(nextChar == '=' || nextChar == ';' || nextChar == ',' ||
               nextChar == ')' || nextChar == '[')
            {
                if(beforeSymbol >= 0)
                {
                    char prevChar = effectiveLine[beforeSymbol];
                    if(isIdent(prevChar) || prevChar == '>' ||
                       prevChar == '*' || prevChar == '&' || prevChar == ']')
                    {
                        return (int)pos;
                    }
                }
            }
        }
        else if(beforeSymbol >= 0)
        {
            char prevChar = effectiveLine[beforeSymbol];
            if(isIdent(prevChar) || prevChar == '>' || prevChar == '*' ||
               prevChar == '&' || prevChar == ']')
            {
                return (int)pos;
            }
        }

        pos++;
    }

    return -1;
}

bool CppNavigationUtilities::searchLocalDefinition(
    const std::vector<std::string>& lines, const std::string& symbol,
    int startY, int startX, int& outY, int& outX)
{
    int braceDepth = 0;
    bool foundOpenBrace = false;

    for(int y = startY; y >= 0; y--)
    {
        const std::string& line = lines[y];

        for(int i = (int)line.length() - 1; i >= 0; i--)
        {
            if(y == startY && i >= startX)
                continue;

            char c = line[i];
            if(c == '}')
            {
                braceDepth++;
            }
            else if(c == '{')
            {
                if(braceDepth > 0)
                    braceDepth--;
                else
                    foundOpenBrace = true;
            }
        }

        if(braceDepth == 0)
        {
            int col = findLocalDeclaration(line, symbol);
            if(col >= 0 && (y < startY || col < startX))
            {
                outY = y;
                outX = col;
                return true;
            }
        }

        if(foundOpenBrace && braceDepth == 0)
        {
            int col = findLocalDeclaration(line, symbol);
            if(col >= 0)
            {
                outY = y;
                outX = col;
                return true;
            }

            if(y > 0)
            {
                col = findLocalDeclaration(lines[y - 1], symbol);
                if(col >= 0)
                {
                    outY = y - 1;
                    outX = col;
                    return true;
                }
            }
            break;
        }
    }

    return false;
}

bool CppNavigationUtilities::searchMemberDefinition(
    const std::vector<std::string>& lines, const std::string& symbol, int& outY,
    int& outX)
{
    bool inClassOrStruct = false;
    int braceDepth = 0;

    for(int y = 0; y < (int)lines.size(); y++)
    {
        const std::string& line = lines[y];

        if(text_utils::contains(line, "class ") ||
           text_utils::contains(line, "struct "))
        {
            inClassOrStruct = true;
            braceDepth = 0;
        }

        for(char c : line)
        {
            if(c == '{')
                braceDepth++;
            else if(c == '}')
            {
                braceDepth--;
                if(braceDepth == 0 && inClassOrStruct)
                    inClassOrStruct = false;
            }
        }

        if(inClassOrStruct && braceDepth == 1)
        {
            int col = findLocalDeclaration(line, symbol);
            if(col >= 0)
            {
                outY = y;
                outX = col;
                return true;
            }
        }
    }

    return false;
}

bool CppNavigationUtilities::isHeaderFile(const std::string& path)
{
    return path == ".h" || path == ".hpp";
}

bool CppNavigationUtilities::isSourceFile(const std::string& path)
{
    return path == ".c" || path == ".cpp" || path == ".cc";
}

std::pair<std::string, bool>
CppNavigationUtilities::extractIncludePath(const std::string& line)
{
    size_t includePos = line.find("#include");
    if(text_utils::is_not_found(includePos))
        return {"", false};

    size_t pos = includePos + 8;
    while(pos < line.length() && std::isspace((unsigned char)line[pos]))
        pos++;

    if(pos >= line.length())
        return {"", false};

    bool isSystem = false;
    char openDelim = line[pos];
    char closeDelim;

    if(openDelim == '<')
    {
        isSystem = true;
        closeDelim = '>';
    }
    else if(openDelim == '"')
    {
        closeDelim = '"';
    }
    else
    {
        return {"", false};
    }

    pos++;
    size_t start = pos;
    size_t end = line.find(closeDelim, pos);

    if(text_utils::is_not_found(end))
        return {"", false};

    return {line.substr(start, end - start), isSystem};
}

std::string
CppNavigationUtilities::resolveSystemInclude(const std::string& includeName)
{
    std::vector<std::string> systemPaths;

#ifdef __APPLE__
    systemPaths = {
        "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/"
        "Developer/SDKs/MacOSX.sdk/usr/include/c++/v1",
        "/Applications/Xcode.app/Contents/Developer/Toolchains/"
        "XcodeDefault.xctoolchain/usr/lib/clang/17/include",
        "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/"
        "Developer/SDKs/MacOSX.sdk/usr/include",
        "/usr/local/include",
        "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/"
        "v1",
        "/Library/Developer/CommandLineTools/usr/include/c++/v1",
    };
#else
    systemPaths = {
        "/usr/include/c++/11", "/usr/include/c++/10", "/usr/include/c++/9",
        "/usr/include",        "/usr/local/include",
    };
#endif

    for(const auto& basePath : systemPaths)
    {
        std::filesystem::path fullPath =
            std::filesystem::path(basePath) / includeName;
        std::error_code ec;
        if(std::filesystem::exists(fullPath, ec))
            return fullPath.string();
    }

    return "";
}

bool CppNavigationUtilities::isLikelyDefinition(const std::string& line,
                                                const std::string& symbol)
{
    size_t commentPos = line.find("//");
    std::string effectiveLine =
        (text_utils::is_found(commentPos)) ? line.substr(0, commentPos) : line;

    if(text_utils::contains(effectiveLine, symbol + "("))
        return true;

    if(text_utils::contains(effectiveLine, "::" + symbol + "("))
        return true;

    return false;
}
