#include <array>
#include <string_view>

namespace constants
{

static constexpr std::array<std::string_view, 10> CPP_FILE_EXTENSIONS = {
    ".cpp", ".cc", ".cxx", ".h",  ".hpp", ".hxx",
    ".c",   ".C",  ".mla", ".ixx"
    // add/remove as you like (e.g. ".tpp", ".inl", ".hh", ".cu")
};

}
