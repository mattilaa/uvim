#include "editor_path_utilities.h"
#include "text_utils.h"

#include <cstdlib>
#include <mutex>
#include <string_view>

namespace fs = std::filesystem;

namespace
{
std::mutex& editor_working_directory_mutex()
{
    static std::mutex mutex;
    return mutex;
}

fs::path& editor_working_directory_unlocked()
{
    static fs::path directory = []
    {
        std::error_code ec;
        fs::path cwd = fs::current_path(ec);
        return ec ? fs::path{} : cwd;
    }();
    return directory;
}
} // namespace

std::string EditorPathUtilities::resolveExecutablePath(const std::string& exe)
{
    if(exe.empty())
        return {};
    fs::path exePath(exe);
    if(exePath.has_parent_path())
    {
        std::error_code ec;
        if(fs::exists(exePath, ec) && fs::is_regular_file(exePath, ec))
            return exePath.string();
        return {};
    }

    const char* path = std::getenv("PATH");
    if(!path || !*path)
        return {};

    std::string_view pathView{path};
    size_t start = 0;
    while(start < pathView.size())
    {
#ifdef _WIN32
        size_t end = pathView.find(';', start);
#else
        size_t end = pathView.find(':', start);
#endif
        if(text_utils::is_not_found(end))
            end = pathView.size();
        if(end > start)
        {
            fs::path candidate =
                fs::path(std::string(pathView.substr(start, end - start))) /
                exe;
            std::error_code ec;
            if(fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec))
                return candidate.string();
        }
        start = end + 1;
    }
    return {};
}

fs::path EditorPathUtilities::homeDirectory()
{
#ifdef _WIN32
    if(const char* userProfile = std::getenv("USERPROFILE"))
        return userProfile;
    const char* homeDrive = std::getenv("HOMEDRIVE");
    const char* homePath = std::getenv("HOMEPATH");
    if(homeDrive && homePath)
        return std::string(homeDrive) + std::string(homePath);
#endif
    if(const char* home = std::getenv("HOME"))
        return home;

    std::error_code ec;
    fs::path cwd = fs::current_path(ec);
    return ec ? fs::path{} : cwd;
}

fs::path EditorPathUtilities::expandUserPath(const fs::path& input)
{
    std::string text = input.string();
    if(text.empty() || text[0] != '~')
        return input;

    fs::path home = homeDirectory();
    if(home.empty())
        return input;

    if(text.size() == 1)
        return home;

    const char sep = text[1];
    if(sep == '/' || sep == '\\')
        return home / text.substr(2);

    return input;
}

fs::path EditorPathUtilities::currentWorkingDirectory()
{
    std::lock_guard<std::mutex> lock(editor_working_directory_mutex());
    return editor_working_directory_unlocked();
}

fs::path EditorPathUtilities::resolveEditorPath(const fs::path& input)
{
    fs::path path = expandUserPath(input);
    if(path.is_absolute())
        return path;

    fs::path base = currentWorkingDirectory();
    if(base.empty())
    {
        std::error_code ec;
        base = fs::current_path(ec);
    }
    return base / path;
}

bool EditorPathUtilities::setWorkingDirectory(const fs::path& input,
                                              std::string& displayPath,
                                              std::string& errorMessage)
{
    fs::path resolved = resolveEditorPath(input);

    std::error_code ec;
    fs::path normalized = fs::weakly_canonical(resolved, ec);
    if(ec)
    {
        ec.clear();
        normalized = fs::absolute(resolved, ec);
    }
    if(ec)
    {
        errorMessage = ec.message();
        return false;
    }

    if(!fs::is_directory(normalized, ec) || ec)
    {
        errorMessage = ec ? ec.message() : "not a directory";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(editor_working_directory_mutex());
        editor_working_directory_unlocked() = normalized;
    }

    displayPath = normalized.string();
    return true;
}

std::string EditorPathUtilities::defaultThemeDir()
{
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    std::string base;
    if(xdg && *xdg)
        base = xdg;
    else if(home && *home)
        base = std::string(home) + "/.config";
    else
        return "";
    return base + "/uvim/themes";
}
