#pragma once

#include <string>
#include <utility>
#include <vector>

class CppNavigationUtilities
{
public:
    static bool isIdent(char c);
    static bool searchLocalDefinition(const std::vector<std::string>& lines,
                                      const std::string& symbol, int startY,
                                      int startX, int& outY, int& outX);
    static bool searchMemberDefinition(const std::vector<std::string>& lines,
                                       const std::string& symbol, int& outY,
                                       int& outX);
    static bool isHeaderFile(const std::string& path);
    static bool isSourceFile(const std::string& path);
    static std::pair<std::string, bool>
    extractIncludePath(const std::string& line);
    static std::string resolveSystemInclude(const std::string& includeName);
    static bool isLikelyDefinition(const std::string& line,
                                   const std::string& symbol);

private:
    static int findLocalDeclaration(const std::string& line,
                                    const std::string& symbol);
};
