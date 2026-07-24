#include "diagnostics_common.h"
#include "text_utils.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>

namespace fs = std::filesystem;

namespace diagnostics_common
{
std::string shellQuote(const std::string& text)
{
    std::string out = "'";
    for(char ch : text)
    {
        if(ch == '\'')
            out += "'\\''";
        else
            out.push_back(ch);
    }
    out.push_back('\'');
    return out;
}

std::string resolveExecutablePath(const std::string& exe)
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

fs::path makeTempPath(const std::string& stem, const std::string& extension)
{
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec);
    if(ec || dir.empty())
        dir = fs::current_path(ec);

    const auto now =
        std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream name;
    name << stem << '_' << now << '_' << reinterpret_cast<std::uintptr_t>(&dir)
         << extension;
    return dir / name.str();
}

std::string relativeDisplayPath(const fs::path& path, const fs::path& root)
{
    std::error_code ec;
    fs::path rel = fs::relative(path, root, ec);
    if(!ec && !rel.empty())
        return rel.string();
    return path.string();
}

std::string normalizedPathString(const fs::path& path)
{
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec);
    if(ec)
        absolute = path;
    return absolute.lexically_normal().string();
}

std::vector<std::string> readLines(const fs::path& path)
{
    std::vector<std::string> lines;
    std::ifstream in(path);
    std::string line;
    while(std::getline(in, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

} // namespace diagnostics_common
