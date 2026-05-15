#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

class MlangUtilities
{
public:
    static bool findBuiltinType(std::string_view symbol, std::string& path,
                                int& line);
    static bool findBuiltinMacro(std::string_view symbol, std::string& path,
                                 int& line);
    static bool findBuiltinAttribute(std::string_view symbol, std::string& path,
                                     int& line);
    static bool findBuiltinFunction(std::string_view symbol, std::string& path,
                                    int& line,
                                    std::string_view contextFilePath = {});
    static bool findTopLevelDefInLines(const std::vector<std::string>& lines,
                                       std::string_view symbol, int& outY,
                                       int& outX);
    static bool resolveModuleFile(std::string_view modulePath,
                                  std::string_view contextFilePath,
                                  std::string& outPath);
    static bool moduleDeclUnderCursor(std::string_view line, int cursorX,
                                      std::string& modulePath);

private:
    static std::string moduleRelPath(std::string_view modulePath);
    static std::vector<std::filesystem::path> stdlibRoots();
};
