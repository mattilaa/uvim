#include "text_utils.h"

#include <fstream>
#include <system_error>

namespace text_utils
{
bool find_prefixed_marker_in_file(std::string_view symbol,
                                  const std::filesystem::path& file,
                                  std::string_view marker, std::string& path,
                                  int& line)
{
    std::error_code ec;
    if(!std::filesystem::exists(file, ec))
        return false;

    std::ifstream in(file);
    if(!in)
        return false;

    const std::string symbolLower = ascii_lower(symbol);
    std::string lineText;
    int lineNo = 0;
    while(std::getline(in, lineText))
    {
        ++lineNo;
        if(lineText.rfind(marker, 0) != 0)
            continue;

        std::string_view name(lineText);
        name.remove_prefix(marker.size());
        if(name.empty())
            continue;

        if(name == symbol || ascii_lower(name) == symbolLower)
        {
            path = file.string();
            line = lineNo - 1;
            return true;
        }
    }

    return false;
}
} // namespace text_utils
