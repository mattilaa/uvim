#pragma once

#include <filesystem>
#include <string>

class EditorPathUtilities
{
public:
    static std::string resolveExecutablePath(const std::string& exe);
    static std::filesystem::path currentWorkingDirectory();
    static std::filesystem::path
    resolveEditorPath(const std::filesystem::path& input);
    static bool setWorkingDirectory(const std::filesystem::path& input,
                                    std::string& displayPath,
                                    std::string& errorMessage);
    static std::filesystem::path homeDirectory();
    static std::string defaultThemeDir();

private:
    static std::filesystem::path
    expandUserPath(const std::filesystem::path& input);
};
