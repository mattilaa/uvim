#include "executable_lookup.h"
#include "text_utils.h"

#include <cstdlib>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace executable_lookup
{
namespace
{
std::string stripQuotes(std::string path)
{
    while(path.size() >= 2)
    {
        const char first = path.front();
        const char last = path.back();
        if((first == '"' && last == '"') || (first == '\'' && last == '\''))
            path = path.substr(1, path.size() - 2);
        else
            break;
    }
    return path;
}

bool isExecutableFile(const fs::path& path)
{
    std::error_code ec;
    return fs::exists(path, ec) && fs::is_regular_file(path, ec);
}

std::vector<std::string> executableNames(std::string_view name)
{
    std::vector<std::string> names{std::string(name)};
#ifdef _WIN32
    if(text_utils::contains(name, "."))
        return names;
    names.push_back(std::string(name) + ".exe");
    names.push_back(std::string(name) + ".cmd");
    names.push_back(std::string(name) + ".bat");
    names.push_back(std::string(name) + ".com");
#endif
    return names;
}
} // namespace

Result find(std::string_view name)
{
    const std::string requested = stripQuotes(std::string(name));
    if(requested.empty())
        return {};

    if(text_utils::contains(requested, '/') ||
       text_utils::contains(requested, '\\'))
    {
        if(isExecutableFile(requested))
            return {true, requested};
        return {};
    }

    const char* envPath = std::getenv("PATH");
    if(!envPath || !*envPath)
        return {};

    std::string_view pathView{envPath};
    size_t start = 0;
    while(start <= pathView.size())
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
            fs::path dir(std::string(pathView.substr(start, end - start)));
            for(const std::string& candidateName : executableNames(requested))
            {
                fs::path candidate = dir / candidateName;
                if(isExecutableFile(candidate))
                    return {true, candidate.string()};
            }
        }
        if(end == pathView.size())
            break;
        start = end + 1;
    }

    return {};
}

Result findAny(std::initializer_list<std::string_view> names)
{
    for(std::string_view name : names)
    {
        Result result = find(name);
        if(result.found)
            return result;
    }
    return {};
}
} // namespace executable_lookup
