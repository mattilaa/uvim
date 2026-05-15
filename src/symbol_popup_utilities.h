#pragma once

#include <string>
#include <string_view>
#include <vector>

class SymbolPopupUtilities
{
public:
    static std::string
    collectSignatureLine(const std::vector<std::string>& lines, int startY,
                         int maxLines);
    static std::string extractInitializerTypeCandidate(std::string_view rhs);
    static bool findDeclarationInLines(const std::vector<std::string>& lines,
                                       const std::string& symbol, int& outY,
                                       int& outX);
    static bool loadFileLines(const std::string& path,
                              std::vector<std::string>& out);
    static std::string lastQualifier(std::string_view text);
    static std::string extractTypeBeforeName(const std::string& line,
                                             const std::string& name);

private:
    static bool isControlStatement(std::string_view line);
};
